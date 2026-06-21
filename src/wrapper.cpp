/*
 * Rustyfoot JSFX Wrapper — LV2 plugin that hosts JSFX via ysfx
 * Copyright (C) 2026 Dataforte <info@dataforte.net>
 * Licensed under GPLv2+
 *
 * Uses lv2_lib_descriptor() to read per-bundle wrapper.json config,
 * allowing a single .so to serve different JSFX plugins.
 */

#include "ysfx.h"

#include "lv2/lv2.h"
#include "lv2/state.h"
#include "lv2/urid.h"
#include "lv2/atom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

#define MAX_PARAMS 64

struct ParamMapping {
    uint32_t lv2_index;
    uint32_t slider_index;
    float min;
    float max;
    float def;
};

static const char* JSFX_SEARCH_PATHS[] = {
    "/var/lib/stom-pi/jsfx",
    "/usr/share/stom-pi/jsfx",
    nullptr
};

struct WrapperConfig {
    std::string uri;
    std::string jsfx_file;
    std::string jsfx_path;
    std::string name;
    uint32_t audio_inputs;
    uint32_t audio_outputs;
    std::vector<ParamMapping> parameters;
};

struct WrapperInstance {
    const WrapperConfig* config;

    ysfx_config_t* ysfx_config;
    ysfx_t* fx;

    float* port_buffers[MAX_PARAMS + 128];
    float prev_values[MAX_PARAMS];
    uint32_t total_ports;

    double sample_rate;
};

// --- Base64 encoding for state serialization ---

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out += b64_table[(n >> 18) & 0x3f];
        out += b64_table[(n >> 12) & 0x3f];
        out += (i + 1 < len) ? b64_table[(n >> 6) & 0x3f] : '=';
        out += (i + 2 < len) ? b64_table[n & 0x3f] : '=';
    }
    return out;
}

static std::vector<uint8_t> base64_decode(const char* str, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        int val = -1;
        if (c >= 'A' && c <= 'Z') val = c - 'A';
        else if (c >= 'a' && c <= 'z') val = c - 'a' + 26;
        else if (c >= '0' && c <= '9') val = c - '0' + 52;
        else if (c == '+') val = 62;
        else if (c == '/') val = 63;
        else continue;
        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(buf >> bits));
        }
    }
    return out;
}

// --- JSFX file resolution ---

static std::string resolve_jsfx_path(const std::string& filename,
                                      const char* bundle_path) {
    if (!filename.empty() && filename[0] == '/')
        return filename;

    // Check bundle directory first
    if (bundle_path) {
        std::string candidate = std::string(bundle_path) + filename;
        if (access(candidate.c_str(), F_OK) == 0)
            return candidate;
    }

    for (const char** p = JSFX_SEARCH_PATHS; *p; p++) {
        std::string candidate = std::string(*p) + "/" + filename;
        if (access(candidate.c_str(), F_OK) == 0)
            return candidate;
    }

    const char* home = getenv("HOME");
    if (home) {
        std::string candidate = std::string(home) + "/.jsfx/" + filename;
        if (access(candidate.c_str(), F_OK) == 0)
            return candidate;
    }

    return "";
}

// --- Minimal JSON parser for wrapper.json ---

static std::string json_get_string(const char* json, const char* key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return "";
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return "";
    pos = strchr(pos, '"');
    if (!pos) return "";
    pos++;
    const char* end = strchr(pos, '"');
    if (!end) return "";
    return std::string(pos, end - pos);
}

static double json_get_number(const char* json, const char* key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* pos = strstr(json, search);
    if (!pos) return 0.0;
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return 0.0;
    while (*pos && (*pos == ' ' || *pos == ':')) pos++;
    return atof(pos);
}

