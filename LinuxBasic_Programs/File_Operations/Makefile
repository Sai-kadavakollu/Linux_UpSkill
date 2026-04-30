CC= gcc
CFLAGS = -Wall -Wextra -g

TARGET = fileHandling

SRCS = fileHandlingProgram.c fileRead.c fileWrite.c
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

#Link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

#Compile step
%.o: %.c
	$(CC) $(CFLAGS) -c $<

#clean
clean:
	rm -f $(OBJS) $(TARGET)

#Phony targets
.PHONY: all clean
