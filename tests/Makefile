CC     := gcc
ROOT   := ..
BUILD  := $(ROOT)/build/tests

CFLAGS := -std=c11 -Wall -Wextra -g -O1 \
          -ffreestanding -fno-builtin -nostdlib -static \
          -fno-stack-protector -fno-tree-loop-distribute-patterns \
          -I$(ROOT)/include -I.

# Everything except main.c and the terminal-owning UI code.
LIB_SRCS := $(wildcard $(ROOT)/src/util/*.c \
                       $(ROOT)/src/platform/linux/*.c \
                       $(ROOT)/src/core/*.c \
                       $(ROOT)/src/storage/*.c) \
            $(ROOT)/src/ui/input.c

TESTS := $(wildcard util/test_*.c platform/test_*.c storage/test_*.c ui/test_*.c)
BINS  := $(patsubst %.c,$(BUILD)/%,$(TESTS))

.PHONY: all run clean

all: run

$(BUILD)/%: %.c $(LIB_SRCS) test.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(LIB_SRCS) -o $@

run: $(BINS)
	@fail=0; for t in $(BINS); do $$t || fail=1; done; exit $$fail

clean:
	rm -rf $(BUILD)
