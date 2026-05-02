NT_API_PATH ?= ./distingNT_API
TOOLCHAIN ?= arm-none-eabi-g++
CXXFLAGS ?= -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall
INCLUDES := -Iinclude -I$(NT_API_PATH)/include -Isrc

CORE_SRCS := src/smearnt_core/SmearCore.cpp \
             src/smearnt_core/AllpassNetwork.cpp \
             src/smearnt_core/AllpassSection.cpp \
             src/smearnt_core/ParamSmoother.cpp
ADAPTER_SRC := src/SmearNTAdapter.cpp

OUT_DIR := build
TARGET := $(OUT_DIR)/smearnt_nt.o

all: $(TARGET)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(TARGET): $(ADAPTER_SRC) $(CORE_SRCS) | $(OUT_DIR)
	$(TOOLCHAIN) $(CXXFLAGS) $(INCLUDES) -r -nostdlib $(ADAPTER_SRC) $(CORE_SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
