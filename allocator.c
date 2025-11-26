#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t *heapStart = NULL;  // Pointer to the start of the heap
static size_t heapSize = 0; // Total size of the heap
static uint8_t fiveBytePattern[5]; 

typedef struct {
    size_t size; // Size of the block (including the metadata)
                 // Final digit indicates status. 0 means free, 1 means taken.
} Header;
typedef struct {
    size_t size; 
} Footer;

// Initialize the allocator over a provided memory block. Returns 0 on success, non-zero on failure.
int mm_init(uint8_t *heap, size_t heap_size){
    if (heap == NULL || heap_size == 0){
        return 1;
    }
    heapStart = heap;
    heapSize = heap_size;

    memcpy(fiveBytePattern, heapStart, 5);

    // For testing
    printf("Five-Byte Pattern: ");
    for (int i = 0; i < 5; i++) {
        printf("0x%02X ", fiveBytePattern[i]); // Print each byte in hexadecimal
    }
    printf("\n");

    Header *firstHeader = (Header *)heapStart; // Place the header at the start of the heap
    firstHeader->size = heapSize & ~1;         // Set the size of the block and mark it as free (LSB = 0)

    return 0;
}

// Allocate a block with ALIGN-byte aligned payload. Returns NULL on failure.
void *mm_malloc(size_t size){
     Header *currentHeader = (Header *)heapStart;
     // While the current header hasnt reached the end of the heap
     while ((uint8_t *)currentHeader < heapStart + heapSize){
            // If the current header indicates the block is free and has enough space
            // & is bitwise AND, first seven bits are irrelevent. 1 AND 0 = 0, so an empty block will equal 0
            // ~ is NOT
            if ((currentHeader -> size & 1) == 0 && (currentHeader -> size & ~1) >= size){

                currentHeader -> size = size + sizeof(Header) + sizeof(Footer);
                // Mark that the next block is taken
                // |= is bitwise OR, first 7 bits will remain as they are, last will be switched to a 1
                currentHeader -> size |= 1;

                // Create footer with same contents as the header. Later this will include a checksum.
                Footer *newFooter = (Footer *)((uintptr_t)currentHeader + sizeof(Header) + size);
                newFooter -> size = currentHeader -> size;
                
                // Calculate next header location
                // Takes current header pointer, adds the size of the metadata and payload
                // Subtract heapStart so we're working in "distance from heapStart"
                // Round up to the next multiple of 40
                // Add back heapStart

                uintptr_t offset = (uintptr_t)currentHeader + sizeof(Header) + sizeof(Footer) + size - (uintptr_t)heapStart;
                offset = ((offset + 39) / 40) * 40;
                uint8_t *nextHeader = (uint8_t *)((uintptr_t)heapStart + offset);             
                Header *newHeader = (Header *)nextHeader;

                // Size of new header is size of the current header - size of the payload and metadata
                newHeader -> size = ((currentHeader->size & ~1) - (size + sizeof(Header) + sizeof(Footer))) & ~1;
                
                return (void *)currentHeader;
            }

            else{
                // If the current block is taken, move to the next header
                uintptr_t offset = (uintptr_t)currentHeader + (currentHeader->size & ~1) - (uintptr_t)heapStart;
                offset = ((offset + 39) / 40) * 40;
                currentHeader = (Header *)((uintptr_t)heapStart + offset);            
            }
     }
    return NULL;
}

// Safely read data from an allocated block at offset bytes into buf.
// Returns the number of bytes read, or -1 if corruption or invalid pointer detected.
int mm_read(void *ptr, size_t offset, void *buf, size_t len){
    Header *header = (Header *)ptr;
    size_t payloadSize = (header->size & ~1) - sizeof(Header) - sizeof(Footer);

    // Check we're not attempting to read an empty block 
    if((header -> size & 1 ) == 0){
        printf("DEBUG: Read attempt failed - Header indicates block has not been allocated. It is empty.\n");
        return -1;
    }
    // Check we're not trying to read beyond the payload
    else if (offset + len > payloadSize) {
        printf("DEBUG: Read attempt failed - Read exceeds payload size.\n");
        return -1;
    }

    uint8_t *readPointer = (uint8_t *)ptr + sizeof(Header) + offset;
    memcpy(buf, readPointer, len);
    return len;
}

// Safely write data into an allocated block at offset bytes from src.
// Returns the number of bytes written, or -1 if corruption or invalid pointer detected.
int mm_write(void *ptr, size_t offset, const void *src, size_t len){
    
    Header *header = (Header *)ptr;
    // Check block has been allocated to be written to
    if((header -> size & 1 ) == 0){
        printf("DEBUG: Write attempt failed - Block has not been allocated.\n");
        return -1;
    }
    // Check there is enough free space in the block
    else if ((header->size & ~1) <= offset + len) {
        printf("DEBUG: Write attempt failed - Block is too small.\n");
        return -1;
    }

    // Write to block
    uint8_t *writePointer = (uint8_t *)ptr + sizeof(Header) + offset;
    memcpy(writePointer, src, len);
    return len;
}

// Free a previously-allocated pointer (ignore NULL).
// Must detect double-free.
void mm_free(void *ptr){
     Header *header = (Header *)ptr;
    // Ignore NULL
    if (ptr == NULL){
        return;
    }
    // Check for double-free (maybe check pattern for redundency?)
    else if ((header -> size & 1) == 0){
        printf("DEBUG: Free attempt failed - Block has not been allocated, it is aready free.\n");
        return;
    }

    // Mark as free
    header -> size &= ~1;

    // Replace all bytes with the fiveBytePattern, making sure not to overwrite the header
    uint8_t *blockStart = (uint8_t *)header + sizeof(Header);
    size_t blockSize = (header->size & ~1) - sizeof(Header);  
    for (size_t x = 0; x < blockSize; x++) {
        blockStart[x] = fiveBytePattern[x % 5];
    }

    // Coalesce with the next block if it is free
    uintptr_t offset = (uintptr_t)header + (header->size & ~1) - (uintptr_t)heapStart;
    offset = ((offset + 39) / 40) * 40;
    Header *nextHeader = (Header *)((uintptr_t)heapStart + offset);  

    if ((nextHeader -> size & 1) == 0){
        header -> size += nextHeader -> size;
        // We want to overwrite the nextHeader, so don't need to account for it
        blockStart = (uint8_t *)nextHeader;
        blockSize = (nextHeader -> size & ~1);
        for (size_t x = 0; x < blockSize; x++){
            blockStart[x] = fiveBytePattern[x % 5];
        }
    }
    return;
}

// Optional (bonus) functions:

// Resize a previously allocated block to new_size bytes, preserving data. [See additional credit]
void *mm_realloc(void *ptr, size_t new_size);

// Output current heap usage and integrity statistics for debugging (No Credit, helper function).
void mm_heap_stats(void);