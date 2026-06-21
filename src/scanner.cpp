/*
 * Rustyfoot JSFX Scanner — introspects a JSFX file and outputs wrapper.json
 * Copyright (C) 2026 Dataforte <info@dataforte.net>
 * Licensed under GPLv2+
 *
 * Usage: stom-pi-jsfx-scan <path-to.jsfx> [output-dir]
 *
 * Outputs wrapper.json with slider/parameter metadata for the LV2 wrapper.
 * If output-dir is omitted, writes to stdout.
 */

#include "ysfx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::string json_escape(const char* s) {
    std::string out;
    if (!s) return out;
    for (; *s; s++) {
        switch (*s) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:   out += *s; break;
        }
    }
    return out;
}

static std::string make_symbol(const char* name) {
    std::string sym;
    if (!name || !*name) return "param";
    for (const char* p = name; *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            sym += c;
        } else if (c >= 'A' && c <= 'Z') {
            sym += (char)(c + 32);
        } else if (c == ' ' || c == '-') {
            sym += '_';
        }
    }
    if (sym.empty()) sym = "param";
    if (sym[0] >= '0' && sym[0] <= '9') sym = "p_" + sym;
    return sym;
}

static std::string make_uri(const char* name) {
    std::string lower;
    if (!name || !*name) return "urn:stom-pi:jsfx:unknown";
    for (const char* p = name; *p; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
            lower += c;
        else if (c == ' ')
            lower += '-';
    }
    return "urn:stom-pi:jsfx:" + lower;
}

