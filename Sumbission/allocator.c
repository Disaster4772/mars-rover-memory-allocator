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

// Helper: Find the previous block header by scanning from heap start
// Returns NULL if this is the first block or on error
Header *findPreviousHeader(Header *currentHeader) {
    if ((uintptr_t)currentHeader == (uintptr_t)heapStart) {
        return NULL;  // No previous block
    }
    
    Header *scan = (Header *)heapStart;
    Header *prev = NULL;
    
    // Scan through blocks until we find the one right before current
    while (scan < currentHeader) {
        size_t blockSize = scan->size & ~1;
        if (blockSize == 0) {
            break;  // Hit end marker
        }
        prev = scan;
        scan = (Header *)((uintptr_t)scan + blockSize);
        if (scan >= currentHeader) {
            break;
        }
    }
    
    // Verify we found the right previous block
    if (prev != NULL) {
        size_t prevSize = prev->size & ~1;
        if ((Header *)((uintptr_t)prev + prevSize) == currentHeader) {
            return prev;
        }
    }
    return NULL;
}

// Initialize the allocator over a provided memory block. Returns 0 on success, non-zero on failure.
int mm_init(uint8_t *heap, size_t heap_size){
    if (heap == NULL || heap_size == 0){
        return 1;
    }
    heapStart = heap;
    heapSize = heap_size;

    memcpy(fiveBytePattern, heapStart, 5);

    Header *firstHeader = (Header *)heapStart; // Place the header at the start of the heap
    firstHeader->size = heapSize & ~1;         // Set the size of the block and mark it as free (LSB = 0)

    return 0;
}

// Allocate a block with ALIGN-byte aligned payload. Returns NULL on failure.
void *mm_malloc(size_t size){
     // Handle invalid size
     if (size == 0) {
         return NULL;
     }
     
     Header *currentHeader = (Header *)heapStart;
     // While the current header hasnt reached the end of the heap
     while ((uint8_t *)currentHeader < heapStart + heapSize){
            size_t currentSize = currentHeader->size & ~1;
            
            // If we hit a zero-sized header, we've reached the end of valid blocks
            // This prevents infinite loops when traversing a fully allocated heap
            if (currentSize == 0) {
                break;
            }
            
            // If the current header indicates the block is free and has enough space
            // Check if this free block can fit the requested payload size
            size_t freeBlockSize = currentHeader -> size & ~1;
            size_t freePayloadSpace = freeBlockSize - sizeof(Header) - sizeof(Footer);
            
            int isFree = (currentHeader -> size & 1) == 0;
            
            if (isFree && freePayloadSpace >= size){

                // Save original block size before modifying
                size_t originalBlockSize = freeBlockSize;
                
                // Calculate the aligned distance to next block first, including 40-byte padding
                // The block size must include padding so that traversal with (current + blockSize) lands on the correct next header. If we only stored header+footer+payload, the next header would be placed at an unaligned offset and contain garbage data
                uintptr_t currentOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
                uintptr_t unalignedNextOffset = currentOffset + sizeof(Header) + sizeof(Footer) + size;
                uintptr_t alignedNextOffset = ((unalignedNextOffset + 39) / 40) * 40;
                size_t distanceToNextHeader = alignedNextOffset - currentOffset;
                
                // Store the padded distance (including alignment padding) in the size field.
                // This ensures the next header is always at the correct aligned position.
                currentHeader -> size = distanceToNextHeader;
                // Mark that the block is taken (set LSB to 1)
                currentHeader -> size |= 1;

                // Create footer at the END of the aligned block (just before next header)
                // The footer goes at: currentHeader + distanceToNextHeader - sizeof(Footer)
                Footer *newFooter = (Footer *)((uintptr_t)currentHeader + distanceToNextHeader - sizeof(Footer));
                newFooter -> size = currentHeader -> size;
                
                // Calculate next header location
                uint8_t *nextHeaderPtr = (uint8_t *)((uintptr_t)heapStart + alignedNextOffset);             
                Header *newHeader = (Header *)nextHeaderPtr;

                // Size of remaining free block
                size_t remainingSize = originalBlockSize - distanceToNextHeader;
                
                // Only initialize the next header if there's remaining space
                if (remainingSize >= sizeof(Header)) {
                    newHeader -> size = remainingSize & ~1;
                } else if (remainingSize > 0) {
                    // Too small to fit a header, just mark end
                    newHeader -> size = 0;
                }
                
                return (void *)currentHeader;
            }

            else{
                // If the current block is taken, move to the next header
                // Blocks are already 40-byte aligned, so just add the block size directly
                currentHeader = (Header *)((uintptr_t)currentHeader + (currentHeader->size & ~1));            
            }
     }
    return NULL;
}

// Safely read data from an allocated block at offset bytes into buf
// Returns the number of bytes read, or -1 if corruption or invalid pointer detected
int mm_read(void *ptr, size_t offset, void *buf, size_t len){
    Header *header = (Header *)ptr;
    // Calculate available payload space: block size minus header and footer
    size_t blockSize = header->size & ~1;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

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

// Safely write data into an allocated block at offset bytes from src
// Returns the number of bytes written, or -1 if corruption or invalid pointer detected
int mm_write(void *ptr, size_t offset, const void *src, size_t len){
    
    Header *header = (Header *)ptr;
    // Calculate available payload space: block size minus header and footer
    size_t blockSize = header->size & ~1;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);
    
    // Check block has been allocated to be written to
    if((header -> size & 1 ) == 0){
        printf("DEBUG: Write attempt failed - Block has not been allocated.\n");
        return -1;
    }
    // Check there is enough free space in the block (must fit within requested size)
    else if (offset + len > payloadSize) {
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
    // Check for double-free (maybe check pattern for redundancy?)
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

    // FIX: Coalesce with PREVIOUS block if it exists and is free
    Header *prevHeader = findPreviousHeader(header);
    if (prevHeader != NULL && (prevHeader->size & 1) == 0) {
        // Previous block is free, merge into it
        prevHeader->size += header->size;
        header = prevHeader;  // Update to merged block for next coalescing
    }

    // Coalesce with the next block if it is free
    // The next header is at exactly (current + blockSize) because blockSize already includes padding
    uintptr_t nextHeaderOffset = (uintptr_t)header + (header->size & ~1) - (uintptr_t)heapStart;
    
    // Only attempt coalescing if the next header offset is within heap bounds
    if (nextHeaderOffset < heapSize) {
        Header *nextHeader = (Header *)((uintptr_t)heapStart + nextHeaderOffset);  

        if ((nextHeader -> size & 1) == 0){
            header -> size += nextHeader -> size;
            // We want to overwrite the nextHeader, so don't need to account for it
            uint8_t *blockStart = (uint8_t *)nextHeader;
            size_t blockSize = (nextHeader -> size & ~1);
            for (size_t x = 0; x < blockSize; x++){
                blockStart[x] = fiveBytePattern[x % 5];
            }
        }
    }
    return;
}

// Optional (bonus) functions:

// Resize a previously allocated block to new_size bytes, preserving data. [See additional credit]
void *mm_realloc(void *ptr, size_t new_size);

// Output current heap usage and integrity statistics for debugging (No Credit, helper function).
void mm_heap_stats(void);
