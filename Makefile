CC     := gcc
CFLAGS := -Wall -Wextra -O2 -Iinclude -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer

SRC_DIR  := src
TEST_DIR := tests
OUT_DIR  := out

SRC_BIN  := $(OUT_DIR)/server
TEST_BIN := $(OUT_DIR)/test

SRCS       := $(wildcard $(SRC_DIR)/*.c)
SRC_OBJS   := $(SRCS:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)
SERVER_OBJ := $(OUT_DIR)/server.o
LIB_OBJS   := $(filter-out $(SERVER_OBJ),$(SRC_OBJS))

TESTS := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS := $(TESTS:$(TEST_DIR)/%.c=$(OUT_DIR)/%.test.o)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_BIN): $(SRC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OUT_DIR)/%.test.o: $(TEST_DIR)/%.c
	@mkdir -p $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	./$(TEST_BIN)

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)

