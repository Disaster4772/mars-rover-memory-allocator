#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t *heapStart = NULL;  // Pointer to the start of the heap
static size_t heapSize = 0; // Total size of the heap
static uint8_t fiveBytePattern[5]; 

typedef struct {\
    uint32_t magic; // Magic number for validation (0xCAFEBEEF)
    size_t size; // Size of the block (including the metadata)
    uint8_t status; // Status of the block (0 = free, 1 = allocated, 2 = quarantined...)
} Header;

typedef struct {
    size_t size; 
    uint8_t status;
    uint32_t magic; // 0xDEADBEEF
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
        size_t blockSize = scan->size;
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
        size_t prevSize = prev->size;
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
    firstHeader->size = heapSize;              // Set the size of the block
    firstHeader->status = 0;                   // Mark as free and uncorrupted 
    firstHeader->magic = 0xCAFEBEEF;

    Footer *firstFooter = (Footer *)(heapStart + heapSize - sizeof(Footer));
    firstFooter->size = heapSize;
    firstFooter->status = 0;
    firstFooter->magic = 0xDEADBEEF;

    return 0;
}

// Allocate a block with ALIGN-byte aligned payload. Returns NULL on failure.
void *mm_malloc(size_t size){
     // Handle invalid size
     if (size == 0) {
         return NULL;
     }
     
     Header *currentHeader = (Header *)heapStart;
     // While the current header hasn't reached the end of the heap
     while ((uint8_t *)currentHeader < heapStart + heapSize){
            size_t currentSize = currentHeader->size;
            
            // If we hit a zero-sized header, we've reached the end of valid blocks
            // This prevents infinite loops when traversing a fully allocated heap
            if (currentSize == 0) {
                break;
            }
            
            // If the current header indicates the block is free and has enough space
            // Check if this free block can fit the requested payload size
            size_t freeBlockSize = currentHeader -> size;
            size_t freePayloadSpace = freeBlockSize - sizeof(Header) - sizeof(Footer);
            
            int isFree = (currentHeader -> status) == 0;
            
            if (isFree && freePayloadSpace >= size){

                // Save original block size before modifying
                size_t originalBlockSize = freeBlockSize;
                
                // Calculate the aligned distance to next block first, including 40-byte padding
                uintptr_t currentOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
                uintptr_t unalignedNextOffset = currentOffset + sizeof(Header) + sizeof(Footer) + size;
                uintptr_t alignedNextOffset = ((unalignedNextOffset + 39) / 40) * 40;
                size_t distanceToNextHeader = alignedNextOffset - currentOffset;
                
                // Store the padded distance (including alignment padding) in the size field.
                currentHeader -> size = distanceToNextHeader;
                // Mark that the block is taken
                currentHeader -> status = 1;

                // Create footer at the END of the aligned block (just before next header)
                Footer *newFooter = (Footer *)((uintptr_t)currentHeader + distanceToNextHeader - sizeof(Footer));
                newFooter -> size = currentHeader -> size;
                newFooter -> status = 1;
                newFooter -> magic = 0xDEADBEEF;
                
                // Calculate next header location
                uint8_t *nextHeaderPtr = (uint8_t *)((uintptr_t)heapStart + alignedNextOffset);             
                Header *newHeader = (Header *)nextHeaderPtr;

                // Size of remaining free block
                size_t remainingSize = originalBlockSize - distanceToNextHeader;
                
                // Only initialize the next header if there's remaining space
                if (remainingSize >= sizeof(Header)) {
                    newHeader -> size = remainingSize;
                    newHeader -> status = 0;
                    newHeader -> magic = 0xCAFEBEEF;
                } else if (remainingSize > 0) {
                    // Too small to fit a header, just mark end
                    newHeader -> size = 0;
                }
                
                return (void *)currentHeader;
            }

            else{
                // If the current block is taken, move to the next header
                // Blocks are already 40-byte aligned, so just add the block size directly
                currentHeader = (Header *)((uintptr_t)currentHeader + (currentHeader->size));            
            }
     }
    return NULL;
}

