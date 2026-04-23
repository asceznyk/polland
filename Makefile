CC := gcc

CFLAGS := -Wall -Wextra -g -O0 \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -rdynamic \
          -DDEBUG_FD \
          -include include/debug.h \
          -Iinclude -I3rdparty/yyjson/src

YYJSON_CFLAGS := -O2

MAKEFLAGS += -j$(shell nproc)

SRC_DIR  := src
TEST_DIR := tests
OUT_DIR  := out

YYJSON_BUILD_DIR := 3rdparty/yyjson/build
YYJSON_LIB := $(YYJSON_BUILD_DIR)/libyyjson.a
YYJSON_OBJ := $(YYJSON_BUILD_DIR)/yyjson.o
YYJSON_SRC := 3rdparty/yyjson/src/yyjson.c

SRC_BIN  := $(OUT_DIR)/rgnx
TEST_BIN := $(OUT_DIR)/test

all: $(SRC_BIN)

SRCS       := $(wildcard $(SRC_DIR)/*.c)
SRC_OBJS   := $(SRCS:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)

SERVER_OBJ := $(OUT_DIR)/server.o
LIB_OBJS   := $(filter-out $(SERVER_OBJ),$(SRC_OBJS))

TESTS      := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS  := $(TESTS:$(TEST_DIR)/%.c=$(OUT_DIR)/%.test.o)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(YYJSON_BUILD_DIR):
	mkdir -p $(YYJSON_BUILD_DIR)

$(OUT_DIR)/%.o: $(SRC_DIR)/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/%.test.o: $(TEST_DIR)/%.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(YYJSON_OBJ): $(YYJSON_SRC) | $(YYJSON_BUILD_DIR)
	$(CC) $(YYJSON_CFLAGS) -c $< -o $@

$(YYJSON_LIB): $(YYJSON_OBJ)
	ar rcs $@ $^

$(SRC_BIN): $(SRC_OBJS) $(YYJSON_LIB)
	$(CC) $(CFLAGS) -o $@ $(SRC_OBJS) -L$(YYJSON_BUILD_DIR) -lyyjson

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS) $(YYJSON_LIB)
	$(CC) $(CFLAGS) -o $@ $(LIB_OBJS) $(TEST_OBJS) -L$(YYJSON_BUILD_DIR) -lyyjson

test: $(TEST_BIN)
	./$(TEST_BIN)

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)

