BUILDIR := bin
EXECUTABLE := $(BUILDIR)/tst
DATADIR := data

CC ?= gcc
CXX ?= g++

PLATFORM ?= lida_platform1
GLSLANG ?= glslangValidator

CFLAGS = -O0 -g -Wall -Wextra -Wpedantic
# CFLAGS = -O3 -march=native -Wall -Wextra -Wpedantic
CFLAGS += $(shell pkg-config --cflags freetype2)

LDFLAGS := -g -lm $(shell pkg-config --libs freetype2)

CXXFLAGS := $(CFLAGS) -nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics

# we use 11 standard for both languages
CXXFLAGS += -std=c++11
CFLAGS += -std=c11

# lida_platform1 uses SDL2
CXXFLAGS += $(shell pkg-config --cflags sdl2)
LDFLAGS += $(shell pkg-config --libs sdl2)

# uncomment if using ASAN
# NOTE: don't forget to do 'make clean' before build if you changed flags!
# NOTE: renderdoc doesn't work with ASAN enabled
# CFLAGS += -fsanitize=address -fno-omit-frame-pointer
# LDFLAGS += -fsanitize=address -fno-omit-frame-pointer -lrt

SHADERS := $(wildcard shaders/*.comp) $(wildcard shaders/*.vert) $(wildcard shaders/*.frag)
SPIRVS := $(addprefix $(DATADIR)/, $(SHADERS:shaders/%=%.spv))

SOURCES := $(wildcard src/*.c)
HEADERS := $(wildcard src/*.h) $(wildcard src/lib/*.h)
OBJS := $(BUILDIR)/$(PLATFORM).o $(BUILDIR)/lida_engine.o $(BUILDIR)/volk.o

.PHONY: all clean

all: directories $(EXECUTABLE) $(SPIRVS)

clean:
	rm -rf $(BUILDIR)

directories:
	@mkdir -p $(BUILDIR)
	@mkdir -p $(DATADIR)

$(EXECUTABLE): $(OBJS)
	$(CC) $(LDFLAGS) $^  -o $@

$(BUILDIR)/$(PLATFORM).o: src/$(PLATFORM).cc $(HEADERS)
	$(CXX) $(CXXFLAGS) src/$(PLATFORM).cc -c -o $@

$(BUILDIR)/lida_engine.o: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) src/lida_engine.c -c -o $@

$(BUILDIR)/volk.o: src/lib/volk.c
	$(CC) $(CFLAGS) $^ -c -o $@

# -gVS is for debugging https://renderdoc.org/docs/how/how_debug_shader.html
define compile_shader =
	$(GLSLANG) $(1) -V -gVS -o $(2)
endef

$(DATADIR)/%.comp.spv: shaders/%.comp
	$(call compile_shader,$^,$@)
$(DATADIR)/%.vert.spv: shaders/%.vert
	$(call compile_shader,$^,$@)
$(DATADIR)/%.frag.spv: shaders/%.frag
	$(call compile_shader,$^,$@)
