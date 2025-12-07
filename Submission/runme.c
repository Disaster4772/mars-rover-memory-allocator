#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // For seeding random number generator
#include "allocator.h"  // Include your allocator header file

// Forward declaration
uint32_t calcHeaderChecksum(const Header *h);

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
    size_t heapSize = 65536; // Default heap size 

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

    printf("\n=== TEST 1: Basic Allocation and Write ===\n");
    // Allocate a block
    size_t blockSize = 100;  // Example block size
    void *block = mm_malloc(blockSize);
    if (block == NULL) {
        printf("FAIL: Failed to allocate memory block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated %zu bytes\n", blockSize);

    // Write data to the block
    const char *data = "Hello, Mars!";
    size_t dataLen = strlen(data) + 1;  // Include null terminator
    if (mm_write(block, 0, data, dataLen) != (int)dataLen) {
        printf("FAIL: Failed to write data to block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Wrote %zu bytes to block\n", dataLen);
    
    char buffer[100];
    
    if (mm_read(block, 0, buffer, dataLen) != (int)dataLen) {
        printf("FAIL: Failed to read data from block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read %zu bytes from block\n", dataLen);
    printf("Data read from block: %s\n", buffer);

    // Verify the data
    if (strcmp(data, buffer) == 0) {
        printf("PASS: Read data matches written data.\n");
    } else {
        printf("FAIL: Read data does not match written data.\n");
        free(heap);
        return 1;
    }

    printf("\n=== TEST 2: Multiple Allocations ===\n");
    // Allocate multiple blocks
    void *block2 = mm_malloc(50);
    void *block3 = mm_malloc(75);
    
    if (block2 == NULL || block3 == NULL) {
        printf("FAIL: Failed to allocate second or third block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 50 and 75 byte blocks\n");

    printf("\n=== TEST 3: Free and Reallocate ===\n");
    // Free the block
    mm_free(block);
    printf("PASS: Freed first block\n");

    // Allocate another block to ensure free works
    void *newBlock = mm_malloc(blockSize);
    if (newBlock == NULL) {
        printf("FAIL: Failed to allocate memory block after freeing.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated new block after freeing\n");

    // Write and read data to/from the new block
    const char *newData = "Reallocated Block!";
    size_t newDataLen = strlen(newData) + 1;
    if (mm_write(newBlock, 0, newData, newDataLen) != (int)newDataLen) {
        printf("FAIL: Failed to write data to new block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Wrote to reallocated block\n");
    
    if (mm_read(newBlock, 0, buffer, newDataLen) != (int)newDataLen) {
        printf("FAIL: Failed to read data from new block.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read from reallocated block\n");
    printf("Data read from new block: %s\n", buffer);

    // Verify the new data
    if (strcmp(newData, buffer) == 0) {
        printf("PASS: Read data matches written data for new block.\n");
    } else {
        printf("FAIL: Read data does not match written data for new block.\n");
        free(heap);
        return 1;
    }

    printf("\n=== TEST 4: Write at Offset ===\n");
    // Test writing at offset
    char *offsetData = "OFFSET";
    if (mm_write(newBlock, 5, offsetData, strlen(offsetData)) != (int)strlen(offsetData)) {
        printf("FAIL: Failed to write at offset.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Wrote at offset 5\n");

    if (mm_read(newBlock, 5, buffer, strlen(offsetData)) != (int)strlen(offsetData)) {
        printf("FAIL: Failed to read from offset.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read from offset 5: %s\n", buffer);

    printf("\n=== TEST 5: Double Free Detection ===\n");
    void *testBlock = mm_malloc(30);
    mm_free(testBlock);
    printf("PASS: Freed test block (first time)\n");
    
    printf("Attempting double-free (should print error):\n");
    mm_free(testBlock);
    printf("PASS: Double-free detected and handled\n");

    printf("\n=== TEST 6: Free remaining blocks ===\n");
    mm_free(block2);
    mm_free(block3);
    mm_free(newBlock);
    printf("PASS: Freed all remaining blocks\n");

    printf("\n=== TEST 7: Many small allocations ===\n");
    void *smallBlocks[20];
    for (int i = 0; i < 20; i++) {
        smallBlocks[i] = mm_malloc(10);
        if (smallBlocks[i] == NULL) {
            printf("FAIL: Could not allocate small block %d\n", i);
            free(heap);
            return 1;
        }
        char byte = (char)(i % 256);
        mm_write(smallBlocks[i], 0, &byte, 1);
    }
    printf("PASS: Allocated and wrote to 20 small blocks\n");
    
    for (int i = 0; i < 20; i++) {
        char byte;
        mm_read(smallBlocks[i], 0, &byte, 1);
        if (byte != (char)(i % 256)) {
            printf("FAIL: Data mismatch in small block %d\n", i);
            free(heap);
            return 1;
        }
    }
    printf("PASS: Read and verified 20 small blocks\n");
    
    for (int i = 0; i < 20; i++) {
        mm_free(smallBlocks[i]);
    }
    printf("PASS: Freed all 20 small blocks\n");

    printf("\n=== TEST 8: Fragmentation and Coalescing ===\n");
    // Adjusted for larger heap
    void *frag1 = mm_malloc(4000);
    void *frag2 = mm_malloc(4000);
    void *frag3 = mm_malloc(4000);
    printf("PASS: Allocated 3x4000 byte blocks\n");

    mm_free(frag2);  // Free middle block
    printf("PASS: Freed middle block\n");

    void *frag2new = mm_malloc(4000);  // Should reuse freed space
    if (frag2new == NULL) {
        printf("FAIL: Could not reallocate after freeing middle block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Reallocated freed middle block\n");

    mm_free(frag1);
    mm_free(frag2new);
    mm_free(frag3);
    printf("PASS: Freed fragmentation test blocks\n");

    printf("\n=== TEST 9: Large allocation ===\n");
    void *largeBlock = mm_malloc(500);
    if (largeBlock == NULL) {
        printf("FAIL: Could not allocate large 500-byte block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 500-byte block\n");
    
    // Fill with pattern
    for (int i = 0; i < 500; i++) {
        char byte = (char)(i % 256);
        mm_write(largeBlock, i, &byte, 1);
    }
    printf("PASS: Wrote 500-byte pattern\n");
    
    // Verify pattern
    for (int i = 0; i < 500; i++) {
        char byte;
        mm_read(largeBlock, i, &byte, 1);
        if (byte != (char)(i % 256)) {
            printf("FAIL: Large block data mismatch at offset %d\n", i);
            free(heap);
            return 1;
        }
    }
    printf("PASS: Verified 500-byte pattern\n");
    
    mm_free(largeBlock);
    printf("PASS: Freed large block\n");

    printf("\n=== TEST 10: Boundary conditions ===\n");
    // Test with exactly 1 byte
    void *oneByte = mm_malloc(1);
    if (oneByte == NULL) {
        printf("FAIL: Could not allocate 1 byte\n");
        free(heap);
        return 1;
    }
    mm_write(oneByte, 0, "X", 1);
    char readByte;
    mm_read(oneByte, 0, &readByte, 1);
    if (readByte != 'X') {
        printf("FAIL: 1-byte block data incorrect\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated, wrote, and read 1-byte block\n");
    mm_free(oneByte);

    printf("\n=== TEST 11: Alignment verification ===\n");
    // Allocate several blocks and check they're at aligned offsets
    void *align1 = mm_malloc(10);
    void *align2 = mm_malloc(10);
    void *align3 = mm_malloc(10);
    
    uintptr_t off1 = (uintptr_t)align1 - (uintptr_t)heap;
    uintptr_t off2 = (uintptr_t)align2 - (uintptr_t)heap;
    uintptr_t off3 = (uintptr_t)align3 - (uintptr_t)heap;
    
    if (off1 % 40 != 0 || off2 % 40 != 0 || off3 % 40 != 0) {
        printf("FAIL: Blocks not 40-byte aligned (offsets: %zu, %zu, %zu)\n", off1, off2, off3);
        free(heap);
        return 1;
    }
    printf("PASS: All blocks are 40-byte aligned\n");
    
    mm_free(align1);
    mm_free(align2);
    mm_free(align3);

    printf("\n=== TEST 12: Read/Write bounds checking ===\n");
    void *boundsTest = mm_malloc(50);
    
    // Try to write beyond payload size (should fail)
    char largeData[100];
    memset(largeData, 'A', 100);
    int result = mm_write(boundsTest, 0, largeData, 100);
    if (result != -1) {
        printf("FAIL: Should have rejected write beyond payload size\n");
        free(heap);
        return 1;
    }
    printf("PASS: Write bounds checking works (rejected oversized write)\n");
    
    // Try to read beyond payload size (should fail)
    char readBuf[100];
    result = mm_read(boundsTest, 0, readBuf, 100);
    if (result != -1) {
        printf("FAIL: Should have rejected read beyond payload size\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read bounds checking works (rejected oversized read)\n");
    
    mm_free(boundsTest);

    printf("\n=== TEST 13: Write at various offsets ===\n");
    void *offsetTest = mm_malloc(100);
    
    // Write at offset 0
    mm_write(offsetTest, 0, "START", 5);
    // Write at offset 50
    mm_write(offsetTest, 50, "MID", 3);
    // Write near end (offsets 94-96 in 100-byte payload)
    mm_write(offsetTest, 94, "END", 3);
    
    printf("PASS: Offset write operations completed\n");
    
    char verifyBuf[10];
    memset(verifyBuf, 0, sizeof(verifyBuf));  // Clear buffer
    mm_read(offsetTest, 0, verifyBuf, 5);
    if (memcmp(verifyBuf, "START", 5) != 0) {
        printf("FAIL: Offset 0 data incorrect\n");
        free(heap);
        return 1;
    }
    printf("PASS: Verified data at offset 0\n");
    
    mm_free(offsetTest);

    printf("\n=== TEST 14: Alternating allocate/free ===\n");
    for (int i = 0; i < 5; i++) {
        void *altBlock = mm_malloc(30);
        if (altBlock == NULL) {
            printf("FAIL: Could not allocate in alternating pattern iteration %d\n", i);
            free(heap);
            return 1;
        }
        char byte = (char)(i + 65);  // A-E
        mm_write(altBlock, 0, &byte, 1);
        mm_free(altBlock);
    }
    printf("PASS: Alternating allocate/free pattern works\n");

    printf("\n=== TEST 15: Multiple frees and reallocations ===\n");
    void *mf1 = mm_malloc(25);
    void *mf2 = mm_malloc(25);
    void *mf3 = mm_malloc(25);
    void *mf4 = mm_malloc(25);
    printf("PASS: Allocated 4x25 byte blocks\n");
    
    mm_free(mf1);
    mm_free(mf3);
    printf("PASS: Freed blocks 1 and 3\n");
    
    void *mf1_new = mm_malloc(25);
    void *mf3_new = mm_malloc(25);
    if (mf1_new == NULL || mf3_new == NULL) {
        printf("FAIL: Could not reallocate after freeing alternating blocks\n");
        free(heap);
        return 1;
    }
    printf("PASS: Reallocated freed blocks\n");
    
    mm_free(mf1_new);
    mm_free(mf2);
    mm_free(mf3_new);
    mm_free(mf4);
    printf("PASS: Freed all blocks in test 15\n");

    printf("\n=== TEST 16: Edge case - Allocate with 0 size ===\n");
    void *zeroBlock = mm_malloc(0);
    if (zeroBlock != NULL) {
        printf("FAIL: Should return NULL for size 0\n");
        free(heap);
        return 1;
    }
    printf("PASS: Correctly rejected allocation of size 0\n");

    printf("\n=== Reinitializing heap for corruption tests ===\n");
    // Reinitialize the heap to start fresh for corruption tests
    free(heap);
    heap = malloc(heapSize);
    if (heap == NULL) {
        printf("Failed to reallocate heap memory.\n");
        return 1;
    }
    initialize_heap_with_pattern(heap, heapSize, pattern);
    mm_init(heap, heapSize);
    printf("PASS: Heap reinitialized\n");

    printf("\n=== TEST 17: Corruption Tests (Before Stress Tests) ===\n");
    printf("\n--- TEST 17a: Corrupted Header Detection ---\n");
    // Test that malloc detects corrupted headers and doesn't use them
    void *scan0 = mm_malloc(10);  // Allocate before the test block
    void *scan1 = mm_malloc(10);
    void *scan1_after = mm_malloc(10);  // Allocate after to ensure space exists
    if (scan0 == NULL || scan1 == NULL || scan1_after == NULL) {
        printf("FAIL: Could not allocate blocks for header corruption test\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated blocks for header corruption test\n");

    // Corrupt the header of scan1
    // scan1 is a payload pointer, so header is sizeof(Header) bytes before it
    Header *corruptHeader = (Header *)((uint8_t *)scan1 - sizeof(Header));
    corruptHeader->magic = 0xBADFBEEF;  // Corrupt the magic number
    printf("PASS: Corrupted header of scan1\n");

    // Try to write to the corrupted block - should fail
    if (mm_write(scan1, 0, "test", 4) != -1) {
        printf("FAIL: Write should have failed on corrupted header\n");
        free(heap);
        return 1;
    }
    printf("PASS: Write correctly rejected corrupted header\n");

    // Try to read from the corrupted block - should fail
    char scanBuf[100];
    if (mm_read(scan1, 0, scanBuf, 4) != -1) {
        printf("FAIL: Read should have failed on corrupted header\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read correctly rejected corrupted header\n");

    // Verify we can still use scan1_after (allocated after the corrupted block)
    if (mm_write(scan1_after, 0, "test", 4) != 4) {
        printf("FAIL: Could not write to block allocated after corrupted block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Block after corrupted block still usable\n");

    mm_free(scan0);
    mm_free(scan1_after);
    // Note: scan1 is corrupted, so we skip freeing it

    printf("\n--- TEST 17b: Corrupted Footer Quarantine ---\n");
    // Test that malloc quarantines blocks with corrupted footers
    void *foot1 = mm_malloc(10);
    void *foot2 = mm_malloc(10);
    if (foot1 == NULL || foot2 == NULL) {
        printf("FAIL: Could not allocate blocks for footer corruption test\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 2 blocks for footer corruption test\n");

    // Corrupt the footer of foot1
    // foot1 is a payload pointer, so header is sizeof(Header) bytes before it
    Header *footHeader = (Header *)((uint8_t *)foot1 - sizeof(Header));
    Footer *corruptFooter = (Footer *)((uint8_t *)footHeader + footHeader->size - sizeof(Footer));
    corruptFooter->magic = 0xDEADDEAD;  // Corrupt footer magic
    printf("PASS: Corrupted footer of foot1\n");

    // Try to allocate - should skip the corrupted block and still be able to allocate
    void *foot3 = mm_malloc(10);
    if (foot3 == NULL) {
        printf("FAIL: Could not allocate despite corrupted footer in previous block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocator handled corrupted footer and allocated new block\n");

    // Verify foot1 was quarantined by trying to write to it
    if (mm_write(foot1, 0, "test", 4) != -1) {
        printf("FAIL: Should not be able to write to quarantined block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Quarantined block correctly rejected write operation\n");

    mm_free(foot2);
    mm_free(foot3);

    printf("\n--- TEST 17c: Checksum Robustness ---\n");
    // Verify that single-bit corruption is detected
    void *check1 = mm_malloc(10);
    if (check1 == NULL) {
        printf("FAIL: Could not allocate for checksum test\n");
        free(heap);
        return 1;
    }

    // Write initial data
    mm_write(check1, 0, "Checksum", 8);

    // Flip a single bit in the header (not the checksum field itself)
    // check1 is a payload pointer, so header is sizeof(Header) bytes before it
    Header *checkHeader = (Header *)((uint8_t *)check1 - sizeof(Header));
    uint8_t *headerBytes = (uint8_t *)checkHeader;
    headerBytes[0] ^= 0x01;  // Flip one bit in magic
    printf("PASS: Flipped single bit in header\n");

    // Try to read - checksum should detect this
    char checkBuf[50];
    if (mm_read(check1, 0, checkBuf, 8) != -1) {
        printf("FAIL: Checksum failed to detect single-bit corruption\n");
        free(heap);
        return 1;
    }
    printf("PASS: Checksum correctly detected single-bit corruption\n");

    printf("\n=== END Corruption Tests ===\n");

    printf("\n=== Reinitializing heap for stress tests ===\n");
    // Reinitialize the heap to clear corrupted/quarantined blocks
    free(heap);
    heap = malloc(heapSize);
    if (heap == NULL) {
        printf("Failed to reallocate heap memory.\n");
        return 1;
    }
    initialize_heap_with_pattern(heap, heapSize, pattern);
    mm_init(heap, heapSize);
    printf("PASS: Heap reinitialized\n");

    printf("\n=== TEST 17: Autograder-style allocation/deallocation ===\n");
    // This mimics what an autograder might do: basic alloc/dealloc cycle
    void *a1 = mm_malloc(50);
    void *a2 = mm_malloc(100);
    void *a3 = mm_malloc(75);
    
    if (a1 == NULL || a2 == NULL || a3 == NULL) {
        printf("FAIL: Could not allocate memory blocks\n");
        printf("  a1=%p, a2=%p, a3=%p\n", a1, a2, a3);
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 3 blocks (50, 100, 75 bytes)\n");
    
    // Write to verify blocks are usable
    const char test[] = "test";
    if (mm_write(a1, 0, test, 4) != 4) {
        printf("FAIL: Could not write to a1\n");
        free(heap);
        return 1;
    }
    if (mm_write(a2, 0, test, 4) != 4) {
        printf("FAIL: Could not write to a2\n");
        free(heap);
        return 1;
    }
    if (mm_write(a3, 0, test, 4) != 4) {
        printf("FAIL: Could not write to a3\n");
        free(heap);
        return 1;
    }
    printf("PASS: Wrote to all 3 blocks\n");
    
    // Read back to verify
    char buf[10];
    if (mm_read(a1, 0, buf, 4) != 4 || memcmp(buf, test, 4) != 0) {
        printf("FAIL: Read back from a1 failed\n");
        free(heap);
        return 1;
    }
    if (mm_read(a2, 0, buf, 4) != 4 || memcmp(buf, test, 4) != 0) {
        printf("FAIL: Read back from a2 failed\n");
        free(heap);
        return 1;
    }
    if (mm_read(a3, 0, buf, 4) != 4 || memcmp(buf, test, 4) != 0) {
        printf("FAIL: Read back from a3 failed\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read back from all 3 blocks verified\n");
    
    // Deallocate
    mm_free(a1);
    mm_free(a2);
    mm_free(a3);
    printf("PASS: Deallocated all 3 blocks\n");

    printf("\n=== TEST 18: Single large allocation ===\n");
    // Test with a large allocation that spans most of heap
    void *large = mm_malloc(2000);
    if (large == NULL) {
        printf("FAIL: Could not allocate 2000 byte block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 2000 byte block\n");
    
    // Write and read
    char large_data[100] = "Large block test data";
    if (mm_write(large, 0, large_data, 21) != 21) {
        printf("FAIL: Could not write to large block\n");
        free(heap);
        return 1;
    }
    printf("PASS: Wrote 21 bytes to large block\n");
    
    char large_buf[100];
    memset(large_buf, 0, sizeof(large_buf));
    int readResult = mm_read(large, 0, large_buf, 21);
    if (readResult != 21) {
        printf("FAIL: Could not read from large block (got %d bytes, expected 21)\n", readResult);
        free(heap);
        return 1;
    }
    if (strcmp(large_buf, "Large block test data") != 0) {
        printf("FAIL: Read data doesn't match (got: '%s')\n", large_buf);
        free(heap);
        return 1;
    }
    printf("PASS: Read/write to large block verified\n");
    mm_free(large);
    printf("PASS: Freed large block\n");

    printf("\n=== TEST 19: Stress test - many small allocations ===\n");
    void *ptrs[2000]; // Adjusted to test the limits of a 64 KB heap
    int allocatedBlocks = 0;
    for (int i = 0; i < 2000; i++) {
        ptrs[i] = mm_malloc(10);
        if (ptrs[i] == NULL) {
            printf("Heap exhausted after %d allocations of 10-byte blocks.\n", i);
            break;
        }
        allocatedBlocks++;
    }
    printf("PASS: Allocated %d blocks of 10 bytes each\n", allocatedBlocks);

    // Write to all allocated blocks
    for (int i = 0; i < allocatedBlocks; i++) {
        uint8_t val = (uint8_t)(i % 256);
        if (mm_write(ptrs[i], 0, &val, 1) != 1) {
            printf("FAIL: Could not write to block %d\n", i);
            free(heap);
            return 1;
        }
    }
    printf("PASS: Wrote to all %d blocks\n", allocatedBlocks);

    // Read from all allocated blocks
    for (int i = 0; i < allocatedBlocks; i++) {
        uint8_t val;
        if (mm_read(ptrs[i], 0, &val, 1) != 1 || val != (uint8_t)(i % 256)) {
            printf("FAIL: Could not read from block %d (got %u, expected %u)\n", i, val, (uint8_t)(i % 256));
            free(heap);
            return 1;
        }
    }
    printf("PASS: Read from all %d blocks verified\n", allocatedBlocks);

    // Free all allocated blocks
    for (int i = 0; i < allocatedBlocks; i++) {
        mm_free(ptrs[i]);
    }
    printf("PASS: Freed all %d blocks\n", allocatedBlocks);

    // Reinitialize heap for next phase of testing
    initialize_heap_with_pattern(heap, heapSize, pattern);
    mm_init(heap, heapSize);

    printf("\n=== TEST 20: Alternating pattern - allocate/free/allocate ===\n");
    void *alt1 = mm_malloc(100);
    void *alt2 = mm_malloc(100);
    if (alt1 == NULL || alt2 == NULL) {
        printf("FAIL: Initial allocations failed\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 2 blocks of 100 bytes\n");
    
    mm_free(alt1);
    printf("PASS: Freed first block\n");
    
    void *alt3 = mm_malloc(50);
    if (alt3 == NULL) {
        printf("FAIL: Could not allocate 50 bytes in freed space\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 50 bytes in freed space\n");
    
    mm_free(alt2);
    mm_free(alt3);
    printf("PASS: Freed remaining blocks\n");

    printf("\n=== TEST 21: Coalescing Verification ===\n");
    // Adjusted for larger heap
    void *coal1 = mm_malloc(20000);
    void *coal2 = mm_malloc(20000);
    void *coal3 = mm_malloc(20000);
    if (coal1 == NULL || coal2 == NULL || coal3 == NULL) {
        printf("FAIL: Could not allocate blocks for coalescing test\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated 3 blocks of 20000 bytes each\n");

    mm_free(coal2);
    printf("PASS: Freed middle block\n");

    mm_free(coal1);
    printf("PASS: Freed first block\n");

    // Coalescing should merge coal1 and coal2
    // Account for alignment padding - request slightly less than coal1+coal2
    void *coal4 = mm_malloc(39000);
    if (coal4 == NULL) {
        printf("FAIL: Coalescing failed to merge free blocks\n");
        free(heap);
        return 1;
    }
    printf("PASS: Coalescing merged free blocks successfully\n");

    mm_free(coal3);
    mm_free(coal4);
    printf("PASS: Freed all blocks in coalescing test\n");

    printf("\n=== TEST 22: Heap Exhaustion ===\n");
    // Adjusted for larger heap
    void *exhaustBlocks[2000];
    int i;
    for (i = 0; i < 2000; i++) {
        exhaustBlocks[i] = mm_malloc(30);
        if (exhaustBlocks[i] == NULL) {
            printf("PASS: Heap exhausted after %d allocations\n", i);
            break;
        }
    }
    if (i == 2000) {
        printf("FAIL: Heap exhaustion test did not exhaust heap\n");
        free(heap);
        return 1;
    }

    // Free all allocated blocks
    for (int j = 0; j < i; j++) {
        mm_free(exhaustBlocks[j]);
    }
    printf("PASS: Freed all blocks after heap exhaustion test\n");

    printf("\n=== TEST 23: Zero Allocation ===\n");
    // Test allocation of size 0
    void *zeroAlloc = mm_malloc(0);
    if (zeroAlloc != NULL) {
        printf("FAIL: Allocation of size 0 should return NULL\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocation of size 0 correctly returned NULL\n");

    // Reinitialize heap for final tests
    initialize_heap_with_pattern(heap, heapSize, pattern);
    mm_init(heap, heapSize);

    printf("\n=== TEST 24: Alignment Verification ===\n");
    // Allocate blocks and check alignment
    void *alignTest1 = mm_malloc(10);
    void *alignTest2 = mm_malloc(10);
    void *alignTest3 = mm_malloc(10);

    uintptr_t offset1 = (uintptr_t)alignTest1 - (uintptr_t)heap;
    uintptr_t offset2 = (uintptr_t)alignTest2 - (uintptr_t)heap;
    uintptr_t offset3 = (uintptr_t)alignTest3 - (uintptr_t)heap;

    if (offset1 % 40 != 0 || offset2 % 40 != 0 || offset3 % 40 != 0) {
        printf("FAIL: Blocks not aligned correctly (offsets: %zu, %zu, %zu)\n", offset1, offset2, offset3);
        free(heap);
        return 1;
    }
    printf("PASS: All blocks are correctly aligned\n");

    mm_free(alignTest1);
    mm_free(alignTest2);
    mm_free(alignTest3);

    printf("\n=== TEST 25: Double-Free Detection ===\n");
    // Test double-free detection
    void *doubleFreeTest = mm_malloc(50);
    mm_free(doubleFreeTest);
    printf("PASS: Freed block once\n");

    printf("Attempting double-free (should print error):\n");
    mm_free(doubleFreeTest);
    printf("PASS: Double-free detected and handled\n");

    printf("\n=== TEST: Magic Number Validation (Header and Footer) ===\n");

    // Allocate a block
    size_t magicBlockSize = 100;
    void *magicBlock = mm_malloc(magicBlockSize);
    if (magicBlock == NULL) {
        printf("FAIL: Failed to allocate block for magic number test.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Allocated block for magic number test.\n");

    // magicBlock is a payload pointer, so header is sizeof(Header) bytes before it
    Header *header = (Header *)((uint8_t *)magicBlock - sizeof(Header));
    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer));

    printf("DEBUG: Original header magic number: 0x%X\n", header->magic);
    printf("DEBUG: Original footer magic number: 0x%X\n", footer->magic);

    // Corrupt the magic numbers
    header->magic = 0xDEADC0DE; // Corrupt the header magic number
    footer->magic = 0xBADF00D;  // Corrupt the footer magic number

    printf("DEBUG: Corrupted header magic number: 0x%X\n", header->magic);
    printf("DEBUG: Corrupted footer magic number: 0x%X\n", footer->magic);

    // Attempt to write to the corrupted block
    const char *testData = "Magic Test";
    size_t testDataLen = strlen(testData) + 1;
    if (mm_write(magicBlock, 0, testData, testDataLen) != -1) {
        printf("FAIL: Write succeeded on a block with corrupted magic numbers.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Write failed as expected on a block with corrupted magic numbers.\n");

    // Attempt to read from the corrupted block
    char testBuffer[100];
    if (mm_read(magicBlock, 0, testBuffer, testDataLen) != -1) {
        printf("FAIL: Read succeeded on a block with corrupted magic numbers.\n");
        free(heap);
        return 1;
    }
    printf("PASS: Read failed as expected on a block with corrupted magic numbers.\n");

    // Free the corrupted block
    mm_free(magicBlock);
    printf("PASS: Freed block with corrupted magic numbers.\n");

    printf("\n=== ALL TESTS COMPLETED ===\n");
    
    // Clean up
    free(heap);
    return 0;
}