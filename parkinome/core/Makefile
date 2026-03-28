CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS := -lcjson -lm

TARGET := parkinome_server
SRCS := server.c auth.c predictor.c model.c json_io.c
OBJS := $(SRCS:.c=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
