#!/usr/bin/make -f

PREFIX  ?= /usr/local
DESTDIR ?=

CC      ?= gcc
CXX     ?= g++
AR      ?= ar
CFLAGS  ?= -O2 -Wall
CXXFLAGS ?= -O2 -Wall -std=c++17

CARLA_DIR = ildaeil/carla
YSFX_DIR  = $(CARLA_DIR)/source/modules/ysfx

LV2_INCLUDES  = -I$(CARLA_DIR)/source/includes
YSFX_INCLUDES = -I$(YSFX_DIR)/include -I$(YSFX_DIR)/sources \
                -I$(YSFX_DIR)/thirdparty/WDL/source \
                -I$(YSFX_DIR)/thirdparty/dr_libs \
                -I$(YSFX_DIR)/thirdparty/stb

YSFX_FLAGS = -fsigned-char \
    -DEELSCRIPT_NO_NET -DEELSCRIPT_NO_LICE \
    -DNSEEL_ATOF=ysfx_wdl_atof \
    -DWDL_FFT_REALSIZE=8 \
    -DWDL_LINEPARSE_ATOF=ysfx_wdl_atof \
    -DWDL_WIN32_UTF8_NO_UI_IMPL \
    -DYSFX_API= \
    -DYSFX_NO_STANDARD_MUTEX \
    -DYSFX_NO_GFX \
    -D_FILE_OFFSET_BITS=64 \
    -Wno-ignored-attributes -Wno-sign-compare \
    -Wno-unused-function -Wno-unused-parameter \
    -pthread

ARCH := $(shell uname -m)
ifeq ($(filter x86_64 i686 i386,$(ARCH)),)
ifeq ($(filter aarch64 arm64,$(ARCH)),)
YSFX_FLAGS += -DEEL_TARGET_PORTABLE
endif
endif

YSFX_CXX_SRCS = \
    $(YSFX_DIR)/sources/ysfx.cpp \
    $(YSFX_DIR)/sources/ysfx_api_eel.cpp \
    $(YSFX_DIR)/sources/ysfx_api_file.cpp \
    $(YSFX_DIR)/sources/ysfx_api_gfx.cpp \
    $(YSFX_DIR)/sources/ysfx_api_reaper.cpp \
    $(YSFX_DIR)/sources/ysfx_audio_flac.cpp \
    $(YSFX_DIR)/sources/ysfx_audio_wav.cpp \
    $(YSFX_DIR)/sources/ysfx_config.cpp \
    $(YSFX_DIR)/sources/ysfx_eel_utils.cpp \
    $(YSFX_DIR)/sources/ysfx_midi.cpp \
    $(YSFX_DIR)/sources/ysfx_parse.cpp \
    $(YSFX_DIR)/sources/ysfx_reader.cpp \
    $(YSFX_DIR)/sources/ysfx_utils.cpp \
    $(YSFX_DIR)/sources/ysfx_utils_fts.cpp

YSFX_C_SRCS = \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-caltab.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-cfunc.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-compiler.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-eval.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-lextab.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-ram.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/eel2/nseel-yylex.c \
    $(YSFX_DIR)/thirdparty/WDL/source/WDL/fft.c

ifneq ($(filter x86_64,$(ARCH)),)
YSFX_ASM_SRCS = $(YSFX_DIR)/sources/eel2-gas/sources/asm-nseel-x64-sse.S
endif

YSFX_CXX_OBJS = $(patsubst $(YSFX_DIR)/%.cpp,build/ysfx/%.cpp.o,$(YSFX_CXX_SRCS))
YSFX_C_OBJS   = $(patsubst $(YSFX_DIR)/%.c,build/ysfx/%.c.o,$(YSFX_C_SRCS))
YSFX_ASM_OBJS = $(patsubst $(YSFX_DIR)/%.S,build/ysfx/%.S.o,$(YSFX_ASM_SRCS))
YSFX_OBJS     = $(YSFX_CXX_OBJS) $(YSFX_C_OBJS) $(YSFX_ASM_OBJS)

LINK_FLAGS = -ldl -pthread -lm

.PHONY: all ysfx clean install

all: build/jsfx-wrapper.so build/stom-pi-jsfx-scan

ysfx: build/ysfx.a

build/ysfx.a: $(YSFX_OBJS)
	@echo "Creating ysfx.a"
	@rm -f $@
	@$(AR) crs $@ $^

build/jsfx-wrapper.so: src/wrapper.cpp build/ysfx.a | build
	$(CXX) -shared -fPIC $(CXXFLAGS) $(LV2_INCLUDES) \
		-I$(YSFX_DIR)/include \
		-o $@ $< build/ysfx.a $(LINK_FLAGS)

build/stom-pi-jsfx-scan: src/scanner.cpp build/ysfx.a | build
	$(CXX) $(CXXFLAGS) -I$(YSFX_DIR)/include \
		-o $@ $< build/ysfx.a $(LINK_FLAGS)

build/ysfx/%.cpp.o: $(YSFX_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CXX) $< $(CXXFLAGS) $(YSFX_FLAGS) $(YSFX_INCLUDES) -fPIC -c -o $@

build/ysfx/%.c.o: $(YSFX_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CC) $< $(CFLAGS) $(YSFX_FLAGS) $(YSFX_INCLUDES) -fPIC -c -o $@

build/ysfx/%.S.o: $(YSFX_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CC) $< $(CFLAGS) $(YSFX_FLAGS) -fPIC -c -o $@

build:
	mkdir -p build

install: all
	install -D -m 644 build/jsfx-wrapper.so $(DESTDIR)$(PREFIX)/lib/stom-pi/jsfx-wrapper.so
	install -D -m 755 build/stom-pi-jsfx-scan $(DESTDIR)$(PREFIX)/bin/stom-pi-jsfx-scan

clean:
	rm -rf build
