#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

// Not "allocator metadata", so can be stored outside the heap
static uint8_t *heapStart = NULL;  // Pointer to the start of the heap
static size_t heapSize = 0; // Total size of the heap
static uint8_t fiveBytePattern[5]; 

typedef struct {
    uint32_t magic; // Magic number for validation (0xCAFEBEEF)
    size_t size; // Size of the block (including the metadata)
    uint8_t status; // Status of the block (0 = free, 1 = allocated, 2 = quarantined...)
    uint32_t hChecksum; // Header checksum of all above metadata
} Header;

typedef struct {
    size_t size; 
    uint8_t status;
    uint32_t magic; // 0xDEADBEEF
    uint32_t pChecksum; // Checksum for verifying the payload
    uint32_t fChecksum;
} Footer;

// Adler-32 checksum for Header (excluding hChecksum itself)
uint32_t calcHeaderChecksum(const Header *h) {
    const uint8_t *data = (const uint8_t *)h;
    size_t len = offsetof(Header, hChecksum); // Hash up to but not including hChecksum
    
    uint32_t a = 1, b = 0;
    const uint32_t modAdler = 65521;
    
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % modAdler;
        b = (b + a) % modAdler;
    }
    
    return (b << 16) | a;
}

// Adler-32 checksum for Footer (excluding fChecksum and pChecksum)
uint32_t calcFooterChecksum(const Footer *f) {
    const uint8_t *data = (const uint8_t *)f;
    size_t len = offsetof(Footer, fChecksum); // Hash up to but not including fChecksum
    
    uint32_t a = 1, b = 0;
    const uint32_t modAdler = 65521;
    
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % modAdler;
        b = (b + a) % modAdler;
    }
    
    return (b << 16) | a;
}

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

void clearSpace(uint8_t *start, size_t size) {
    // Replace bits with five byte pattern, relative to start of heap
    uintptr_t offset = (uintptr_t)start - (uintptr_t)heapStart;
    size_t patternStart = offset % 5;
    
    for (size_t x = 0; x < size; x++) {
        start[x] = fiveBytePattern[(patternStart + x) % 5];
    }
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
    firstHeader->hChecksum = calcHeaderChecksum(firstHeader);

    // Place footer at the end of the heap
    Footer *firstFooter = (Footer *)(heapStart + heapSize - sizeof(Footer));
    firstFooter->size = heapSize;
    firstFooter->status = 0;
    firstFooter->magic = 0xDEADBEEF;
    firstFooter->fChecksum = calcFooterChecksum(firstFooter);
    // firstFooter->pChecksum = 0;

    return 0;
}

