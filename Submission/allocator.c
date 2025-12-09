#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

// Heap start pointer must be global. Storing it in the heap creates a circular dependency
static uint8_t *heapStart = NULL;
// Payload alignment requirement
#define ALIGNMENT 40
// Metadata region: 1KB (1024 bytes) reserved at heap start for 3 copies
#define METADATA_REGION_SIZE 1024
#define METADATA_COPY1_OFFSET 0
#define METADATA_COPY2_OFFSET 512
#define METADATA_COPY3_OFFSET 1024

// Block status macros
#define BLOCK_FREE 0
#define BLOCK_ALLOCATED 1
#define BLOCK_QUARANTINED 2

typedef struct {
    uint32_t magic; // Magic number for validation (0xCAFEBEEF)
    size_t size; // Size of the block (including the metadata)
    uint8_t status; // Status of the block (0 = free, 1 = allocated, 2 = quarantined...)
    uint32_t hChecksum; // Header checksum of all above metadata
} Header;

typedef struct {
    uint32_t magic; // 0xDEADBEEF
    size_t size; 
    uint8_t status;
    uint32_t fChecksum;
} Footer;

// Heap metadata stored at the beginning of the heap, fits nicely before the first header
typedef struct {
    uint32_t magic1;             // Magic number for validation (0xA1A1A1A1)
    size_t heapSize;             // Total heap size
    uint8_t fiveBytePattern[5];  // 5-byte pattern from initial heap state
    uint32_t magic2;             // Magic number for validation (0xB2B2B2B2)
    uint32_t mChecksum;          // Adler-32 checksum for metadata validation
} HeapMetadata;

// Function to get heap size from metadata
static size_t get_heap_size(void) {
    if (heapStart == NULL) return 0;
    HeapMetadata *meta = (HeapMetadata *)heapStart;
    return meta->heapSize;
}

// Compute the offset of the first header so that:
// It does not overlap with 1KB metadata region at heap start (0-1023)
// The corresponding payload starts at an ALIGNMENT boundary from heapStart
static size_t get_first_header_offset(void) {
    size_t headerSize = sizeof(Header);
    // Metadata region is 1KB (0-1023), so first available offset is 1024
    // Round up to next ALIGNMENT boundary after metadata region
    size_t firstAvail = METADATA_REGION_SIZE;
    size_t k = (firstAvail + headerSize + (ALIGNMENT - 1)) / ALIGNMENT;
    size_t payloadOffset = k * ALIGNMENT;
    return payloadOffset - headerSize;
}


// Generic Adler-32 checksum function
uint32_t calc_adler32_checksum(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    const uint32_t modAdler = 65521;
    
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % modAdler;
        b = (b + a) % modAdler;
    }
    
    return (b << 16) | a;
}

uint32_t calc_heap_metadata_checksum(const HeapMetadata *m) {
    // offsetof() calculates the checksum up to, not including, the second parameter
    return calc_adler32_checksum((const uint8_t *)m, offsetof(HeapMetadata, mChecksum));
}

uint32_t calc_header_checksum(const Header *h) {
    return calc_adler32_checksum((const uint8_t *)h, offsetof(Header, hChecksum));
}

uint32_t calc_footer_checksum(const Footer *f) {
    return calc_adler32_checksum((const uint8_t *)f, offsetof(Footer, fChecksum));
}

