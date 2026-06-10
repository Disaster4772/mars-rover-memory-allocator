# Mars Rover Memory Allocator

A fault-tolerant, heap-based memory allocator implemented in C99 for resource-constrained embedded systems. This project was developed as coursework for **Programming Paradigms (University of Durham)** to demonstrate software-implemented fault tolerance (SIFT) against single-bit corruption in high-radiation environments.

## Features

### Fault Tolerance & Resilience
* **Triple Modular Redundancy (TMR):** Critical global metadata is replicated across three spatially separated regions. The allocator uses runtime majority voting to detect and automatically repair bit-flips.
* **Adler-32 Checksums:** Integrated into all block headers, footers, and global metadata structures to ensure data integrity before any pointer manipulation or state change.
* **Boundary Magic Numbers:** Appended sentinel patterns (`0xCAFEBEEF`, `0xDEADBEEF`, `0xA1A1A1A1`, `0xB2B2B2B2`) act as red lines to capture out-of-bounds writes.
* **Quarantine System:** Corrupted blocks that fail checksum or validation checks are permanently quarantined to prevent cascading heap failure.

### Memory Management
* **40-byte Payload Alignment:** All allocation requests force payloads to line up precisely on a 40-byte boundary relative to the heap start (`heapStart + 40n`) to satisfy strict hardware constraints.
* **Immediate Coalescing:** Automatically merges contiguous free blocks during `mm_free` operations to prevent heap fragmentation.
* **Defensive Space Wiping:** Fills newly freed memory spaces with a persistent 5-byte pattern specified during heap initialization.
* **Double-Free Detection:** Evaluates block state flags and boundary structures to trap and log double-free vulnerabilities.

---

## Architecture & Layout

The allocator reserves a dedicated static overhead region at the base of the heap for redundant metadata copies, positioning the initial block header to maintain downstream 40-byte payload alignment.

```text
Heap Start (Offset 0)
 ├─ Metadata Region 
 │   ├─ Metadata Copy 1 (Offset 0)
 │   ├─ Metadata Copy 2 (Offset 512)
 │   └─ Metadata Copy 3 (Offset 1024) [Ends at 1047]
 │
 ├─ Padding / Alignment Gap (Bytes 1048 - 1063)
 │
 ├─ Block 1 Header  (Offset 1064; 16 bytes)
 ├─ Block 1 Payload (Offset 1080; 40-byte aligned)
 ├─ Block 1 Footer  (16 bytes)
 ├─ ...
 └─ Last Block Footer
```

---

## Layout Specifications

**Header (16 bytes):** Magic number (0xCAFEBEEF), block size, allocation status (FREE=0, ALLOCATED=1, QUARANTINED=2), and header checksum.

**Footer (16 bytes):** Magic number (0xDEADBEEF), block size, allocation status, and footer checksum.

**Heap Metadata (24 bytes per copy):** Structural verification magic numbers, total managed heap size, 5-byte wiping pattern, and regional checksum.

---

## API Reference

```c
// Initialize allocator over a raw, pre-allocated memory pool
int mm_init(uint8_t *heap, size_t heap_size);

// Allocate memory with a 40-byte aligned payload pointer
void *mm_malloc(size_t size);

// Free an allocated block and trigger boundary integrity validation
void mm_free(void *ptr);

// Safely read from a block offset with bounds and checksum validation
int mm_read(void *ptr, size_t offset, void *buf, size_t len);

// Safely write to a block offset with bounds and checksum updates
int mm_write(void *ptr, size_t offset, const void *src, size_t len);

// Resize an existing allocation block dynamically
void *mm_realloc(void *ptr, size_t new_size);

// Print runtime heap allocation statistics and fragmentation status
void mm_heap_stats(void);
```

---

## Core Mechanics

### Metadata Repair Pipeline

When reading global allocator state, the engine reads all three TMR copies:

1. Validates magic numbers and calculates regional checksums.
2. If an anomaly or bit-flip is caught, it triggers a majority vote.
3. If two copies match, the third corrupted copy is dynamically overwritten and repaired.
4. If all three copies contain mismatched corruption, a fatal heap panic is triggered.

### Bidirectional Coalescing Strategy

To optimize `mm_free` execution:

- **Backward Check:** Inspects the immediate preceding block using its footer (O(1) lookup). If that footer is unreadable, it gracefully falls back to a linear forward scan from the heap start (O(n)).
- **Forward Check:** Evaluates the next contiguous block header using pointer arithmetic.
- **Merge:** If either neighbor is marked FREE, the blocks are merged, structural dimensions are recalculated, new checksums are written, and the interior payload area is wiped clean.

---

## Building and Verification

The test harness compiles via GNU Make and handles complete verification of the allocation lifetime.

```bash
make              # Compiles allocator.c, allocator.h, and runme.c
./runme           # Executes test suites
```

The validation suite explicitly tests:
- Sequential allocation, deallocation, and strict 40-byte target alignment
- Forward/backward block merging under fragmentation stress
- Synthetic bit-flip injection to verify runtime TMR metadata healing
- Out-of-bounds memory read/write traps and double-free mitigation

---

**Status:** Complete  
**Language:** C (99%)  
**Key Concepts:** Memory management, fault tolerance, embedded systems, SIFT