// Allocate a block with ALIGN-byte aligned payload. Returns NULL on failure.
void *mm_malloc(size_t size){
     // Handle invalid size
     if (size == 0) {
        printf("ERROR: ALLOCATION FAILED. Attempting to allocate for a 0 size payload.\n");
        return NULL;
     }
     
     Header *currentHeader = (Header *)heapStart;
     uint8_t *heapEnd = heapStart + heapSize;
     // While the current header hasn't reached the end of the heap
     while ((uint8_t *)currentHeader < heapEnd){

        // Check for corruption in the header
        // If the header is corrupted, then skip to the next 40 aligned location. We cannot trust anything in the header so just skip ahead by 40.
        if (currentHeader->magic != 0xCAFEBEEF || currentHeader->hChecksum != calcHeaderChecksum(currentHeader)) {
            size_t skipPos = (uintptr_t)currentHeader + 40;
            skipPos = (skipPos / 40) * 40;
            // Check we haven't reached the end of the heap
            if (skipPos > (uintptr_t)(heapStart + heapSize)) {
                printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                return NULL;
            }
            // Unfortunately, we have no way to verify whether we are on a header or not, so quarantining it could result in corrupted a payload. We just move on.
            currentHeader = (Header *)skipPos;
            continue;
        }
        // At this point, we can trust the header metadata
        Footer *currentFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
        if (currentHeader->status == 2){
            currentFooter->status = 2;
            // We've changed status, so we need to re-calculate the checksum
            currentFooter->fChecksum = calcFooterChecksum(currentFooter);
            // Use trusted header size to skip to next block
            currentHeader = (Header *)((uintptr_t)currentHeader + currentHeader->size);
            if ((uintptr_t)currentHeader > (uintptr_t)(heapStart + heapSize)){
                printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                return NULL;
            }
            continue;      
        }
        // Move onto checking the footer.
        else if (currentFooter->magic != 0xDEADBEEF || currentFooter->fChecksum != calcFooterChecksum(currentFooter) || currentFooter->status == 2) {
            // Header is valid, so we know this is a real header. Quarantine it since footer/payload is corrupted.
            // We don't need to separate a quarantine from a corruption because we don't do anything different
            currentHeader->status = 2;
            currentHeader->hChecksum = calcHeaderChecksum(currentHeader);
            currentHeader = (Header *)((uintptr_t)currentHeader + currentHeader->size);
            if ((uintptr_t)currentHeader > (uintptr_t)(heapStart + heapSize)){
                printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                return NULL;
            }
            continue;
        }

            // If the current header indicates the block is free and has enough space
            // Check if this free block can fit the requested payload size
            size_t freeBlockSize = currentHeader->size;
            if (freeBlockSize < sizeof(Header) + sizeof(Footer)) {
                // Corrupted small block size: quarantine and stop
                currentHeader->status = 2;
                return NULL;
            }
            size_t freePayloadSpace = freeBlockSize - sizeof(Header) - sizeof(Footer);
            int isFree = (currentHeader->status) == 0;

            if (isFree && freePayloadSpace >= size){

                // Save original block size before modifying
                size_t originalBlockSize = freeBlockSize;

                // Calculate the aligned distance to next block first, including 40-byte padding
                uintptr_t currentOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
                uintptr_t unalignedNextOffset = currentOffset + sizeof(Header) + sizeof(Footer) + size;
                uintptr_t alignedNextOffset = ((unalignedNextOffset + 39) / 40) * 40;
                size_t distanceToNextHeader = alignedNextOffset - currentOffset;

                // Size of remaining free block if we split at the aligned offset
                size_t remainingSize = 0;
                if (originalBlockSize > distanceToNextHeader) {
                    remainingSize = originalBlockSize - distanceToNextHeader;
                } 
                else {
                    remainingSize = 0;
                }

                // Decide whether to split or absorb the tiny remainder.
                if (remainingSize >= sizeof(Header) + sizeof(Footer)) {
                    // Split: allocated block consumes distanceToNextHeader
                    currentHeader->size = distanceToNextHeader;
                    currentHeader->status = 1;
                    currentHeader->hChecksum = calcHeaderChecksum(currentHeader);

                    // Write allocated footer at end of allocated portion
                    Footer *allocFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
                    allocFooter->size = currentHeader->size;
                    allocFooter->status = 1;
                    allocFooter->magic = 0xDEADBEEF;
                    allocFooter->fChecksum = calcFooterChecksum(allocFooter);

                    // Initialize the new free header/footer for remaining space
                    uint8_t *nextHeaderPtr = (uint8_t *)((uintptr_t)heapStart + alignedNextOffset);
                    Header *newHeader = (Header *)nextHeaderPtr;
                    newHeader->size = remainingSize;
                    newHeader->status = 0;
                    newHeader->magic = 0xCAFEBEEF;
                    newHeader->hChecksum = calcHeaderChecksum(newHeader);

                    Footer *newFreeFooter = (Footer *)((uint8_t *)newHeader + remainingSize - sizeof(Footer));
                    newFreeFooter->size = remainingSize;
                    newFreeFooter->status = 0;
                    newFreeFooter->magic = 0xDEADBEEF;
                    newFreeFooter->fChecksum = calcFooterChecksum(newFreeFooter);
                } 
                else {
                    // Absorb the tiny remainder: allocated block consumes entire original block
                    currentHeader->size = originalBlockSize;
                    currentHeader->status = 1;
                    currentHeader->hChecksum = calcHeaderChecksum(currentHeader);

                    // Place footer at original block end
                    Footer *allocFooter = (Footer *)((uint8_t *)currentHeader + originalBlockSize - sizeof(Footer));
                    allocFooter->size = originalBlockSize;
                    allocFooter->status = 1;
                    allocFooter->magic = 0xDEADBEEF;
                    allocFooter->fChecksum = calcFooterChecksum(allocFooter);
                }
                // Wipe area between header and footer 
                uint8_t *sectionStart =(uint8_t *)currentHeader + sizeof(Header);
                size_t sectionSize = currentHeader->size - sizeof(Header) - sizeof(Footer);
                if (sectionSize > 0){
                    clearSpace(sectionStart, sectionSize);
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

    if((header->magic) != 0xCAFEBEEF || header->hChecksum != calcHeaderChecksum(header)){
        printf("DEBUG: Read attempt failed - Magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
        return -1;
    }

    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer)); // For magic check
    size_t blockSize = header->size;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

    // Check pointer is in heap
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + heapSize) {
        printf("DEBUG: Read attempt failed - Pointer is outside of heap.\n");
        return -1;
    }
    // Check we're not attempting to read an empty block 
    else if((header->status) == 0){
        printf("DEBUG: Read attempt failed - Header indicates block has not been allocated. It is empty.\n");
        return -1;
    }
    // Check we're not trying to read beyond the payload
    else if (offset + len > payloadSize) {
        printf("DEBUG: Read attempt failed - Read exceeds payload size.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header->status) == 2){
        printf("DEBUG: Read attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }
    // Need to check header isn't corrupted before we check the footer

    else if((footer->magic) != 0xDEADBEEF ||
            footer->fChecksum != calcFooterChecksum(footer)){
        printf("DEBUG: Read attempt failed - Footer magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
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

    if((header->magic) != 0xCAFEBEEF ||
       header->hChecksum != calcHeaderChecksum(header)){
        printf("DEBUG: Write attempt failed - Magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
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
    else if((header->status) == 0){
        printf("DEBUG: Write attempt failed - Block has not been allocated.\n");
        return -1;
    } 
    // Check there is enough free space in the block (must fit within requested size)pace to be written to
    else if (offset + len > payloadSize) {
        printf("DEBUG: Write attempt failed - Block is too small.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header->status) == 2){
        printf("DEBUG: Write attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }

    else if((footer->magic) != 0xDEADBEEF ||
            footer->fChecksum != calcFooterChecksum(footer)){
        printf("DEBUG: Write attempt failed - Footer magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
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
    else if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + heapSize) {
        printf("DEBUG: Free attempt failed - Pointer outside heap bounds.\n");
        return;
    }
    
    Header *currentHeader = (Header *)ptr;

    // Validate header FIRST (before using header->size)

    if (currentHeader->magic != 0xCAFEBEEF ||
        currentHeader->hChecksum != calcHeaderChecksum(currentHeader)) {
        printf("DEBUG: Free attempt failed - Header magic/checksum corrupted. Block quarantined.\n");
        currentHeader->status = 2; // Quarantine instead of freeing
        return;
    }

    // Validate header size is reasonable
    size_t blockSize = currentHeader->size;
    if (blockSize < sizeof(Header) + sizeof(Footer) || blockSize > heapSize) {
        printf("DEBUG: Free attempt failed - Invalid block size. Block quarantined.\n");
        currentHeader->status = 2;
        return;
    }

    // Now safely calculate footer position
    Footer *currentFooter = (Footer *)((uint8_t *)currentHeader + blockSize - sizeof(Footer));

    // Validate footer

    if (currentFooter->magic != 0xDEADBEEF ||
        currentFooter->fChecksum != calcFooterChecksum(currentFooter)) {
        printf("DEBUG: Free attempt failed - Footer magic/checksum corrupted. Block quarantined.\n");
        currentHeader->status = 2;
        return;
    }

    // Check for double-free
    if (currentHeader->status == 0) {
        printf("DEBUG: Free attempt failed - Double-free detected.\n");
        return;
    }

    // Check block isn't already quarantined
    if (currentHeader->status == 2) {
        printf("DEBUG: Free attempt failed - Block already quarantined.\n");
        return;
    }

    // Mark as free

    currentHeader->status = 0;
    currentHeader->hChecksum = calcHeaderChecksum(currentHeader);
    currentFooter->status = 0;
    currentFooter->fChecksum = calcFooterChecksum(currentFooter);

    // Wipe payload (do not overwrite footer)
    uint8_t *wipeStart = (uint8_t *)currentHeader + sizeof(Header);
    size_t wipeSize = blockSize - sizeof(Header) - sizeof(Footer);
    if (wipeSize > 0) {
        clearSpace(wipeStart, wipeSize);
    }

    // Coalesce with adjacent blocks (previous and next) if they exist and are free
    Header *prevHeader = findPreviousHeader(currentHeader);

    // Check if previous block is free and merge

    if (prevHeader != NULL) {
        if (prevHeader->magic == 0xCAFEBEEF && prevHeader->status == 0 &&
            prevHeader->hChecksum == calcHeaderChecksum(prevHeader)) {
            Footer *prevFooter = (Footer *)((uint8_t *)prevHeader + prevHeader->size - sizeof(Footer));
            if (prevFooter->magic == 0xDEADBEEF &&
                prevFooter->fChecksum == calcFooterChecksum(prevFooter)) {
                // Merge: extend previous header to include current block
                prevHeader->size += currentHeader->size;
                prevHeader->hChecksum = calcHeaderChecksum(prevHeader);

                // Recompute footer for merged block (should be at old currentFooter location)
                Footer *mergedFooter = (Footer *)((uint8_t *)prevHeader + prevHeader->size - sizeof(Footer));
                mergedFooter->size = prevHeader->size;
                mergedFooter->status = 0;
                mergedFooter->magic = 0xDEADBEEF;
                mergedFooter->fChecksum = calcFooterChecksum(mergedFooter);

                // Update currentHeader/currentFooter to the merged block for possible next merge
                currentHeader = prevHeader;
                currentFooter = mergedFooter;

                // Wipe the payload of the merged block
                wipeStart = (uint8_t *)currentHeader + sizeof(Header);
                wipeSize = currentHeader->size - sizeof(Header) - sizeof(Footer);
                if (wipeSize > 0) {
                    clearSpace(wipeStart, wipeSize);
                }
            }
        }
    }

    // Check if next block is within bounds and free, then merge
    uintptr_t nextHeaderAddr = (uintptr_t)currentHeader + currentHeader->size;
    if (nextHeaderAddr < (uintptr_t)heapStart + heapSize) {
        Header *nextHeader = (Header *)nextHeaderAddr;

        // Validate next header before using it
        if (nextHeader->magic == 0xCAFEBEEF && nextHeader->status == 0 &&
            nextHeader->hChecksum == calcHeaderChecksum(nextHeader)) {
            Footer *nextFooter = (Footer *)((uint8_t *)nextHeader + nextHeader->size - sizeof(Footer));
            if (nextFooter->magic == 0xDEADBEEF &&
                nextFooter->fChecksum == calcFooterChecksum(nextFooter)) {
                // Merge: add next block size to current block
                currentHeader->size += nextHeader->size;
                currentHeader->hChecksum = calcHeaderChecksum(currentHeader);

                // Recompute footer for the newly merged block
                Footer *mergedFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
                mergedFooter->size = currentHeader->size;
                mergedFooter->status = 0;
                mergedFooter->magic = 0xDEADBEEF;
                mergedFooter->fChecksum = calcFooterChecksum(mergedFooter);

                // Wipe the payload area that belonged to the next block
                uint8_t *newSpaceStart = (uint8_t *)nextHeader + sizeof(Header);
                size_t newSpaceSize = nextHeader->size - sizeof(Header) - sizeof(Footer);
                if (newSpaceSize > 0) {
                    clearSpace(newSpaceStart, newSpaceSize);
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
