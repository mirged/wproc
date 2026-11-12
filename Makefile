CC := gcc
OUT_DIR := build

build: src/main.c
	$(CC) src/main.c -o $(OUT_DIR)/wproc