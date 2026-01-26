CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -Iinclude

SRC_DIR := src
OUT_DIR := out
BIN     := $(OUT_DIR)/server

SRCS    := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(SRCS:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)/*

