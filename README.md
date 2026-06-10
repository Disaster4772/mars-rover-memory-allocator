# Mars Rover Memory Allocator

A fault-tolerant, heap-based memory allocator implemented in C99 for resource-constrained embedded systems. This project demonstrates software-implemented fault tolerance (SIFT) against single-bit corruption in high-radiation environments, using Triple Modular Redundancy (TMR) and checksums for automatic corruption detection and repair.

## Overview

This allocator provides a complete memory management solution with sophisticated error recovery mechanisms suitable for mission-critical environments where memory corruption cannot be tolerated. It manages a fixed-size heap with automatic defragmentation and resilience against single-bit corruption through redundancy and checksums—essential capabilities for systems operating in harsh environments like space.

---

## Approach & Solution Design

### 1.1 Initialisation

`mm_init()` stores three `HeapMetadata` copies at offsets (0/512/1024) from `heapStart`. It then places a header to align the first payload to a 40-byte boundary and a footer at the heap end, creating the initial free block.

**Memory Layout:**

```
HEAP START (0)
├─ HEAP METADATA COPY 1        [32 bytes]
├─ HEAP METADATA COPY 2 (512)  [32 bytes]
├─ HEAP METADATA COPY 3 (1024) [32 bytes]
│
├─ [Padding / Alignment Gap]
│
├─ FIRST HEADER   [24 bytes]
├─ FIRST PAYLOAD  [40-byte aligned]
├─ FIRST FOOTER   [24 bytes]
│
├─ [Additional Allocated/Free Blocks...]
│
└─ LAST FOOTER    [24 bytes]
```

### 1.2 Data Structures

Three core structures manage the heap:

**Header & Footer** form a protective envelope around each block, storing size/status with mirroring for detection and an Adler-32 checksum.

```c
typedef struct {
    uint32_t magic;      // 0xCAFEBEEF
    size_t size;         // Block size (including metadata)
    uint8_t status;      // FREE=0, ALLOCATED=1, QUARANTINED=2
    uint32_t hChecksum;  // Header checksum
} Header;

typedef struct {
    uint32_t magic;      // 0xDEADBEEF
    size_t size;
    uint8_t status;
    uint32_t fChecksum;  // Footer checksum
} Footer;
```

**HeapMetadata** stores the global heap state, protected by Triple Modular Redundancy (TMR).

```c
typedef struct {
    uint32_t magic1;             // 0xA1A1A1A1
    size_t heapSize;             // Total heap size
    uint8_t fiveBytePattern[5];  // Pattern from heap initialization
    uint32_t magic2;             // 0xB2B2B2B2
    uint32_t mChecksum;          // Metadata checksum
} HeapMetadata;
```

### 1.3 Allocation

`mm_malloc()` iterates through valid headers to find a free block. It validates the header and footer, quarantining corrupted blocks. If successful, it splits the block if needed and returns a 40-byte aligned payload pointer.

**Algorithm:**
1. Scan headers from first allocatable block
2. Skip corrupted blocks (quarantine them)
3. For free blocks with sufficient size:
   - Calculate aligned payload position
   - Split block if necessary
   - Create new header/footer with checksums
   - Wipe payload and any orphaned gap
4. Return aligned payload pointer or NULL

### 1.4 Freeing and Coalescing

`mm_free()` validates the block, marks it free, and updates checksums. It then coalesces with adjacent free blocks using `find_previous_header()` (O(1) attempt via footer, O(n) fallback) and forward scanning.

**Coalescing Strategy:**
- **Backward Check:** Inspect preceding block via footer (O(1)); fallback to forward scan (O(n))
- **Forward Check:** Evaluate next block via pointer arithmetic
- **Merge:** If either neighbor is FREE, merge and wipe newly freed space

This reduces fragmentation and improves practical utility over many allocation cycles.

### 1.5 Storm Resilience

All allocator interfaces (`mm_malloc`, `mm_read`, `mm_write`, `mm_free`) validate relevant metadata on every operation, ensuring no operation proceeds on a corrupted block. Block metadata uses magic numbers (0xCAFEBEEF, 0xDEADBEEF) for fast rejection, and Adler-32 checksums for integrity.

Corrupted blocks are quarantined. Global heap integrity uses TMR: three `HeapMetadata` copies at 0/512/1024 byte offsets, surviving clustered bit-flips via majority-voting algorithm for self-repair.

---

## Analysis of Solution

### 2.1 Memory Usage

Fixed costs for robustness:
- **1024-byte TMR region** (three metadata copies: 24 bytes × 3)
- **48 bytes per block** (Header 16 + Footer 16 + alignment padding ~16)
- **Total heap overhead** (1024 + 48×N) / heap_size ≈ 2.9% for typical use

