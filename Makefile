BUILDIR := bin
EXECUTABLE := $(BUILDIR)/tst

CC ?= gcc
CXX ?= g++

PLATFORM ?= lida_platform1
GLSLANG ?= glslangValidator

CFLAGS = -O0 -g -Wall -Wextra -Wpedantic
CFLAGS += $(shell pkg-config --cflags freetype2)

LDFLAGS := -g $(shell pkg-config --libs freetype2)

CXXFLAGS := $(CFLAGS) -nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics

# we use 11 standard for both languages
CXXFLAGS += -std=c++11
CFLAGS += -std=c11

# lida_platform1 uses SDL2
CXXFLAGS += $(shell pkg-config --cflags sdl2)
LDFLAGS += $(shell pkg-config --libs sdl2)

# uncomment if using ASAN
# NOTE: don't forget to do 'make clean' before build if the changed flags!
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address -fno-omit-frame-pointer -lrt

SHADERS := $(wildcard shaders/*.comp) $(wildcard shaders/*.vert) $(wildcard shaders/*.frag)
SPIRVS := $(addprefix $(BUILDIR)/, $(SHADERS:shaders/%=%.spv))

SOURCES := $(wildcard src/*.c)
HEADERS := $(wildcard src/*.h) $(wildcard src/lib/*.h)
OBJS := $(BUILDIR)/$(PLATFORM).o $(BUILDIR)/lida_engine.o $(BUILDIR)/volk.o

.PHONY: all clean

all: directories $(EXECUTABLE) $(SPIRVS)

clean:
	rm -rf $(BUILDIR)

directories:
	@mkdir -p $(BUILDIR)

$(EXECUTABLE): $(OBJS)
	$(CC) $(LDFLAGS) $^  -o $@

$(BUILDIR)/$(PLATFORM).o: src/$(PLATFORM).cc $(HEADERS)
	$(CXX) $(CXXFLAGS) src/$(PLATFORM).cc -c -o $@

$(BUILDIR)/lida_engine.o: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) src/lida_engine.c -c -o $@

$(BUILDIR)/volk.o: src/lib/volk.c
	$(CC) $(CFLAGS) $^ -c -o $@

define compile_shader =
	$(GLSLANG) $(1) -V -o $(2)
endef

$(BUILDIR)/%.comp.spv: shaders/%.comp
	$(call compile_shader,$^,$@)
$(BUILDIR)/%.vert.spv: shaders/%.vert
	$(call compile_shader,$^,$@)
$(BUILDIR)/%.frag.spv: shaders/%.frag
	$(call compile_shader,$^,$@)