static bool parse_wrapper_config(const char* bundle_path, WrapperConfig& config) {
    std::string path = std::string(bundle_path) + "wrapper.json";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "jsfx-wrapper: cannot open %s\n", path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        fprintf(stderr, "jsfx-wrapper: %s is empty or invalid\n", path.c_str());
        return false;
    }

    std::vector<char> buf(size + 1);
    size_t nread = fread(buf.data(), 1, size, f);
    fclose(f);

    if (nread != (size_t)size) {
        fprintf(stderr, "jsfx-wrapper: failed to read %s\n", path.c_str());
        return false;
    }
    buf[nread] = '\0';

    const char* json = buf.data();
    config.uri = json_get_string(json, "uri");
    config.name = json_get_string(json, "name");
    config.jsfx_file = json_get_string(json, "jsfx_file");
    config.audio_inputs = (uint32_t)json_get_number(json, "audio_inputs");
    config.audio_outputs = (uint32_t)json_get_number(json, "audio_outputs");

    config.jsfx_path = resolve_jsfx_path(config.jsfx_file, bundle_path);
    if (config.jsfx_path.empty()) {
        fprintf(stderr, "jsfx-wrapper: cannot find %s in any search path\n",
                config.jsfx_file.c_str());
        return false;
    }

    // Parse parameters array
    const char* params = strstr(json, "\"parameters\"");
    if (params) {
        params = strchr(params, '[');
        if (params) {
            params++;
            while (*params) {
                const char* obj = strchr(params, '{');
                if (!obj) break;
                const char* obj_end = strchr(obj, '}');
                if (!obj_end) break;

                std::string chunk(obj, obj_end - obj + 1);
                ParamMapping pm;
                pm.lv2_index = (uint32_t)json_get_number(chunk.c_str(), "lv2_index");
                pm.slider_index = (uint32_t)json_get_number(chunk.c_str(), "slider_index");
                pm.min = (float)json_get_number(chunk.c_str(), "min");
                pm.max = (float)json_get_number(chunk.c_str(), "max");
                pm.def = (float)json_get_number(chunk.c_str(), "default");
                config.parameters.push_back(pm);

                params = obj_end + 1;
            }
        }
    }

    if (config.uri.empty() || config.jsfx_file.empty()) {
        fprintf(stderr, "jsfx-wrapper: invalid wrapper.json (missing uri or jsfx_file)\n");
        return false;
    }

    return true;
}

// --- LV2 plugin callbacks ---

static LV2_Handle wrapper_instantiate(const LV2_Descriptor* descriptor,
                                      double rate,
                                      const char* bundle_path,
                                      const LV2_Feature* const* features) {
    (void)features;

    WrapperConfig* config = new WrapperConfig();
    if (!parse_wrapper_config(bundle_path, *config)) {
        delete config;
        return nullptr;
    }

    if (config->uri != descriptor->URI) {
        fprintf(stderr, "jsfx-wrapper: URI mismatch: config=%s descriptor=%s\n",
                config->uri.c_str(), descriptor->URI);
        delete config;
        return nullptr;
    }

    WrapperInstance* inst = new WrapperInstance();
    inst->config = config;
    inst->sample_rate = rate;
    memset(inst->port_buffers, 0, sizeof(inst->port_buffers));
    memset(inst->prev_values, 0, sizeof(inst->prev_values));

    inst->ysfx_config = ysfx_config_new();
    ysfx_guess_file_roots(inst->ysfx_config, config->jsfx_path.c_str());
    ysfx_register_builtin_audio_formats(inst->ysfx_config);

    inst->fx = ysfx_new(inst->ysfx_config);
    if (!ysfx_load_file(inst->fx, config->jsfx_path.c_str(), 0)) {
        fprintf(stderr, "jsfx-wrapper: failed to load JSFX: %s\n",
                config->jsfx_path.c_str());
        ysfx_free(inst->fx);
        ysfx_config_free(inst->ysfx_config);
        delete config;
        delete inst;
        return nullptr;
    }

    if (!ysfx_compile(inst->fx, ysfx_compile_no_gfx)) {
        fprintf(stderr, "jsfx-wrapper: failed to compile JSFX: %s\n",
                config->jsfx_path.c_str());
        ysfx_free(inst->fx);
        ysfx_config_free(inst->ysfx_config);
        delete config;
        delete inst;
        return nullptr;
    }

    ysfx_set_sample_rate(inst->fx, rate);
    ysfx_set_block_size(inst->fx, 128);

    // Set initial slider values from defaults
    for (size_t i = 0; i < config->parameters.size(); i++) {
        const auto& pm = config->parameters[i];
        ysfx_slider_set_value(inst->fx, pm.slider_index, pm.def);
        inst->prev_values[i] = pm.def;
    }

    ysfx_init(inst->fx);

    // Port layout: control inputs, then audio inputs, then audio outputs
    inst->total_ports = (uint32_t)config->parameters.size()
                      + config->audio_inputs
                      + config->audio_outputs;

    fprintf(stderr, "jsfx-wrapper: loaded %s (%zu params, %u in, %u out)\n",
            config->name.c_str(), config->parameters.size(),
            config->audio_inputs, config->audio_outputs);

    return inst;
}