// Triple Modular Redundancy (TMR): Validate heap metadata with majority voting and auto-repair
// Returns 1 if metadata is valid (with repairs if needed), 0 if catastrophic failure
static int metadata_check(void) {
    if (heapStart == NULL) return 0;
    
    // Get pointers to all 3 metadata copies: spatially separated at hardcoded offsets
    HeapMetadata *copy1 = (HeapMetadata *)(heapStart + METADATA_COPY1_OFFSET);
    HeapMetadata *copy2 = (HeapMetadata *)(heapStart + METADATA_COPY2_OFFSET);
    HeapMetadata *copy3 = (HeapMetadata *)(heapStart + METADATA_COPY3_OFFSET); 
    
    // Validate each copy's magic numbers and checksum to detect obvious corruption
    int valid1 = (copy1->magic1 == 0xA1A1A1A1 && copy1->magic2 == 0xB2B2B2B2 && copy1->mChecksum == calc_heap_metadata_checksum(copy1));
    int valid2 = (copy2->magic1 == 0xA1A1A1A1 && copy2->magic2 == 0xB2B2B2B2 && copy2->mChecksum == calc_heap_metadata_checksum(copy2));
    int valid3 = (copy3->magic1 == 0xA1A1A1A1 && copy3->magic2 == 0xB2B2B2B2 && copy3->mChecksum == calc_heap_metadata_checksum(copy3));
    int validCount = valid1 + valid2 + valid3;

    // Compare data content (heapSize + fiveBytePattern) for both strict and raw matches
    size_t cmpLen = offsetof(HeapMetadata, mChecksum);
    int strictMatch12 = valid1 && valid2 && (memcmp(copy1, copy2, cmpLen) == 0);
    int strictMatch13 = valid1 && valid3 && (memcmp(copy1, copy3, cmpLen) == 0);
    int strictMatch23 = valid2 && valid3 && (memcmp(copy2, copy3, cmpLen) == 0);
    int rawMatch12 = (memcmp(copy1, copy2, cmpLen) == 0);
    int rawMatch13 = (memcmp(copy1, copy3, cmpLen) == 0);
    int rawMatch23 = (memcmp(copy2, copy3, cmpLen) == 0);
    
    // Majority voting - if 2+ copies agree, use that value and repair the outlier
    if (strictMatch12) {  
        // Copy 1 and 2 agree (majority)
        if (!valid3) { 
            // Repair copy 3 from majority
            *copy3 = *copy1; 
            copy3->magic1 = 0xA1A1A1A1;
            copy3->magic2 = 0xB2B2B2B2;
            copy3->mChecksum = calc_heap_metadata_checksum(copy3);
            printf("WARNING: Metadata copy 3 differs from copies 1 and 2. Repairing.\n");
        }  
        return 1;
    }
    else if (strictMatch13) {  
        // Copy 1 and 3 agree (majority)
        if (!valid2) { 
            // Repair copy 2 from majority
            *copy2 = *copy1; 
            copy2->magic1 = 0xA1A1A1A1;
            copy2->magic2 = 0xB2B2B2B2;
            copy2->mChecksum = calc_heap_metadata_checksum(copy2);
            printf("WARNING: Metadata copy 2 differs from copies 1 and 3. Repairing.\n");
        }  
        return 1;
    }
    else if (strictMatch23) {
        // Copy 2 and 3 agree (majority)
        // Repair copy 1 from majority (always keep primary valid)  
        *copy1 = *copy2;  
        copy1->magic1 = 0xA1A1A1A1;
        copy1->magic2 = 0xB2B2B2B2;
        copy1->mChecksum = calc_heap_metadata_checksum(copy1);
        printf("WARNING: Metadata copy 1 differs from copies 2 and 3. Repairing.\n");
        return 1;
    }

    // If exactly one copy is checksum-valid, trust it and repair the others
    if (validCount == 1) {
        HeapMetadata *src = valid1 ? copy1 : (valid2 ? copy2 : copy3);
        *copy1 = *src;
        *copy2 = *src;
        *copy3 = *src;
        copy1->magic1 = 0xA1A1A1A1;
        copy1->magic2 = 0xB2B2B2B2;
        copy2->magic1 = 0xA1A1A1A1;
        copy2->magic2 = 0xB2B2B2B2;
        copy3->magic1 = 0xA1A1A1A1;
        copy3->magic2 = 0xB2B2B2B2;
        copy1->mChecksum = calc_heap_metadata_checksum(copy1);
        copy2->mChecksum = calc_heap_metadata_checksum(copy2);
        copy3->mChecksum = calc_heap_metadata_checksum(copy3);
        printf("WARNING: Only one valid metadata copy found. Repaired others from it.\n");
        return 1;
    }

    // Fallback: data-only majority (all checksums bad but two copies still match)
    if (rawMatch12) {
        *copy3 = *copy1;
        copy1->magic1 = 0xA1A1A1A1;
        copy1->magic2 = 0xB2B2B2B2;
        copy2->magic1 = 0xA1A1A1A1;
        copy2->magic2 = 0xB2B2B2B2;
        copy3->magic1 = 0xA1A1A1A1;
        copy3->magic2 = 0xB2B2B2B2;
        copy1->mChecksum = calc_heap_metadata_checksum(copy1);
        copy2->mChecksum = calc_heap_metadata_checksum(copy2);
        copy3->mChecksum = calc_heap_metadata_checksum(copy3);
        printf("WARNING: Metadata recovered via data majority (copies 1 and 2).\n");
        return 1;
    } else if (rawMatch13) {
        *copy2 = *copy1;
        copy1->magic1 = 0xA1A1A1A1;
        copy1->magic2 = 0xB2B2B2B2;
        copy2->magic1 = 0xA1A1A1A1;
        copy2->magic2 = 0xB2B2B2B2;
        copy3->magic1 = 0xA1A1A1A1;
        copy3->magic2 = 0xB2B2B2B2;
        copy1->mChecksum = calc_heap_metadata_checksum(copy1);
        copy2->mChecksum = calc_heap_metadata_checksum(copy2);
        copy3->mChecksum = calc_heap_metadata_checksum(copy3);
        printf("WARNING: Metadata recovered via data majority (copies 1 and 3).\n");
        return 1;
    } else if (rawMatch23) {
        *copy1 = *copy2;
        copy1->magic1 = 0xA1A1A1A1;
        copy1->magic2 = 0xB2B2B2B2;
        copy2->magic1 = 0xA1A1A1A1;
        copy2->magic2 = 0xB2B2B2B2;
        copy3->magic1 = 0xA1A1A1A1;
        copy3->magic2 = 0xB2B2B2B2;
        copy1->mChecksum = calc_heap_metadata_checksum(copy1);
        copy2->mChecksum = calc_heap_metadata_checksum(copy2);
        copy3->mChecksum = calc_heap_metadata_checksum(copy3);
        printf("WARNING: Metadata recovered via data majority (copies 2 and 3).\n");
        return 1;
    }
    
    // All copies differ or multiple corrupted the same way
    // Catastrophic failure - cannot determine correct values so the allocator must fail
        printf("ERROR: Metadata copies disagree (no majority).\n");
        printf("  copy1: valid=%d heapSize=%zu pattern=%02X %02X %02X %02X %02X\n",
            valid1,
            copy1->heapSize,
            copy1->fiveBytePattern[0], copy1->fiveBytePattern[1], copy1->fiveBytePattern[2],
            copy1->fiveBytePattern[3], copy1->fiveBytePattern[4]);
        printf("  copy2: valid=%d heapSize=%zu pattern=%02X %02X %02X %02X %02X\n",
            valid2,
            copy2->heapSize,
            copy2->fiveBytePattern[0], copy2->fiveBytePattern[1], copy2->fiveBytePattern[2],
            copy2->fiveBytePattern[3], copy2->fiveBytePattern[4]);
        printf("  copy3: valid=%d heapSize=%zu pattern=%02X %02X %02X %02X %02X\n",
            valid3,
            copy3->heapSize,
            copy3->fiveBytePattern[0], copy3->fiveBytePattern[1], copy3->fiveBytePattern[2],
            copy3->fiveBytePattern[3], copy3->fiveBytePattern[4]);
    return 0;
}

