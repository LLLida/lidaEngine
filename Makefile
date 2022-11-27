
BUILDIR := bin
EXECUTABLE := $(BUILDIR)/tst

CFLAGS = -O0 -g -Wall -Wextra -Wpedantic $(addprefix -I, $(INCLUDEDIRS))
CFLAGS += $(shell pkg-config --cflags sdl2)
CFLAGS += $(shell pkg-config --cflags freetype2)

LDFLAGS := $(shell pkg-config --libs sdl2)

CXXFLAGS = $(CFLAGS) -nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics

GLSLANG := glslangValidator
SHADERS := $(wildcard shaders/*.comp) $(wildcard shaders/*.vert) $(wildcard shaders/*.frag)
SPIRVS := $(addprefix $(BUILDIR)/, $(SHADERS:=.spv))

SOURCES := $(wildcard src/*.c)
HEADERS := $(wildcard src/*.h)

.PHONY: all clean

all: directories $(EXECUTABLE) $(SPIRVS)

clean:
	@printf "Removing build directory..."
	@rm -rf $(BUILDIR)
	@printf " done\n"

directories:
	@mkdir -p $(BUILDIR)
	@mkdir -p $(BUILDIR)/shaders

$(EXECUTABLE): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o $@

define compile_shader =
	@$(GLSLANG) $(1) -V -o $(2)
endef

$(BUILDIR)/shaders/%.comp.spv: shaders/%.comp
	$(call compile_shader,$^,$@)
$(BUILDIR)/shaders/%.vert.spv: shaders/%.vert
	$(call compile_shader,$^,$@)
$(BUILDIR)/shaders/%.frag.spv: shaders/%.frag
	$(call compile_shader,$^,$@)
