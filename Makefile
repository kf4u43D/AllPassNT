TOOLCHAIN ?= arm-none-eabi-g++
NT_NM ?= $(subst g++,nm,$(TOOLCHAIN))
NT_READELF ?= $(subst g++,readelf,$(TOOLCHAIN))
CXXFLAGS ?= -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall -Wextra
INCLUDES := -Iinclude -Isrc

ifeq ($(OS),Windows_NT)
POWERSHELL ?= powershell
else
POWERSHELL ?= pwsh
endif

CORE_SRCS := src/widesmearnt_core/SmearCore.cpp \
             src/widesmearnt_core/AllpassNetwork.cpp \
             src/widesmearnt_core/AllpassSection.cpp \
             src/widesmearnt_core/ParamSmoother.cpp
ADAPTER_SRC := src/WideSmearNTAdapter.cpp
HEADERS := $(wildcard src/*.h src/widesmearnt_core/*.h) include/distingnt/api.h verify.ps1 tools/assert_api.ps1

OUT_DIR := build
TARGET := $(OUT_DIR)/WideSmearNT.o

all: $(TARGET)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(TARGET): $(ADAPTER_SRC) $(CORE_SRCS) $(HEADERS) | $(OUT_DIR)
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./tools/assert_api.ps1 -Path include/distingnt/api.h
	"$(TOOLCHAIN)" $(CXXFLAGS) $(INCLUDES) -r -nostdlib $(ADAPTER_SRC) $(CORE_SRCS) -o $(TARGET).tmp
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./verify.ps1 -ObjectPath $(TARGET).tmp -Nm "$(NT_NM)" -ReadElf "$(NT_READELF)"
	mv -f $(TARGET).tmp $(TARGET)

test:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./test.ps1

sanitize:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./test.ps1 -Sanitize

verify: $(TARGET)
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./verify.ps1 -ObjectPath $(TARGET) -Nm "$(NT_NM)" -ReadElf "$(NT_READELF)"

render:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./tools/render.ps1

check:
	$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File ./check.ps1

clean:
	rm -f $(TARGET) $(OUT_DIR)/widesmearnt_tests.exe

.PHONY: all clean test sanitize verify render check
