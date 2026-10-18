# Desktop simulator for the remote's UI (macOS/Linux, needs clang/gcc).
# Uses the LVGL sources PlatformIO downloaded: run `pio run` once first.
#   make run      build, run the scripted scenario, write out/*.png

ROOT   := ..
LVGL   := $(ROOT)/.pio/libdeps/cyd/lvgl
BUILD  := build
FLAGS  := -O0 -g -fno-omit-frame-pointer -I stubs -I $(ROOT)/include -I $(ROOT)/src -I $(LVGL) -I . \
          -DLV_CONF_INCLUDE_SIMPLE '-DLV_ASSERT_HANDLER=abort();' -Wno-everything
LV_SRC := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ := $(patsubst $(LVGL)/%.c,$(BUILD)/lvgl/%.o,$(LV_SRC))
APP_SRC := $(ROOT)/src/ui.cpp $(ROOT)/src/ui_control.cpp $(ROOT)/src/ui_system.cpp \
           sim_main.cpp sim_bed.cpp sim_settings.cpp

sim: $(LV_OBJ) $(APP_SRC) $(wildcard $(ROOT)/src/*.h) $(ROOT)/include/config.h
	@echo "  link sim"
	@clang++ -std=c++17 $(FLAGS) $(APP_SRC) $(LV_OBJ) -o $@

$(BUILD)/lvgl/%.o: $(LVGL)/%.c $(ROOT)/include/lv_conf.h
	@mkdir -p $(dir $@)
	@clang -c $(FLAGS) $< -o $@

run: sim
	./sim

clean:
	rm -rf $(BUILD) sim sim.dSYM out

.PHONY: run clean
