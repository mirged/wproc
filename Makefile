CC      := gcc

# GCC can turn our own memset/memcpy loops into calls to memset/memcpy,
# which is a bold move when we ARE memset. Clang has no such flag;
# -fno-builtin already stops its loop-idiom pass from doing the same.
NO_LOOP_IDIOMS := $(if $(findstring clang,$(shell $(CC) --version 2>/dev/null)),,-fno-tree-loop-distribute-patterns)
OUT_DIR := build
OBJ_DIR := $(OUT_DIR)/obj
TARGET  := $(OUT_DIR)/wproc

CFLAGS  := -std=c11 -Wall -Wextra \
           -ffreestanding -fno-builtin -nostdlib -static \
           -fno-stack-protector $(NO_LOOP_IDIOMS) -fno-unwind-tables \
           -fno-asynchronous-unwind-tables \
           -O2 -Iinclude

LDFLAGS := -s

DEPFLAGS := -MMD -MP

SRCS := $(wildcard src/*.c \
                   src/core/*.c \
                   src/platform/linux/*.c \
                   src/util/*.c \
                   src/storage/*.c \
                   src/ui/*.c)

OBJS := $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean debug test

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

debug: LDFLAGS :=
debug: CFLAGS := -std=c11 -Wall -Wextra -g -O0 \
                 -ffreestanding -fno-builtin -nostdlib -static \
                 -fno-stack-protector $(NO_LOOP_IDIOMS) -Iinclude
debug: clean all

test:
	$(MAKE) -C tests

clean:
	rm -rf $(OUT_DIR)

-include $(DEPS)
