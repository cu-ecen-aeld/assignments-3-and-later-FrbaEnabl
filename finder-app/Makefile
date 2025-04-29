# Makefile for building the "writer" application

# Compiler and target settings
CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = writer

# Source files
SRCS = writer.c
OBJS = $(SRCS:.c=.o)

# Default target: build the application
all: $(TARGET)

# Rule to build the application
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to build .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove output files
clean:
	rm -f $(TARGET) $(OBJS)

# Phony targets
.PHONY: all clean