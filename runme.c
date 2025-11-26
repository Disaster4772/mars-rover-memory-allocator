#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // For seeding random number generator
#include "allocator.h"  // Include your allocator header file

// Function to parse command-line arguments
void parse_arguments(int argc, char *argv[], int *seed, int *storm, size_t *heap_size) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            *seed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--storm") == 0 && i + 1 < argc) {
            *storm = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            *heap_size = (size_t)atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
}

// Function to initialize the heap with a 5-byte pattern
void initialize_heap_with_pattern(uint8_t *heap, size_t heap_size, uint8_t *pattern) {
    for (size_t i = 0; i < heap_size; i++) {
        heap[i] = pattern[i % 5];
    }
}

int main(int argc, char *argv[]) {
    int seed = 42; // Default seed
    int storm = 0; // Default storm type
    size_t heapSize = 1024; // Default heap size

    // Parse command-line arguments
    parse_arguments(argc, argv, &seed, &storm, &heapSize);

    // Seed the random number generator
    srand(seed);

    // Generate a 5-byte pattern based on the seed
    uint8_t pattern[5];
    for (int i = 0; i < 5; i++) {
        pattern[i] = rand() % 256;
    }

    // Allocate the heap
    uint8_t *heap = malloc(heapSize);
    if (heap == NULL) {
        printf("Failed to allocate heap memory.\n");
        return 1;
    }

    // Initialize the heap with the 5-byte pattern
    initialize_heap_with_pattern(heap, heapSize, pattern);

    // Call mm_init with the allocated heap
    mm_init(heap, heapSize);

    // Handle storm type (if applicable)
    if (storm == 1) {
        printf("Simulating radiation storm...\n");
        // Example: Randomly flip bits in the heap
        for (size_t i = 0; i < heapSize / 10; i++) {
            size_t index = rand() % heapSize;
            heap[index] ^= (1 << (rand() % 8));
        }
    }

    // Allocate a block
    size_t blockSize = 100;  // Example block size
    void *block = mm_malloc(blockSize);
    if (block == NULL) {
        printf("Failed to allocate memory block.\n");
        free(heap);
        return 1;
    }

    // Write data to the block
    const char *data = "Hello, Mars!";
    size_t dataLen = strlen(data) + 1;  // Include null terminator
    if (mm_write(block, 0, data, dataLen) != (int)dataLen) {
        printf("Failed to write data to block.\n");
        free(heap);
        return 1;
    }
    char buffer[100];
    
    if (mm_read(block, 0, buffer, dataLen) != (int)dataLen) {
        printf("Failed to read data from block.\n");
        free(heap);
        return 1;
    }
    printf("Data read from block: %s\n", buffer);

    // Verify the data
    if (strcmp(data, buffer) == 0) {
        printf("Read data matches written data. Test passed!\n");
    } else {
        printf("Read data does not match written data. Test failed.\n");
    }

    // Free the block
    mm_free(block);
    printf("Block freed successfully.\n");

    // Allocate another block to ensure free works
    void *newBlock = mm_malloc(blockSize);
    if (newBlock == NULL) {
        printf("Failed to allocate memory block after freeing.\n");
        free(heap);
        return 1;
    }
    printf("New block allocated successfully after freeing.\n");

    // Write and read data to/from the new block
    const char *newData = "Reallocated Block!";
    size_t newDataLen = strlen(newData) + 1;
    if (mm_write(newBlock, 0, newData, newDataLen) != (int)newDataLen) {
        printf("Failed to write data to new block.\n");
        free(heap);
        return 1;
    }
    if (mm_read(newBlock, 0, buffer, newDataLen) != (int)newDataLen) {
        printf("Failed to read data from new block.\n");
        free(heap);
        return 1;
    }
    printf("Data read from new block: %s\n", buffer);

    // Verify the new data
    if (strcmp(newData, buffer) == 0) {
        printf("Read data matches written data for new block. Test passed!\n");
    } else {
        printf("Read data does not match written data for new block. Test failed.\n");
    }

    // Clean up
    free(heap);
    return 0;
}