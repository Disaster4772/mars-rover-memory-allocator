# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Targets
all: runme liballocator.so zip

runme: runme.c allocator.c
	$(CC) $(CFLAGS) -o runme runme.c allocator.c

liballocator.so: allocator.c allocator.h
	$(CC) $(CFLAGS) -shared -fPIC -o liballocator.so allocator.c

test:
	./runme

zip:
	zip allocator.zip allocator.c allocator.h Makefile placeholder.pdf runme.c runme

clean:
	rm -f runme
	rm -f allocator.zip
	rm -f liballocator.so
	