static void wrapper_connect_port(LV2_Handle instance, uint32_t port, void* data) {
    WrapperInstance* inst = static_cast<WrapperInstance*>(instance);
    if (port < inst->total_ports) {
        inst->port_buffers[port] = static_cast<float*>(data);
    }
}

static void wrapper_activate(LV2_Handle instance) {
    (void)instance;
}

static void wrapper_run(LV2_Handle instance, uint32_t n_samples) {
    WrapperInstance* inst = static_cast<WrapperInstance*>(instance);
    const WrapperConfig* cfg = inst->config;
    uint32_t num_params = (uint32_t)cfg->parameters.size();

    // Sync control port values to ysfx sliders
    for (uint32_t i = 0; i < num_params; i++) {
        float* port = inst->port_buffers[i];
        if (!port) continue;

        float value = *port;
        if (value != inst->prev_values[i]) {
            const auto& pm = cfg->parameters[i];
            ysfx_slider_set_value(inst->fx, pm.slider_index, value);
            inst->prev_values[i] = value;
        }
    }

    // Set up audio buffers
    const float* inputs[ysfx_max_channels] = {};
    float* outputs[ysfx_max_channels] = {};

    for (uint32_t i = 0; i < cfg->audio_inputs && i < ysfx_max_channels; i++)
        inputs[i] = inst->port_buffers[num_params + i];

    for (uint32_t i = 0; i < cfg->audio_outputs && i < ysfx_max_channels; i++)
        outputs[i] = inst->port_buffers[num_params + cfg->audio_inputs + i];

    ysfx_process_float(inst->fx, inputs, outputs,
                       cfg->audio_inputs, cfg->audio_outputs, n_samples);
}

static void wrapper_deactivate(LV2_Handle instance) {
    (void)instance;
}

static void wrapper_cleanup(LV2_Handle instance) {
    WrapperInstance* inst = static_cast<WrapperInstance*>(instance);
    ysfx_free(inst->fx);
    ysfx_config_free(inst->ysfx_config);
    delete inst->config;
    delete inst;
}

// --- LV2 State ---

static LV2_State_Status wrapper_save(LV2_Handle instance,
                                      LV2_State_Store_Function store,
                                      LV2_State_Handle handle,
                                      uint32_t,
                                      const LV2_Feature* const* features) {
    WrapperInstance* inst = static_cast<WrapperInstance*>(instance);

    LV2_URID_Map* map = nullptr;
    for (int i = 0; features && features[i]; i++) {
        if (!strcmp(features[i]->URI, LV2_URID__map))
            map = static_cast<LV2_URID_Map*>(features[i]->data);
    }
    if (!map) return LV2_STATE_ERR_NO_FEATURE;

    ysfx_state_t* state = ysfx_save_state(inst->fx);
    if (!state) return LV2_STATE_ERR_UNKNOWN;

    // Encode state as text: "slider:index:value\n..." + "data:base64\n"
    std::string encoded = "JSFX_STATE_V1\n";
    for (uint32_t i = 0; i < state->slider_count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "slider:%u:%.17g\n",
                 state->sliders[i].index, state->sliders[i].value);
        encoded += line;
    }
    if (state->data && state->data_size > 0) {
        encoded += "data:";
        encoded += base64_encode(state->data, state->data_size);
        encoded += "\n";
    }

    ysfx_state_free(state);

    LV2_URID key = map->map(map->handle, "urn:stom-pi:jsfx:state");
    LV2_URID type = map->map(map->handle, LV2_ATOM__String);
    store(handle, key, encoded.c_str(), encoded.size() + 1, type,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    return LV2_STATE_SUCCESS;
}