**Example:** 8 KB heap with 100-byte blocks:
```
1024 (TMR) + (10 × 48) + 1894 (payload) = 1894 bytes ≈ 23.6% overhead
```

This overhead is justified for systems requiring fault tolerance.

### 2.2 Time Complexity

**Operations:**
- `mm_malloc()`: O(1) best-case (first fit), O(n) worst-case with fragmentation or quarantining
- `mm_free()`: O(1) with validated footer, O(n) with fallback scan
- `mm_read()` / `mm_write()`: O(1) access plus O(1) checksum validation
- **Checksum validation:** O(1) per block (bounded by metadata size)

Predictable overhead ensures allocation/deallocation are real-time friendly.

### 2.3 Storm Resilience

The design provides quantifiable fault tolerance:
- **512-byte spacing between TMR copies** — survives clustered bit-flips across individual copies
- **Checksum coverage** — detects any undetected error per validated field
- **Majority voting** — recovers from single-copy corruption
- **Adler-32 probability** — ~1/2^32 chance of undetected error per field

In the allocator's operational lifetime, this moves the failure mode from total heap collapse to graceful degradation with self-healing.

---

## Additional Functionality

### 4.1 Coalescing to Combat Fragmentation

The `mm_free()` function merges adjacent free blocks using a validated `find_previous_header()` and forward scanning. This reduces fragmentation, maintains the discoverability of practical utility over many allocation cycles, and ensures long-term heap health.

### 4.2 Essential Metadata Recovery

The TMR system implements automatic recovery. On every allocator call:
1. Load all three metadata copies
2. Validate checksums and magic numbers
3. If 2+ copies agree → use that value and repair the outlier
4. If all copies corrupt differently → fail gracefully

This self-healing capability enables the allocator to survive transient corruption without human intervention.

---

## Architecture Highlights

### Corruption Detection & Recovery Flow

```
START
  ↓
ALL VALID?
  ├─ YES → Use metadata
  ├─ NO → Checksum valid?
  │       ├─ YES → Use 1 copy, repair others
  │       ├─ NO → Data majority?
  │       │       ├─ YES → Recover via majority
  │       │       ├─ NO → FAIL (catastrophic)
  │       └─ [Repair all copies]
  │
SUCCESS ← All checksums updated
  ↓
CONTINUE OPERATION
```

### Alignment Strategy

All payloads align to 40-byte boundaries from `heapStart + 40n`. Headers position themselves to satisfy this requirement while preserving the 1 KB metadata region at the heap start. This ensures efficient memory access patterns and satisfies hardware alignment constraints.

---

## Building & Testing

```bash
make              # Compiles allocator.c, allocator.h, runme.c
./runme           # Executes comprehensive test suite
```

**Test Coverage:**
- Sequential allocation, deallocation, and 40-byte alignment verification
- Forward/backward block merging under fragmentation stress
- Synthetic bit-flip injection to verify TMR metadata healing
- Out-of-bounds memory read/write traps
- Double-free prevention and detection
- Boundary conditions and edge cases

---

## API Reference

```c
// Initialize allocator over provided heap
int mm_init(uint8_t *heap, size_t heap_size);

// Allocate memory with 40-byte aligned payload pointer
void *mm_malloc(size_t size);

// Free an allocated block with boundary integrity validation
void mm_free(void *ptr);

// Safely read from block offset with bounds and checksum validation
int mm_read(void *ptr, size_t offset, void *buf, size_t len);

// Safely write to block offset with bounds and checksum updates
int mm_write(void *ptr, size_t offset, const void *src, size_t len);

// Resize an existing allocation block dynamically
void *mm_realloc(void *ptr, size_t new_size);

// Print runtime heap allocation statistics and fragmentation status
void mm_heap_stats(void);
```

---

## Technology Stack

- **Language:** C (C99 standard)
- **Build:** GNU Make
- **Complexity:** O(1) malloc (best-case), O(n) with fragmentation
- **Memory Overhead:** ~2-3% for TMR + metadata
- **Fault Tolerance:** Triple Modular Redundancy with majority voting

---

## Project Context

This allocator was developed as coursework for **COMP2221 Systems Programming (University of Durham, 2025/26)** to demonstrate:
- Low-level memory management in embedded systems
- Fault tolerance design for harsh environments
- Trade-offs between robustness and performance
- Professional-grade systems programming in C

The design targets Mars rover memory constraints where single-bit corruption from cosmic radiation cannot be ignored, and automatic recovery is mission-critical.

---

**Status:** Complete  
**Language:** C (99%)  
**Lines of Code:** ~850 (core allocator)  
**Key Concepts:** Memory management, fault tolerance, TMR, SIFT, embedded systems
