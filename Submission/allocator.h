#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

// DEBUG: Exposing Header and Footer structures for debugging purposes. REMOVE AFTER DEBUGGING
typedef struct {
    uint32_t magic; // Magic number for validation (0xCAFEBEEF)
    size_t size; // Size of the block (including the metadata)
    uint8_t status; // Status of the block (0 = free, 1 = allocated, 2 = quarantined...)
} Header;

typedef struct {
    size_t size; 
    uint8_t status;
    uint32_t magic; // 0xDEADBEEF
} Footer;

// Function prototypes
int mm_init(uint8_t *heap, size_t heap_size);
void *mm_malloc(size_t size);
int mm_read(void *ptr, size_t offset, void *buf, size_t len);
int mm_write(void *ptr, size_t offset, const void *src, size_t len);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t new_size);
void mm_heap_stats(void);

#endif // ALLOCATOR_H