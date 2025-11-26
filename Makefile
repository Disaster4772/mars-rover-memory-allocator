# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Targets
all: runme

runme: runme.c allocator.c
	$(CC) $(CFLAGS) -o runme runme.c allocator.c

clean:
	rm -f runme