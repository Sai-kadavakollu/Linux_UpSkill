CC = gcc
CFLAGS = -Wall -Wextra -g
TARGET = printHello
all: $(TARGET)
$(TARGET): printHello.c
	$(CC) $(CFLAGS) -o $(TARGET) printHello.c
clean:
	rm -f $(TARGET)
.PHONY: all clean