// Helper: Find the previous block header
// Attempts to use the footer directly before currentHeader for a O(1) solution, if this fails then forward scan with validation (safer if corruption is present)
Header *find_previous_header(Header *currentHeader) {
    if (currentHeader == NULL || heapStart == NULL) {
        return NULL;
    }

    size_t firstHeaderOffset = get_first_header_offset();
    uintptr_t firstHeaderPos = (uintptr_t)heapStart + firstHeaderOffset;
    uintptr_t currentAddr = (uintptr_t)currentHeader;
    uint8_t *heapEnd = heapStart + get_heap_size();

    if (currentAddr <= firstHeaderPos) {
        return NULL; // No previous block exists
    }

    // O(1) attempt using footer directly before current header
    Footer *prevFooter = (Footer *)((uint8_t *)currentHeader - sizeof(Footer));
    if ((uint8_t *)prevFooter >= heapStart) {
        if (prevFooter->magic == 0xDEADBEEF && prevFooter->fChecksum == calc_footer_checksum(prevFooter)) {
            size_t prevSize = prevFooter->size;
            Header *prevHeader = (Header *)((uint8_t *)currentHeader - prevSize);
            if ((uint8_t *)prevHeader >= heapStart && (uint8_t *)prevHeader + sizeof(Header) <= heapEnd) {
                if (prevHeader->magic == 0xCAFEBEEF && prevHeader->hChecksum == calc_header_checksum(prevHeader) && prevHeader->status != 2) {
                    if ((uint8_t *)prevHeader + prevHeader->size == (uint8_t *)currentHeader) {
                        return prevHeader;
                    }
                }
            }
        }
    }

    // Fallback: forward scan from first header with validation
    Header *scan = (Header *)firstHeaderPos;
    while ((uint8_t *)scan + sizeof(Header) <= heapEnd && scan < currentHeader) {
        if (scan->magic != 0xCAFEBEEF || scan->hChecksum != calc_header_checksum(scan)) {
            break; // Corrupted header encountered
        }
        size_t blockSize = scan->size;
        if (blockSize == 0) {
            break; // End marker or corrupted size
        }
        Header *next = (Header *)((uint8_t *)scan + blockSize);
        if (next == currentHeader) {
            return scan;
        }
        if (next <= scan || (uint8_t *)next > heapEnd) {
            break; // Prevent infinite loop or out-of-heap
        }
        scan = next;
    }

    return NULL;
}

