CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -O2 -Iinclude
LDFLAGS := -lcjson -lsqlite3 -lm

TARGET := parkinome_server
TRAIN_TARGET := parkinome_train
SRC_DIR := src
BUILD_DIR := build
SRCS := $(SRC_DIR)/server.c $(SRC_DIR)/predictor.c $(SRC_DIR)/model.c $(SRC_DIR)/json_io.c $(SRC_DIR)/db.c $(SRC_DIR)/train.c
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TRAIN_SRCS := $(SRC_DIR)/train_main.c $(SRC_DIR)/train.c $(SRC_DIR)/model.c $(SRC_DIR)/json_io.c
TRAIN_OBJS := $(TRAIN_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all run clean

all: $(TARGET) $(TRAIN_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(TRAIN_TARGET): $(TRAIN_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TRAIN_OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

train: $(TRAIN_TARGET)
	./$(TRAIN_TARGET)

clean:
	rm -f $(OBJS) $(TRAIN_OBJS) $(TARGET) $(TRAIN_TARGET)
