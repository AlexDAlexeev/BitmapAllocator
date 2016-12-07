#pragma once

#include <set>

struct IAllocator
{
	virtual void *Alloc(size_t size) const = 0;
	virtual void Free(void *ptr) const = 0;
};

class BitmapAllocatorImpl
	: public IAllocator
{
public:
	BitmapAllocatorImpl() {	}
	~BitmapAllocatorImpl()
	{
		// Cleanup allocated chunks
		for (auto& chunk : m_allocatedChunks)
			free(chunk);
	}

	void *Alloc(size_t size) const
	{
		return const_cast<BitmapAllocatorImpl*>(this)->AllocImpl(size);
	}

	void Free(void *ptr) const
	{
		return const_cast<BitmapAllocatorImpl*>(this)->FreeImpl(ptr);
	}
//private:
	static const size_t BLOCK_MAX_MANGED_SIZE = 1024; // Maximum managed size
	static const size_t BLOCK_SIMPLE_RANGE = BLOCK_MAX_MANGED_SIZE / 64; // Range of every block
	static const size_t BLOCK_CHUNK_COUNT = 1024; // Number of allocation blocks in every class

												  // all memory chunks allocated by allocator
	std::set<void*> m_allocatedChunks;

	// Number of managed classes
	size_t GetBlockClassCount() const
	{
		return BLOCK_MAX_MANGED_SIZE / BLOCK_SIMPLE_RANGE;
	}
	// size of memory allocated for specified block class
	size_t GetBlockClassSize(size_t blockClass) const
	{
		return (blockClass + 1) * BLOCK_SIMPLE_RANGE;
	}
	// calculate block class
	size_t GetBlockClass(size_t size) const
	{
		if (size >= BLOCK_MAX_MANGED_SIZE)
			return (size_t)-1; // Block larger than BLOCK_MAX_MANGED_SIZE processed with standard allocation
							   // Here we can implement different strategies for block classification. simple is every 100 bytes will split
		return size / BLOCK_SIMPLE_RANGE; // We've got blocks f.e. 0..127, 128..255 ... 
	}

	// Header for block chunk
	struct BlockChunkHeader
	{
		// Each bit of the bitmap is block busy flag
		// Must be aligned by sizeof(DWORD) for use with _BitScanForward
#ifdef _WIN64
		unsigned char bitmap[((BLOCK_CHUNK_COUNT / 8) + 7) & ~7];
		// Lock block
		size_t LockAndGetFreeBlock()
		{
			for (size_t b = 0; b < _countof(bitmap); b += sizeof(DWORD64))
			{
				DWORD bit = 0;
				const DWORD64& mask(*((DWORD64*)&bitmap[b]));
				if (mask == 0) continue;
				if (!_BitScanForward64(&bit, mask))
					continue; // No free bits in this DWORD
				if ((b * 8 + bit) >= BLOCK_CHUNK_COUNT)
					continue; // No free bit at all
							  // set bit to indicate lock
				bitmap[b + bit / 8] &= ~(1 << (bit % 8));
				return b * 8 + bit; // block number
			}
			return -1;
		}
#else
		unsigned char bitmap[((BLOCK_CHUNK_COUNT / 8) + 3) & ~3];
		// Lock block
		size_t LockAndGetFreeBlock()
		{
			for (size_t b = 0; b < _countof(bitmap); b += sizeof(DWORD))
			{
				DWORD bit = 0;
				const DWORD& mask(*((DWORD*)&bitmap[b]));
				if (mask == 0) continue;
				if (!_BitScanForward(&bit, mask))
					continue; // No free bits in this DWORD
				if ((b * 8 + bit) >= BLOCK_CHUNK_COUNT)
					continue; // No free bit at all
							  // set bit to indicate lock
				bitmap[b + bit / 8] &= ~(1 << (bit % 8));
				return b * 8 + bit; // block number
			}
			return -1;
		}
#endif // _M_64
		// Free  block
		void UnlockBlock(size_t block)
		{
			if (block >= BLOCK_CHUNK_COUNT)
				return; // Something bad input
						// set bit for block
			bitmap[block / 8] |= 1 << (block % 8);
		}
	};

	void AllocateNewChunk()
	{
		// Size of pointers to block chunks
		const size_t chunkHeaderSize = GetBlockClassCount() * sizeof(void*);
		// Sum of blocks sizes
		size_t totalChunkSize = chunkHeaderSize;
		totalChunkSize += BLOCK_CHUNK_COUNT * BLOCK_SIMPLE_RANGE * (1 + GetBlockClassCount()) * GetBlockClassCount() / 2;
		// Add chunk headers
		totalChunkSize += GetBlockClassCount() * sizeof(BlockChunkHeader);

		void* newChunk = malloc(totalChunkSize);
		m_allocatedChunks.insert(newChunk);
		// Calculate block addresses
		void* dataBegin = (char*)newChunk + chunkHeaderSize;
		void** blockTable = (void**)newChunk;
		for (size_t i = 0; i < GetBlockClassCount(); i++)
		{
			blockTable[i] = dataBegin;
			memset(dataBegin, 0xFF, sizeof(BlockChunkHeader::bitmap));
			dataBegin = (char*)dataBegin + BLOCK_CHUNK_COUNT * GetBlockClassSize(i);
		}
	}

	void TestChunkEmptiness(void* chunk)
	{
		void** blockTable = (void**)chunk;
		for (size_t i = 0; i < GetBlockClassCount(); i++)
		{
			BlockChunkHeader* chunkHeader = (BlockChunkHeader*)blockTable[i];

			for (size_t b = 0; b < sizeof(chunkHeader->bitmap); b += sizeof(DWORD))
			{
				if (*((DWORD*)&chunkHeader->bitmap[b]) != 0xFFFFFFFF)
					return;
			}
		}
		free(chunk);
		m_allocatedChunks.erase(chunk);
	}

	// Get start address of block chunk
	void* GetBlockChunk(void* chunk, size_t blockClass) const
	{
		if (blockClass == (size_t)-1) return nullptr;
		void** blockTable = (void**)chunk;
		return blockTable[blockClass];
	}

	void* AllocImpl(size_t size)
	{
		const size_t blockClass = GetBlockClass(size);
		if (blockClass == (size_t)-1)
			return AllocateStd(size);

		const size_t blockClassSize = GetBlockClassSize(blockClass);
		// find free chunk for given class
		for (void* chunk : m_allocatedChunks)
		{
			// block base address
			char* block = (char*)GetBlockChunk(chunk, blockClass);
			BlockChunkHeader* chunkHeader = (BlockChunkHeader*)block;
			char* data = block + sizeof(BlockChunkHeader);
			// find free block in chunk
			size_t blockNumber = chunkHeader->LockAndGetFreeBlock();
			if (blockNumber == (size_t)-1)
				continue; // skip chunk to the next
			return data + blockNumber * blockClassSize;
		}
		// No free chunk, need to allocate more
		const_cast<BitmapAllocatorImpl*>(this)->AllocateNewChunk();
		return Alloc(size);
	}
	void FreeImpl(void* ptr)
	{
		// Find chunk for ptr
		auto& it = m_allocatedChunks.lower_bound(ptr);
		if (it != m_allocatedChunks.begin())
			it--;
		if (it != m_allocatedChunks.end())
		{
			void* chunk = *it;
			void** blockTable = (void**)chunk;
			for (size_t blockClass = 0; blockClass < GetBlockClassCount(); blockClass++)
			{
				BlockChunkHeader* chunkHeader = (BlockChunkHeader*)blockTable[blockClass];
				char* data = (char*)chunkHeader + sizeof(BlockChunkHeader);
				if (ptr >= data && ptr < data + GetBlockClassSize(blockClass) * BLOCK_CHUNK_COUNT)
				{
					// found
					size_t blockNumber = ((char*)ptr - data) / GetBlockClassSize(blockClass);
					chunkHeader->UnlockBlock(blockNumber);
					TestChunkEmptiness(chunk);
					return;
				}
			}
		}
		FreeStd(ptr);
	}
	void* AllocateStd(size_t size) const
	{
		return malloc(size);
	}
	void FreeStd(void* ptr) const
	{
		return free(ptr);
	}
public:
};