void clear_space(uint8_t *start, size_t size) {
    // Read heap metadata from the start of the heap
    HeapMetadata *metadata = (HeapMetadata *)heapStart;
    // Replace bits with five byte pattern, relative to start of heap
    uintptr_t offset = (uintptr_t)start - (uintptr_t)heapStart;
    size_t patternStart = offset % 5;
    
    for (size_t x = 0; x < size; x++) {
        start[x] = metadata->fiveBytePattern[(patternStart + x) % 5];
    }
}


// Initialize the allocator over a provided memory block. Returns 0 on success, non-zero on failure.
int mm_init(uint8_t *heap, size_t heap_size){
    if (heap == NULL || heap_size == 0){
        return 1;
    }
    heapStart = heap;

    size_t firstHeaderOffset = get_first_header_offset();
    if (heap_size <= firstHeaderOffset + sizeof(Header) + sizeof(Footer)) {
        return 1; // Not enough space to place initial block
    }

    // Capture the initial 5-byte pattern before overwriting the heap with metadata
    uint8_t pattern[5];
    memcpy(pattern, heapStart, 5);

    // Store heap metadata at three spatially-separated offsets for redundancy
    // Copy 1 (primary) at offset 0
    HeapMetadata *copy1 = (HeapMetadata *)(heapStart + METADATA_COPY1_OFFSET);
    copy1->magic1 = 0xA1A1A1A1;
    copy1->heapSize = heap_size;
    memcpy(copy1->fiveBytePattern, pattern, 5);
    copy1->magic2 = 0xB2B2B2B2;
    copy1->mChecksum = calc_heap_metadata_checksum(copy1);

    // Copy 2 (backup) at offset 512
    HeapMetadata *copy2 = (HeapMetadata *)(heapStart + METADATA_COPY2_OFFSET);
    *copy2 = *copy1;
    copy2->magic1 = 0xA1A1A1A1;
    copy2->magic2 = 0xB2B2B2B2;
    copy2->mChecksum = calc_heap_metadata_checksum(copy2);

    // Copy 3 (backup) at offset 1024
    HeapMetadata *copy3 = (HeapMetadata *)(heapStart + METADATA_COPY3_OFFSET);
    *copy3 = *copy1; 
    copy3->magic1 = 0xA1A1A1A1;
    copy3->magic2 = 0xB2B2B2B2;
    copy3->mChecksum = calc_heap_metadata_checksum(copy3);

    // Place the first header so that its payload starts on an ALIGNMENT boundary and metadata is preserved
    Header *firstHeader = (Header *)(heapStart + firstHeaderOffset);

    firstHeader->size = heap_size - firstHeaderOffset;
    firstHeader->status = BLOCK_FREE;
    firstHeader->magic = 0xCAFEBEEF;
    firstHeader->hChecksum = calc_header_checksum(firstHeader);

    // Place footer at the end of the heap
    Footer *firstFooter = (Footer *)(heapStart + heap_size - sizeof(Footer));
    firstFooter->size = firstHeader->size;
    firstFooter->status = BLOCK_FREE;
    firstFooter->magic = 0xDEADBEEF;
    firstFooter->fChecksum = calc_footer_checksum(firstFooter);
    return 0;
}