// Safely read data from an allocated block at offset bytes into buf
// Returns the number of bytes read, or -1 if corruption or invalid pointer detected
int mm_read(void *ptr, size_t offset, void *buf, size_t len){
    Header *header = (Header *)ptr;
    // Check it magic number for corruption, quarantine if there is.
    // Needs to be done first, as everything else relies on the header being correct
    if((header -> magic) != 0xCAFEBEEF){
        printf("DEBUG: Read attempt failed - Magic number does not match. Block is corrupted and will be quarantined.\n");
        header -> status = 2;
        return -1;
    }

    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer)); // For magic check
    size_t blockSize = header->size;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

    // Check pointer is in heap
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + heapSize) {
        printf("DEBUG: Write attempt failed - Pointer is outside of heap.\n");
        return -1;
    }
    // Check we're not attempting to read an empty block 
    else if((header -> status) == 0){
        printf("DEBUG: Read attempt failed - Header indicates block has not been allocated. It is empty.\n");
        return -1;
    }
    // Check we're not trying to read beyond the payload
    else if (offset + len > payloadSize) {
        printf("DEBUG: Read attempt failed - Read exceeds payload size.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header -> status) == 2){
        printf("DEBUG: Read attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }
    // Need to check header isn't corrupted before we check the footer
    else if((footer -> magic) != 0xDEADBEEF ){
        printf("DEBUG: Read attempt failed - Magic number does not match. Block is corrupted and will be quarantined.\n");
        header -> status = 2;
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
    // Check it magic number for corruption, quarantine if there is
    // Needs to be done first, as everything else relies on the header being correct
    if((header -> magic) != 0xCAFEBEEF){
        printf("DEBUG: Write attempt failed - Magic number does not match. Block is corrupted and will be quarantined.\n");
        header -> status = 2;
        return -1;
    }
    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer));
    // Calculate available payload space: block size minus header and footer
    size_t blockSize = header->size;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

    // Check pointer is in heap
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + heapSize) {
        printf("DEBUG: Write attempt failed - Pointer is outside of heap.\n");
        return -1;
    }
    // Check block has been allocated to be written to
    else if((header -> status) == 0){
        printf("DEBUG: Write attempt failed - Block has not been allocated.\n");
        return -1;
    } 
    // Check there is enough free space in the block (must fit within requested size)pace to be written to
    else if (offset + len > payloadSize) {
        printf("DEBUG: Write attempt failed - Block is too small.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header -> status) == 2){
        printf("DEBUG: Write attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }
    else if((footer -> magic) != 0xDEADBEEF){
        printf("DEBUG: Write attempt failed - Magic number does not match. Block is corrupted and will be quarantined.\n");
        header -> status = 2;
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
    // Ignore NULL
    if (ptr == NULL){
        return;
    }
    
    // Check pointer is within heap bounds
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + heapSize) {
        printf("DEBUG: Free attempt failed - Pointer outside heap bounds.\n");
        return;
    }
    
    Header *header = (Header *)ptr;
    
    // Validate header magic FIRST (before using header->size)
    if (header->magic != 0xCAFEBEEF) {
        printf("DEBUG: Free attempt failed - Header magic corrupted. Block quarantined.\n");
        header->status = 2; // Quarantine instead of freeing
        return;
    }
    
    // Validate header size is reasonable
    size_t blockSize = header->size;
    if (blockSize < sizeof(Header) + sizeof(Footer) || 
        blockSize > heapSize) {
        printf("DEBUG: Free attempt failed - Invalid block size. Block quarantined.\n");
        header->status = 2;
        return;
    }
    
    // Now safely calculate footer position
    Footer *footer = (Footer *)((uint8_t *)header + blockSize - sizeof(Footer));
    
    // Validate footer magic
    if (footer->magic != 0xDEADBEEF) {
        printf("DEBUG: Free attempt failed - Footer magic corrupted. Block quarantined.\n");
        header->status = 2;
        return;
    }
    
    // Check for double-free
    if (header->status == 0) {
        printf("DEBUG: Free attempt failed - Double-free detected.\n");
        return;
    }
    
    // Check block isn't already quarantined
    if (header->status == 2) {
        printf("DEBUG: Free attempt failed - Block already quarantined.\n");
        return;
    }
    
    // Mark as free
    header->status = 0;
    footer->status = 0;
    
    // Replace payload bytes with fiveBytePattern (don't overwrite footer)
    uint8_t *payloadStart = (uint8_t *)header + sizeof(Header);
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);
    for (size_t x = 0; x < payloadSize; x++) {
        payloadStart[x] = fiveBytePattern[x % 5];
    }
    
    // Coalesce with adjacent blocks (previous and next) if they exist and are free
    Header *prevHeader = findPreviousHeader(header);
    
    // Check if previous block is free and merge
    if (prevHeader != NULL) {
        // Validate previous header before using it
        if (prevHeader->magic == 0xCAFEBEEF && prevHeader->status == 0) {
            // Calculate previous block's footer to update it
            Footer *prevFooter = (Footer *)((uint8_t *)prevHeader + prevHeader->size - sizeof(Footer));
            if (prevFooter->magic == 0xDEADBEEF) {
                // Merge: add current block size to previous block
                prevHeader->size += header->size;
                prevFooter->size = prevHeader->size;
                
                // Update header pointer to merged block for next coalescing
                header = prevHeader;
                footer = prevFooter;
                
                // Fill the newly added space with pattern
                uint8_t *newSpaceStart = (uint8_t *)prevHeader + prevHeader->size - header->size + sizeof(Header);
                size_t newSpaceSize = header->size - sizeof(Header) - sizeof(Footer);
                for (size_t x = 0; x < newSpaceSize; x++) {
                    newSpaceStart[x] = fiveBytePattern[x % 5];
                }
            }
        }
    }
    
    // Check if next block is within bounds and free, then merge
    uintptr_t nextHeaderAddr = (uintptr_t)header + header->size;
    if (nextHeaderAddr < (uintptr_t)heapStart + heapSize) {
        Header *nextHeader = (Header *)nextHeaderAddr;
        
        // Validate next header before using it
        if (nextHeader->magic == 0xCAFEBEEF && nextHeader->status == 0) {
            // Calculate next block's footer
            Footer *nextFooter = (Footer *)((uint8_t *)nextHeader + nextHeader->size - sizeof(Footer));
            if (nextFooter->magic == 0xDEADBEEF) {
                // Merge: add next block size to current block
                header->size += nextHeader->size;
                footer->size = header->size;
                
                // Fill the newly added space with pattern
                uint8_t *newSpaceStart = (uint8_t *)nextHeader + sizeof(Header);
                size_t newSpaceSize = nextHeader->size - sizeof(Header) - sizeof(Footer);
                for (size_t x = 0; x < newSpaceSize; x++) {
                    newSpaceStart[x] = fiveBytePattern[x % 5];
                }
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
