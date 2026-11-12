CC      := gcc
OUT_DIR := build
OBJ_DIR := $(OUT_DIR)/obj
TARGET  := $(OUT_DIR)/wproc

CFLAGS  := -std=c11 -Wall -Wextra \
           -ffreestanding -fno-builtin -nostdlib -static \
           -fno-stack-protector -fno-unwind-tables \
           -fno-asynchronous-unwind-tables \
           -O2 -s -Iinclude

DEPFLAGS := -MMD -MP

SRCS := $(wildcard src/*.c \
                   src/core/*.c \
                   src/platform/linux/*.c \
                   src/util/*.c \
                   src/storage/*.c \
                   src/ui/*.c)

OBJS := $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

debug: CFLAGS := -std=c11 -Wall -Wextra -g -O0 \
                 -ffreestanding -fno-builtin -nostdlib -static \
                 -fno-stack-protector -Iinclude
debug: clean all

clean:
	rm -rf $(OUT_DIR)

-include $(DEPS)