// Allocate a block with ALIGN-byte aligned payload. Returns NULL on failure.
void *mm_malloc(size_t size){
     // Handle invalid size
     if (size == 0) {
        printf("ERROR: ALLOCATION FAILED. Attempting to allocate for a 0 size payload.\n");
        return NULL;
     }

      if (!metadata_check()) {
          printf("ERROR: ALLOCATION FAILED. Heap metadata checksum mismatch.\n");
          return NULL;
      }
    
    // First header offset is computed to preserve metadata and align payloads to 40 bytes
    size_t firstHeaderOffset = get_first_header_offset();
    Header *currentHeader = (Header *)(heapStart + firstHeaderOffset);
    uint8_t *heapEnd = heapStart + get_heap_size();

    // If the very first header was corrupted by a storm, rebuild the initial free block
    if (currentHeader->magic != 0xCAFEBEEF || currentHeader->hChecksum != calc_header_checksum(currentHeader)) {
        size_t totalSize = get_heap_size();
        size_t freeBlockSize = totalSize - firstHeaderOffset;
        if (freeBlockSize < sizeof(Header) + sizeof(Footer)) {
            printf("ERROR: ALLOCATION FAILED. Heap too small after metadata.\n");
            return NULL;
        }
        currentHeader->size = freeBlockSize;
        currentHeader->status = BLOCK_FREE;
        currentHeader->magic = 0xCAFEBEEF;
        currentHeader->hChecksum = calc_header_checksum(currentHeader);

        Footer *firstFooter = (Footer *)(heapStart + totalSize - sizeof(Footer));
        firstFooter->size = freeBlockSize;
        firstFooter->status = BLOCK_FREE;
        firstFooter->magic = 0xDEADBEEF;
        firstFooter->fChecksum = calc_footer_checksum(firstFooter);
    }
     
     // While the current header hasn't reached the end of the heap
     while ((uint8_t *)currentHeader + sizeof(Header) <= heapEnd){

        // Skip metadata region if currentHeader is within it
        uintptr_t currentOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
        if (currentOffset < METADATA_REGION_SIZE) {
            // Jump past metadata region to first allocatable block
            currentHeader = (Header *)(heapStart + firstHeaderOffset);
            if ((uint8_t *)currentHeader >= heapEnd) {
                printf("ERROR: ALLOCATION FAILED. Metadata region exhausts heap.\n");
                return NULL;
            }
            continue;
        }

        // Check for corruption in the header
        // If the header is corrupted, skip this block and continue scanning
        if (currentHeader->magic != 0xCAFEBEEF || currentHeader->hChecksum != calc_header_checksum(currentHeader)) {
            // Try to skip by 40 bytes and continue
            uintptr_t skipPos = (uintptr_t)currentHeader + 40;
            currentHeader = (Header *)skipPos;
            
            // If we've gone past the heap, we're done
            if ((uint8_t *)currentHeader >= heapEnd) {
                printf("ERROR: ALLOCATION FAILED. Exhausted heap while scanning past corruption.\n");
                return NULL;
            }
            continue;
        }
        // At this point, we can trust the header metadata
        Footer *currentFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
        if (currentHeader->status == BLOCK_QUARANTINED){
            currentFooter->status = BLOCK_QUARANTINED;
            // We've changed status, so we need to re-calculate the checksum
            currentFooter->fChecksum = calc_footer_checksum(currentFooter);
            // Use trusted header size to skip to next block
            currentHeader = (Header *)((uintptr_t)currentHeader + currentHeader->size);
            if ((uintptr_t)currentHeader > (uintptr_t)(heapStart + get_heap_size())){
                printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                return NULL;
            }
            continue;      
        }
        // Move onto checking the footer.
        else if (currentFooter->magic != 0xDEADBEEF || currentFooter->fChecksum != calc_footer_checksum(currentFooter) || currentFooter->status == BLOCK_QUARANTINED) {
            // If this is the very first block and only the footer is corrupted, rebuild footer instead of quarantining
            uintptr_t currentOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
            if (currentOffset == firstHeaderOffset && currentHeader->status == 0 &&
                currentOffset + currentHeader->size == (size_t)get_heap_size()) {
                currentFooter->size = currentHeader->size;
                currentFooter->status = 0;
                currentFooter->magic = 0xDEADBEEF;
                currentFooter->fChecksum = calc_footer_checksum(currentFooter);
                // Proceed with allocation checks on this block
            } else {
            // Header is valid, so we know this is a real header. Quarantine it since footer/payload is corrupted.
            // We don't need to separate a quarantine from a corruption because we don't do anything different
            currentHeader->status = 2;
            currentHeader->hChecksum = calc_header_checksum(currentHeader);
            currentHeader = (Header *)((uintptr_t)currentHeader + currentHeader->size);
            if ((uintptr_t)currentHeader > (uintptr_t)(heapStart + get_heap_size())){
                printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                return NULL;
            }
            continue;
            }
        }

        // If the block is marked as free and has the requested amount of space, then we can allocate this block
        if (currentHeader->status == BLOCK_FREE && (currentHeader->size - sizeof(Header) - sizeof(Footer)) >= size){
            // Save original block size before modifying
            size_t originalBlockSize = currentHeader->size;

            // Calculate difference between header and heapStart locations, add header size and round to nearest 40
            // This essentially gives us our 40n, of heapStart + 40n
            uintptr_t currentHeaderOffset = (uintptr_t)currentHeader - (uintptr_t)heapStart;
            uintptr_t payloadOffset = ((currentHeaderOffset + sizeof(Header) + 39) / 40) * 40;
            // Subtracting the size of the header tells us how far back before the payload we need to place the header
            uintptr_t newHeaderOffset = payloadOffset - sizeof(Header);
            
            // Find where the block ends
            // Round up to guarantee that there will be enough space for the next header
            // If the block takes up 39, then we shouldn't place the next payload at +40, as then there wouldn't be space for the header
            uintptr_t blockEndOffset = ((payloadOffset + size + sizeof(Footer) + 39) / 40) * 40;
            // Find out how much space we need for the block total (metadata + payload + padding)
            size_t blockSize = blockEndOffset - newHeaderOffset;
            
            // Check if we have enough space (accounting for gap + allocated block)
            // The previous check was just a quick check to see if there was enough space for the payload, now we can check if we can fit the metadata and necessary padding
            size_t spaceNeeded = (newHeaderOffset - currentHeaderOffset) + blockSize;
            if (spaceNeeded > originalBlockSize) {
                // Not enough space, try next block
                currentHeader = (Header *)((uintptr_t)currentHeader + originalBlockSize);
                if ((uint8_t *)currentHeader >= heapEnd) {
                    printf("ERROR: ALLOCATION FAILED. Reached end of heap without finding a valid header.\n");
                    return NULL;
                }
                continue;
            }
            
            // Create header at aligned position
            Header *alignedHeader = (Header *)(heapStart + newHeaderOffset);
            alignedHeader->size = blockSize;
            alignedHeader->status = BLOCK_ALLOCATED;
            alignedHeader->magic = 0xCAFEBEEF;
            alignedHeader->hChecksum = calc_header_checksum(alignedHeader);

            // Write allocated footer at end of allocated portion
            Footer *newFooter = (Footer *)(heapStart + blockEndOffset - sizeof(Footer));
            newFooter->size = blockSize;
            newFooter->status = BLOCK_ALLOCATED;
            newFooter->magic = 0xDEADBEEF;
            newFooter->fChecksum = calc_footer_checksum(newFooter);

            // Wipe the payload area
            uint8_t *sectionStart = (uint8_t *)alignedHeader + sizeof(Header);
            size_t sectionSize = blockSize - sizeof(Header) - sizeof(Footer);
            if (sectionSize > 0){
                clear_space(sectionStart, sectionSize);
            }

            // Wipe the gap (orphaned header) if there is one
            if (newHeaderOffset > currentHeaderOffset) {
                size_t gapSize = newHeaderOffset - currentHeaderOffset;
                clear_space((uint8_t *)currentHeader, gapSize);
            }

            // Check if there's remaining space to create a new free block
            size_t remainingSpace = originalBlockSize - ((newHeaderOffset - currentHeaderOffset) + blockSize);
            if (remainingSpace >= sizeof(Header) + sizeof(Footer)) {
                // Create new free block after the allocated block
                uintptr_t nextFreeOffset = blockEndOffset;
                Header *nextHeader = (Header *)(heapStart + nextFreeOffset);
                nextHeader->size = remainingSpace;
                nextHeader->status = BLOCK_FREE;
                nextHeader->magic = 0xCAFEBEEF;
                nextHeader->hChecksum = calc_header_checksum(nextHeader);

                // currentFooter was the sibling of currentHeader, but now appears after the newFooter, so just overwrite it and use it for this new block
                // Overwrite to use it for the new block
                currentFooter->size = remainingSpace;
                currentFooter->status = BLOCK_FREE;
                currentFooter->magic = 0xDEADBEEF;
                currentFooter->fChecksum = calc_footer_checksum(currentFooter);
            }
            // Return payload pointer (40-byte aligned)
            return (void *)(heapStart + payloadOffset);
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
    if (!metadata_check()) {
        printf("DEBUG: Read attempt failed - Heap metadata checksum mismatch.\n");
        return -1;
    }

    // Convert payload pointer back to header pointer
    Header *header = (Header *)((uint8_t *)ptr - sizeof(Header));
    // Check it magic number for corruption, quarantine if there is.
    // Needs to be done first, as everything else relies on the header being correct

    if((header->magic) != 0xCAFEBEEF || header->hChecksum != calc_header_checksum(header)){
        printf("DEBUG: Read attempt failed - Magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
        return -1;
    }

    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer)); // For magic check
    size_t blockSize = header->size;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

    // Check pointer is in heap
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + get_heap_size()) {
        printf("DEBUG: Read attempt failed - Pointer is outside of heap.\n");
        return -1;
    }
    // Check we're not attempting to read an empty block 
    else if((header->status) == BLOCK_FREE){
        printf("DEBUG: Read attempt failed - Header indicates block has not been allocated. It is empty.\n");
        return -1;
    }
    // Check we're not trying to read beyond the payload
    else if (offset + len > payloadSize) {
        printf("DEBUG: Read attempt failed - Read exceeds payload size.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header->status) == BLOCK_QUARANTINED){
        printf("DEBUG: Read attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }
    // Need to check header isn't corrupted before we check the footer

    else if((footer->magic) != 0xDEADBEEF ||
            footer->fChecksum != calc_footer_checksum(footer)){
        printf("DEBUG: Read attempt failed - Footer magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
        return -1;
    }

    // Read from payload starting at ptr + offset
    uint8_t *readPointer = (uint8_t *)ptr + offset;
    memcpy(buf, readPointer, len);
    return len;
}

// Safely write data into an allocated block at offset bytes from src
// Returns the number of bytes written, or -1 if corruption or invalid pointer detected
int mm_write(void *ptr, size_t offset, const void *src, size_t len){
    if (!metadata_check()) {
        printf("DEBUG: Write attempt failed - Heap metadata checksum mismatch.\n");
        return -1;
    }

    // Convert payload pointer back to header pointer
    Header *header = (Header *)((uint8_t *)ptr - sizeof(Header));
    // Check it magic number for corruption, quarantine if there is
    // Needs to be done first, as everything else relies on the header being correct

    if((header->magic) != 0xCAFEBEEF ||
       header->hChecksum != calc_header_checksum(header)){
        printf("DEBUG: Write attempt failed - Magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
        return -1;
    }
    Footer *footer = (Footer *)((uint8_t *)header + header->size - sizeof(Footer));
    // Calculate available payload space: block size minus header and footer
    size_t blockSize = header->size;
    size_t payloadSize = blockSize - sizeof(Header) - sizeof(Footer);

    // Check pointer is in heap
    if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + get_heap_size()) {
        printf("DEBUG: Write attempt failed - Pointer is outside of heap.\n");
        return -1;
    }
    // Check block has been allocated to be written to
    else if((header->status) == BLOCK_FREE){
        printf("DEBUG: Write attempt failed - Block has not been allocated.\n");
        return -1;
    } 
    // Check there is enough free space in the block (must fit within requested size)pace to be written to
    else if (offset + len > payloadSize) {
        printf("DEBUG: Write attempt failed - Block is too small.\n");
        return -1;
    }
    // Check block isn't quarantined
    else if((header->status) == BLOCK_QUARANTINED){
        printf("DEBUG: Write attempt failed - Header indicates block has been quarantined.\n");
        return -1;
    }

    else if((footer->magic) != 0xDEADBEEF ||
            footer->fChecksum != calc_footer_checksum(footer)){
        printf("DEBUG: Write attempt failed - Footer magic/checksum mismatch. Block is corrupted and will be quarantined.\n");
        header->status = 2;
        return -1;
    }

    // Write to block payload starting at ptr + offset
    uint8_t *writePointer = (uint8_t *)ptr + offset;
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

    if (!metadata_check()) {
        printf("DEBUG: Free attempt failed - Heap metadata checksum mismatch.\n");
        return;
    }
    
    // Check pointer is within heap bounds
    else if ((uint8_t *)ptr < heapStart || (uint8_t *)ptr >= heapStart + get_heap_size()) {
        printf("DEBUG: Free attempt failed - Pointer outside heap bounds.\n");
        return;
    }
    
    // ptr is the payload pointer - convert to header
    Header *currentHeader = (Header *)((uint8_t *)ptr - sizeof(Header));

    // Validate header FIRST (before using header->size)

    if (currentHeader->magic != 0xCAFEBEEF ||
        currentHeader->hChecksum != calc_header_checksum(currentHeader)) {
        printf("DEBUG: Free attempt failed - Header magic/checksum corrupted. Block quarantined.\n");
        currentHeader->status = BLOCK_QUARANTINED; // Quarantine instead of freeing
        return;
    }

    // Validate header size is reasonable
    size_t blockSize = currentHeader->size;
    if (blockSize < sizeof(Header) + sizeof(Footer) || blockSize > get_heap_size()) {
        printf("DEBUG: Free attempt failed - Invalid block size. Block quarantined.\n");
        currentHeader->status = BLOCK_QUARANTINED;
        return;
    }

    // Now safely calculate footer position
    Footer *currentFooter = (Footer *)((uint8_t *)currentHeader + blockSize - sizeof(Footer));

    // Validate footer

    if (currentFooter->magic != 0xDEADBEEF ||
        currentFooter->fChecksum != calc_footer_checksum(currentFooter)) {
        printf("DEBUG: Free attempt failed - Footer magic/checksum corrupted. Block quarantined.\n");
        currentHeader->status = BLOCK_QUARANTINED;
        return;
    }

    // Check for double-free
    if (currentHeader->status == BLOCK_FREE) {
        printf("DEBUG: Free attempt failed - Double-free detected.\n");
        return;
    }

    // Check block isn't already quarantined
    if (currentHeader->status == BLOCK_QUARANTINED) {
        printf("DEBUG: Free attempt failed - Block already quarantined.\n");
        return;
    }

    // Mark as free

    currentHeader->status = BLOCK_FREE;
    currentHeader->hChecksum = calc_header_checksum(currentHeader);
    currentFooter->status = BLOCK_FREE;
    currentFooter->fChecksum = calc_footer_checksum(currentFooter);

    // Wipe payload (do not overwrite footer)
    uint8_t *wipeStart = (uint8_t *)currentHeader + sizeof(Header);
    size_t wipeSize = blockSize - sizeof(Header) - sizeof(Footer);
    if (wipeSize > 0) {
        clear_space(wipeStart, wipeSize);
    }

    // Coalesce with adjacent blocks (previous and next) if they exist and are free
    Header *prevHeader = find_previous_header(currentHeader);

    // Check if previous block is free and merge

    if (prevHeader != NULL) {
        if (prevHeader->magic == 0xCAFEBEEF && prevHeader->status == BLOCK_FREE &&
            prevHeader->hChecksum == calc_header_checksum(prevHeader)) {
            Footer *prevFooter = (Footer *)((uint8_t *)prevHeader + prevHeader->size - sizeof(Footer));
            if (prevFooter->magic == 0xDEADBEEF &&
                prevFooter->fChecksum == calc_footer_checksum(prevFooter)) {
                // Merge: extend previous header to include current block
                prevHeader->size += currentHeader->size;
                prevHeader->hChecksum = calc_header_checksum(prevHeader);

                // Recompute footer for merged block (should be at old currentFooter location)
                Footer *mergedFooter = (Footer *)((uint8_t *)prevHeader + prevHeader->size - sizeof(Footer));
                mergedFooter->size = prevHeader->size;
                mergedFooter->status = BLOCK_FREE;
                mergedFooter->magic = 0xDEADBEEF;
                mergedFooter->fChecksum = calc_footer_checksum(mergedFooter);

                // Update currentHeader/currentFooter to the merged block for possible next merge
                currentHeader = prevHeader;
                currentFooter = mergedFooter;

                // Wipe the payload of the merged block
                wipeStart = (uint8_t *)currentHeader + sizeof(Header);
                wipeSize = currentHeader->size - sizeof(Header) - sizeof(Footer);
                if (wipeSize > 0) {
                    clear_space(wipeStart, wipeSize);
                }
            }
        }
    }

    // Check if next block is within bounds and free, then merge
    uintptr_t nextHeaderAddr = (uintptr_t)currentHeader + currentHeader->size;
    if (nextHeaderAddr < (uintptr_t)heapStart + get_heap_size()) {
        Header *nextHeader = (Header *)nextHeaderAddr;

        // Validate next header before using it
        if (nextHeader->magic == 0xCAFEBEEF && nextHeader->status == BLOCK_FREE &&
            nextHeader->hChecksum == calc_header_checksum(nextHeader)) {
            Footer *nextFooter = (Footer *)((uint8_t *)nextHeader + nextHeader->size - sizeof(Footer));
            if (nextFooter->magic == 0xDEADBEEF &&
                nextFooter->fChecksum == calc_footer_checksum(nextFooter)) {
                // Merge: add next block size to current block
                currentHeader->size += nextHeader->size;
                currentHeader->hChecksum = calc_header_checksum(currentHeader);

                // Recompute footer for the newly merged block
                Footer *mergedFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
                mergedFooter->size = currentHeader->size;
                mergedFooter->status = BLOCK_FREE;
                mergedFooter->magic = 0xDEADBEEF;
                mergedFooter->fChecksum = calc_footer_checksum(mergedFooter);

                // Wipe the payload area that belonged to the next block
                uint8_t *newSpaceStart = (uint8_t *)nextHeader + sizeof(Header);
                size_t newSpaceSize = nextHeader->size - sizeof(Header) - sizeof(Footer);
                if (newSpaceSize > 0) {
                    clear_space(newSpaceStart, newSpaceSize);
                }
            }
        } else {
            // No valid header found at expected offset - might be padding
            // Scan forward in 40-byte increments to find the next block header
            for (uintptr_t scanAddr = nextHeaderAddr + 40; scanAddr < (uintptr_t)heapStart + get_heap_size(); scanAddr += 40) {
                Header *scanHeader = (Header *)scanAddr;
                if (scanHeader->magic == 0xCAFEBEEF && scanHeader->status == BLOCK_FREE &&
                    scanHeader->hChecksum == calc_header_checksum(scanHeader)) {
                    // Found a valid free block header - include padding and merge
                    size_t paddingSize = scanAddr - nextHeaderAddr;
                    currentHeader->size += paddingSize + scanHeader->size;
                    currentHeader->hChecksum = calc_header_checksum(currentHeader);

                    // Recompute footer
                    Footer *mergedFooter = (Footer *)((uint8_t *)currentHeader + currentHeader->size - sizeof(Footer));
                    mergedFooter->size = currentHeader->size;
                    mergedFooter->status = BLOCK_FREE;
                    mergedFooter->magic = 0xDEADBEEF;
                    mergedFooter->fChecksum = calc_footer_checksum(mergedFooter);
                    break;
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
