CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -O2 -Iinclude
LDFLAGS := -lcjson -lsqlite3 -lm

TARGET := parkinome_server
SRC_DIR := src
BUILD_DIR := build
SRCS := $(SRC_DIR)/server.c $(SRC_DIR)/predictor.c $(SRC_DIR)/model.c $(SRC_DIR)/json_io.c $(SRC_DIR)/db.c
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