static const char* map_tags_to_category(ysfx_t* fx) {
    const char* tags[16];
    uint32_t count = ysfx_get_tags(fx, tags, 16);

    for (uint32_t i = 0; i < count; i++) {
        const char* tag = tags[i];
        if (!strcasecmp(tag, "delay")) return "lv2:DelayPlugin";
        if (!strcasecmp(tag, "reverb")) return "lv2:ReverbPlugin";
        if (!strcasecmp(tag, "filter")) return "lv2:FilterPlugin";
        if (!strcasecmp(tag, "eq") || !strcasecmp(tag, "equalizer"))
            return "lv2:EQPlugin";
        if (!strcasecmp(tag, "distortion") || !strcasecmp(tag, "saturation") ||
            !strcasecmp(tag, "waveshaper"))
            return "lv2:DistortionPlugin";
        if (!strcasecmp(tag, "dynamics") || !strcasecmp(tag, "compressor") ||
            !strcasecmp(tag, "limiter") || !strcasecmp(tag, "gate"))
            return "lv2:DynamicsPlugin";
        if (!strcasecmp(tag, "modulation") || !strcasecmp(tag, "chorus") ||
            !strcasecmp(tag, "flanger") || !strcasecmp(tag, "phaser") ||
            !strcasecmp(tag, "tremolo") || !strcasecmp(tag, "vibrato"))
            return "lv2:ModulatorPlugin";
        if (!strcasecmp(tag, "synthesis") || !strcasecmp(tag, "instrument") ||
            !strcasecmp(tag, "synth") || !strcasecmp(tag, "generator"))
            return "lv2:InstrumentPlugin";
        if (!strcasecmp(tag, "utility") || !strcasecmp(tag, "tool") ||
            !strcasecmp(tag, "analyzer"))
            return "lv2:UtilityPlugin";
        if (!strcasecmp(tag, "amplifier") || !strcasecmp(tag, "amp") ||
            !strcasecmp(tag, "simulator"))
            return "lv2:SimulatorPlugin";
        if (!strcasecmp(tag, "spatial") || !strcasecmp(tag, "panning"))
            return "lv2:SpatialPlugin";
    }
    return "lv2:Plugin";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to.jsfx> [output-dir]\n", argv[0]);
        return 1;
    }

    const char* jsfx_path = argv[1];
    const char* output_dir = (argc > 2) ? argv[2] : nullptr;

    ysfx_config_t* config = ysfx_config_new();
    ysfx_guess_file_roots(config, jsfx_path);
    ysfx_register_builtin_audio_formats(config);

    ysfx_t* fx = ysfx_new(config);
    if (!ysfx_load_file(fx, jsfx_path, ysfx_load_ignoring_imports)) {
        fprintf(stderr, "Error: failed to load JSFX: %s\n", jsfx_path);
        ysfx_free(fx);
        ysfx_config_free(config);
        return 1;
    }

    const char* name = ysfx_get_name(fx);
    const char* author = ysfx_get_author(fx);
    const char* category = map_tags_to_category(fx);
    uint32_t num_inputs = ysfx_get_num_inputs(fx);
    uint32_t num_outputs = ysfx_get_num_outputs(fx);

    std::string uri = make_uri(name);

    // Extract just the filename from the path
    std::string jsfx_basename(jsfx_path);
    size_t sep = jsfx_basename.rfind('/');
    if (sep != std::string::npos)
        jsfx_basename = jsfx_basename.substr(sep + 1);

    FILE* out = stdout;
    if (output_dir) {
        std::string filepath = std::string(output_dir) + "/wrapper.json";
        out = fopen(filepath.c_str(), "w");
        if (!out) {
            fprintf(stderr, "Error: cannot write to %s\n", filepath.c_str());
            ysfx_free(fx);
            ysfx_config_free(config);
            return 1;
        }
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"jsfx_file\": \"%s\",\n", json_escape(jsfx_basename.c_str()).c_str());
    fprintf(out, "  \"uri\": \"%s\",\n", uri.c_str());
    fprintf(out, "  \"name\": \"%s\",\n", json_escape(name).c_str());
    if (author && author[0])
        fprintf(out, "  \"maker\": \"%s\",\n", json_escape(author).c_str());
    fprintf(out, "  \"category\": \"%s\",\n", category);
    fprintf(out, "  \"audio_inputs\": %u,\n", num_inputs);
    fprintf(out, "  \"audio_outputs\": %u,\n", num_outputs);
    fprintf(out, "  \"parameters\": [\n");

    uint32_t lv2_index = 0;
    for (uint32_t i = 0; i < ysfx_max_sliders; i++) {
        if (!ysfx_slider_exists(fx, i))
            continue;

        // Skip file-picker sliders (not mappable to LV2 control ports)
        if (ysfx_slider_is_path(fx, i))
            continue;

        // Skip hidden sliders
        if (!ysfx_slider_is_initially_visible(fx, i))
            continue;

        const char* slider_name = ysfx_slider_get_name(fx, i);
        ysfx_slider_range_t range;
        ysfx_slider_get_range(fx, i, &range);

        bool is_enum = ysfx_slider_is_enum(fx, i);
        bool is_toggle = false;
        bool is_integer = false;

        // Detect toggles: enum with exactly 2 values, or range 0-1 with step 1
        if (is_enum) {
            uint32_t enum_count = ysfx_slider_get_enum_names(fx, i, nullptr, 0);
            if (enum_count == 2)
                is_toggle = true;
            is_integer = true;
        } else if (range.min == 0.0 && range.max == 1.0 && range.inc >= 1.0) {
            is_toggle = true;
        }

        // Detect integer parameters
        if (!is_enum && range.inc >= 1.0)
            is_integer = true;

        std::string symbol = make_symbol(slider_name);
        // Ensure unique symbols by appending slider index if needed
        // (simple approach; collisions rare in practice)

        if (lv2_index > 0) fprintf(out, ",\n");
        fprintf(out, "    {\n");
        fprintf(out, "      \"lv2_index\": %u,\n", lv2_index);
        fprintf(out, "      \"lv2_symbol\": \"%s\",\n", symbol.c_str());
        fprintf(out, "      \"lv2_name\": \"%s\",\n", json_escape(slider_name).c_str());
        fprintf(out, "      \"slider_index\": %u,\n", i);
        fprintf(out, "      \"min\": %g,\n", range.min);
        fprintf(out, "      \"max\": %g,\n", range.max);
        fprintf(out, "      \"default\": %g", range.def);

        if (is_toggle)
            fprintf(out, ",\n      \"toggle\": true");
        if (is_integer)
            fprintf(out, ",\n      \"integer\": true");

        if (is_enum) {
            const char* labels[128];
            uint32_t enum_count = ysfx_slider_get_enum_names(fx, i, labels, 128);
            if (enum_count > 0) {
                fprintf(out, ",\n      \"enum_labels\": [");
                for (uint32_t e = 0; e < enum_count; e++) {
                    if (e > 0) fprintf(out, ", ");
                    fprintf(out, "\"%s\"", json_escape(labels[e]).c_str());
                }
                fprintf(out, "]");
            }
        }

        fprintf(out, "\n    }");
        lv2_index++;
    }

    fprintf(out, "\n  ]\n");
    fprintf(out, "}\n");

    if (output_dir && out != stdout) {
        fclose(out);
        fprintf(stderr, "Wrote %s/wrapper.json: %s (%u params, %u in, %u out)\n",
                output_dir, name, lv2_index, num_inputs, num_outputs);
    }

    ysfx_free(fx);
    ysfx_config_free(config);
    return 0;
}