static LV2_State_Status wrapper_restore(LV2_Handle instance,
                                         LV2_State_Retrieve_Function retrieve,
                                         LV2_State_Handle handle,
                                         uint32_t,
                                         const LV2_Feature* const* features) {
    WrapperInstance* inst = static_cast<WrapperInstance*>(instance);

    LV2_URID_Map* map = nullptr;
    for (int i = 0; features && features[i]; i++) {
        if (!strcmp(features[i]->URI, LV2_URID__map))
            map = static_cast<LV2_URID_Map*>(features[i]->data);
    }
    if (!map) return LV2_STATE_ERR_NO_FEATURE;

    LV2_URID key = map->map(map->handle, "urn:stom-pi:jsfx:state");
    size_t size = 0;
    uint32_t type = 0;
    uint32_t flags = 0;
    const void* value = retrieve(handle, key, &size, &type, &flags);

    if (!value || size == 0) return LV2_STATE_SUCCESS;

    const char* text = static_cast<const char*>(value);

    // Parse "JSFX_STATE_V1" header
    if (strncmp(text, "JSFX_STATE_V1\n", 14) != 0)
        return LV2_STATE_ERR_BAD_TYPE;

    std::vector<ysfx_state_slider_t> sliders;
    std::vector<uint8_t> data;

    const char* line = text + 14;
    while (*line) {
        const char* eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);
        size_t line_len = eol - line;

        if (line_len > 7 && !strncmp(line, "slider:", 7)) {
            ysfx_state_slider_t s;
            if (sscanf(line + 7, "%u:%lf", &s.index, &s.value) == 2)
                sliders.push_back(s);
        } else if (line_len > 5 && !strncmp(line, "data:", 5)) {
            data = base64_decode(line + 5, line_len - 5);
        }

        line = (*eol) ? eol + 1 : eol;
    }

    ysfx_state_t state;
    state.sliders = sliders.empty() ? nullptr : sliders.data();
    state.slider_count = (uint32_t)sliders.size();
    state.data = data.empty() ? nullptr : data.data();
    state.data_size = data.size();

    ysfx_load_state(inst->fx, &state);

    // Update prev_values so run() doesn't overwrite restored slider values
    const WrapperConfig* cfg = inst->config;
    for (size_t i = 0; i < cfg->parameters.size(); i++) {
        inst->prev_values[i] = (float)ysfx_slider_get_value(
            inst->fx, cfg->parameters[i].slider_index);
    }

    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_interface = {
    wrapper_save,
    wrapper_restore
};

static const void* wrapper_extension_data(const char* uri) {
    if (!strcmp(uri, LV2_STATE__interface))
        return &state_interface;
    return nullptr;
}

// --- LV2 Library Descriptor ---

struct LibHandle {
    WrapperConfig config;
    LV2_Descriptor descriptor;
    char* uri_storage;
};

static void lib_cleanup(LV2_Lib_Handle handle) {
    LibHandle* lib = static_cast<LibHandle*>(handle);
    free(lib->uri_storage);
    delete lib;
}

static const LV2_Descriptor* lib_get_plugin(LV2_Lib_Handle handle, uint32_t index) {
    if (index != 0) return nullptr;
    LibHandle* lib = static_cast<LibHandle*>(handle);
    return &lib->descriptor;
}

extern "C" {

LV2_SYMBOL_EXPORT
const LV2_Lib_Descriptor* lv2_lib_descriptor(const char* bundle_path,
                                              const LV2_Feature* const*) {
    LibHandle* lib = new LibHandle();

    if (!parse_wrapper_config(bundle_path, lib->config)) {
        delete lib;
        return nullptr;
    }

    lib->uri_storage = strdup(lib->config.uri.c_str());

    lib->descriptor.URI = lib->uri_storage;
    lib->descriptor.instantiate = wrapper_instantiate;
    lib->descriptor.connect_port = wrapper_connect_port;
    lib->descriptor.activate = wrapper_activate;
    lib->descriptor.run = wrapper_run;
    lib->descriptor.deactivate = wrapper_deactivate;
    lib->descriptor.cleanup = wrapper_cleanup;
    lib->descriptor.extension_data = wrapper_extension_data;

    LV2_Lib_Descriptor* lib_desc = new LV2_Lib_Descriptor();
    lib_desc->handle = lib;
    lib_desc->size = sizeof(LV2_Lib_Descriptor);
    lib_desc->cleanup = lib_cleanup;
    lib_desc->get_plugin = lib_get_plugin;

    return lib_desc;
}

} // extern "C"
