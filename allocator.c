#include <stdint.h>
#include <stdio.h>

static uint8_t *heapStart = NULL;  // Pointer to the start of the heap
static size_t heapSize = 0; // Total size of the heap

typedef struct {
    size_t size; // Size of the block (including the header)
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

    // assign metadata here

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
            if ((currentHeader->size & 1) == 0 && (currentHeader->size & ~1) >= size){
                // Mark that the next block is taken
                // |= is bitwise OR, first 7 bits will remain as they are, last will be switched to a 1
                currentHeader->size |= 1;

                // Create footer with same contents as the header. Later this will include a checksum.
                Footer *newFooter = (Footer *)((uintptr_t)currentHeader + sizeof(Header) + size);
                newFooter -> size = currentHeader -> size;
                // Calculate next header location
                // Takes current header pointer, adds the size of the metadata and payload and 39 so we round up to the next 40-aligned position
                // Removes any bits used to represent 1-39, leaving a multiple of 40
                uint8_t *nextHeader = (uint8_t *)(((uintptr_t)currentHeader + sizeof(Header) + sizeof(Footer) + size + 39) & ~39);              
                Header *newHeader = (Header *)nextHeader;

                // Size of new header is size of the current header - size of the payload and metadata
                newHeader -> size = ((currentHeader->size & ~1) - (size + sizeof(Header) + sizeof(Footer))) & ~1;
                
                return (void *)currentHeader;
            }

            else{
                // Next header
                currentHeader = (Header *)((uint8_t *)currentHeader + (((currentHeader->size & ~1) + 39) & ~39));            
            }
     }
    return NULL;
}

// Safely read data from an allocated block at offset bytes into buf.
// Returns the number of bytes read, or -1 if corruption or invalid pointer detected.
int mm_read(void *ptr, size_t offset, void *buf, size_t len);

// Safely write data into an allocated block at offset bytes from src.
// Returns the number of bytes written, or -1 if corruption or invalid pointer detected.
int mm_write(void *ptr, size_t offset, const void *src, size_t len);

// Free a previously-allocated pointer (ignore NULL).
// Must detect double-free.
void mm_free(void *ptr);

// Optional (bonus) functions:

// Resize a previously allocated block to new_size bytes, preserving data. [See additional credit]
void *mm_realloc(void *ptr, size_t new_size);

// Output current heap usage and integrity statistics for debugging (No Credit, helper function).
void mm_heap_stats(void);