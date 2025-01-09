# Custom Memory Allocator

This project implements a memory allocator that allocates a large chunk of memory upfront and then manages memory within that chunk based on the requested block sizes. Essentially, memory is allocated once and only freed when all objects within the chunk are deallocated.

## How It Works

The allocator's primary idea is to categorize block sizes into classes and reuse freed blocks when necessary. Memory is initially allocated to store the required number of blocks for all classes. Each class has a corresponding bitmap where:

- A bit value of `1` indicates a free block.
- A bit value of `0` indicates an occupied block.

The bitmap is stored alongside the memory block for the class.

### Key Concepts

1. **Block Size Classes**:  
   All block sizes are divided into classes based on their size. This reduces overhead per block and optimizes memory usage.

2. **Chunk Allocation**:  
   Memory is allocated upfront for the required volume of blocks for all classes. This enables efficient reuse and faster allocation.

3. **Bitmaps**:  
   Each class uses a bitmap to track the status of its blocks. This structure minimizes the overhead associated with block management.

### Parameters

The allocator parameters are currently constants but can be adapted to support configuration during initialization. These parameters affect memory usage and performance:

- **`size_t BLOCK_MAX_MANGED_SIZE = 1024;`**  
  Maximum block size managed by the allocator. Blocks larger than this size are allocated using standard `malloc`. Increasing this value increases the total allocated memory but accommodates larger objects.

- **`size_t BLOCK_SIMPLE_RANGE = BLOCK_MAX_MANGED_SIZE / 64;`**  
  Defines the range for grouping block sizes into classes. Decreasing this value reduces per-block overhead but increases overall memory allocation. Increasing it reduces total memory usage but increases per-block overhead.

- **`size_t BLOCK_CHUNK_COUNT = 1024;`**  
  The number of blocks per class. This value should ideally exceed the average queue length to reduce the total required memory.

### Practical Application

When using this allocator in real-world scenarios, adjust the parameters based on the actual sizes of `Event` objects and the average queue length. Fine-tuning these parameters can optimize performance and memory usage.

### Limitations and Possible Improvements

- **Concurrency**:  
  The current implementation does not support multi-threading.

- **Exception Handling**:  
  There is no exception handling in the current version.

- **Performance Optimization**:  
  Performance could be improved by optimizing the placement of bitmaps and blocks.

- **Adaptive Block Sizing**:  
  The allocator could be made adaptive to dynamically adjust block size classes based on allocation patterns, allowing more efficient memory usage.

### Performance

In test cases, the current implementation is approximately **40% faster** than standard allocation for x64 and slightly less for x86.  

### Memory Usage

For test cases, the allocator requires about the same amount of memory as standard allocation. The overhead with the current settings averages **9 bytes per allocation**.
