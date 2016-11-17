// lk_alloc.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <Windows.h>
#include <malloc.h>
#include <list>
#include <vector>
#include <map>
#include <set>

struct IAllocator
{
	virtual void *Alloc(size_t size) const = 0;
	virtual void Free(void *ptr) const = 0;
};

class AllocatorImpl
	: public IAllocator
{
public:
	void *Alloc(size_t size) const
	{
		return malloc(size);
	}

	void Free(void *ptr) const
	{
		free(ptr);
	}
};

class AllocatorImpl2
	: public IAllocator
{
	static const size_t BLOCK_MAX_MANGED_SIZE = 1024; // Maximum managed size
	static const size_t BLOCK_SIMPLE_RANGE    = BLOCK_MAX_MANGED_SIZE / 64; // Range of every block
	static const size_t BLOCK_CHUNK_COUNT     = 1024; // Number of allocation blocks in every class

	// Number of managed classes
	static size_t GetBlockClassCount()
	{
		return BLOCK_MAX_MANGED_SIZE / BLOCK_SIMPLE_RANGE;
	}
	// size of memory allocated for specified block class
	static size_t GetBlockClassSize(size_t blockClass) 
	{ 
		return (blockClass + 1) * BLOCK_SIMPLE_RANGE; 
	}
	// calculate block class
	static size_t GetBlockClass(size_t size)
	{
		if (size >= BLOCK_MAX_MANGED_SIZE)
			return (size_t)-1; // Block larger than BLOCK_MAX_MANGED_SIZE processed with standard allocation
		// Here we can implement different strategies for block classification. simple is every 100 bytes will split
		return size / BLOCK_SIMPLE_RANGE; // We've got blocks f.e. 0..127, 128..255 ... 
	}

	// all memory chunks allocated by allocator
	std::set<void*> m_allocatedChunks;
	// Memory layout
	/*
	0..blockClassCount - 1 offsets of block bitmaps
	*/
	
	/*  Bit counter by Ratko Tomic */
	static size_t countbits(DWORD i)
	{
		i = ((i & 0xAAAAAAAAL) >> 1) + (i & 0x55555555L);
		i = ((i & 0xCCCCCCCCL) >> 2) + (i & 0x33333333L);
		i = ((i & 0xF0F0F0F0L) >> 4) + (i & 0x0F0F0F0FL);
		i = ((i & 0xFF00FF00L) >> 8) + (i & 0x00FF00FFL);
		i = ((i & 0xFFFF0000L) >> 16) + (i & 0x0000FFFFL);
		return (int)i;
	}

	// Header for block chunk
	struct BlockChunkHeader
	{
		// Each bit of the bitmap is block busy flag
		// Must be aligned by sizeof(DWORD) for use with _BitScanForward
		unsigned char bitmap[((BLOCK_CHUNK_COUNT / 8) + 3) & ~3];
		// Lock block
		size_t LockAndGetFreeBlock()
		{
			for (int b = 0; b < _countof(bitmap); b += sizeof(DWORD))
			{
				DWORD bit = 0;
				const DWORD& mask(*((DWORD*)&bitmap[b]));
				if (!_BitScanForward(&bit, ~mask))
					continue; // No free bits in this DWORD
				if ((b * 8 + bit) >= BLOCK_CHUNK_COUNT)
					continue; // No free bit at all
				// set bit to indicate lock
				bitmap[b + bit / 8] |= 1 << (bit % 8);
				return b * 8 + bit; // block number
			}
			return -1;
		}
		// Free  block
		void UnlockBlock(size_t block)
		{
			if (block >= BLOCK_CHUNK_COUNT)
				return; // Something bad input
			// reset bit for block
			bitmap[block/8] &= ~(1 << (block % 8));
		}
		//
		size_t GetFreeBlockCount()
		{
			size_t freeBlocks = 0;
			for (int b = 0; b < _countof(bitmap); b += 4)
			{
				DWORD dw(*((DWORD*)&bitmap[b]));
				freeBlocks += countbits(~dw);
			}
			return freeBlocks;
		}
	};

	void AllocateNewChunk()
	{
		// Size of pointers to block chunks
		size_t chunkHeaderSize = GetBlockClassCount() * sizeof(void*);

		size_t totalChunkSize = 0;
		// Sum of blocks sizes
		for (size_t i = 0; i < GetBlockClassCount(); i++)
			totalChunkSize += GetBlockClassSize(i);
		totalChunkSize *= BLOCK_CHUNK_COUNT;
		// Add chunk headers
		totalChunkSize += GetBlockClassCount() * sizeof(BlockChunkHeader);
		totalChunkSize += chunkHeaderSize;

		void* newChunk = malloc(totalChunkSize);
		memset(newChunk, 0, totalChunkSize);
		// Calculate block addresses
		void* dataBegin = (char*)newChunk + chunkHeaderSize;
		void** blockTable = (void**)newChunk;
		for (size_t i = 0; i < GetBlockClassCount(); i++)
		{
			blockTable[i] = dataBegin;
			dataBegin = (char*)dataBegin + BLOCK_CHUNK_COUNT * GetBlockClassSize(i);
		}
		m_allocatedChunks.insert(newChunk);
	}
	void TestChunkEmptiness(void* chunk)
	{
		void** blockTable = (void**)chunk;
		for (size_t i = 0; i < GetBlockClassCount(); i++)
		{
			BlockChunkHeader* chunkHeader = (BlockChunkHeader*)blockTable[i];
			if (chunkHeader->GetFreeBlockCount() != BLOCK_CHUNK_COUNT)
				return;
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

	void* AllocateStd(size_t size) const
	{
		return malloc(size);
	}
	void FreeStd(void* ptr) const
	{
		return free(ptr);
	}
public:
	AllocatorImpl2()
	{
		allocations = overhead = 0;
	}
	~AllocatorImpl2()
	{
		_tprintf(TEXT("allocations:%I64u total_overhead:%I64u average:%f"), allocations, overhead, (double)overhead / (double)(allocations == 0 ? 1.0 : allocations));
		// Cleanup allocated chunks
		for (auto& chunk : m_allocatedChunks)
			free(chunk);
	}

	unsigned long long overhead;
	unsigned long long allocations;

	void *Alloc(size_t size) const
	{
		size_t blockClass = GetBlockClass(size);
		if (blockClass == (size_t)-1) 
			return AllocateStd(size);

		size_t blockClassSize = GetBlockClassSize(blockClass);
		AllocatorImpl2* pThis = const_cast<AllocatorImpl2*>(this);
		pThis->allocations++;
		pThis->overhead += (blockClassSize - size);
		// find free chunk for given class
		for (void* chunk : m_allocatedChunks)
		{
			// block base address
			void* block = GetBlockChunk(chunk, blockClass);
			void* data = (char*)block + sizeof(BlockChunkHeader);
			BlockChunkHeader* chunkHeader = (BlockChunkHeader*)block;
			// find free block in chunk
			size_t blockNumber = chunkHeader->LockAndGetFreeBlock();
			if (blockNumber == (size_t)-1)
				continue; // skip chunk to the next
			return (char*)data + blockNumber * blockClassSize;
		}
		// No free chunk, need to allocate more
		const_cast<AllocatorImpl2*>(this)->AllocateNewChunk();
		return Alloc(size);
	}

	void Free(void *ptr) const
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
					const_cast<AllocatorImpl2*>(this)->TestChunkEmptiness(chunk);
					return;
				}
			}
		}
		FreeStd(ptr);
	}

	void DumpChunks()
	{
		if (m_allocatedChunks.empty())
			_tprintf(TEXT("empty!\n"));
		size_t chunkNr = 0;
		for (auto& chunk : m_allocatedChunks)
		{
			for (size_t blockClass = 0; blockClass < GetBlockClassCount(); blockClass++)
			{
				void* block = GetBlockChunk(chunk, blockClass);
				BlockChunkHeader* chunkHeader = (BlockChunkHeader*)block;
				size_t freeBlocks = chunkHeader->GetFreeBlockCount();
				_tprintf(TEXT("chunk:%02d class:%02d free:%d\n"), chunkNr, blockClass, freeBlocks);
			}
			chunkNr++;
		}
		_tprintf(TEXT("\n"));
	}
};

int WalkHeap(HANDLE hHeap)
{
	DWORD LastError;
	//HANDLE hHeap;
	PROCESS_HEAP_ENTRY Entry;

	//
	// Lock the heap to prevent other threads from accessing the heap
	// during enumeration.
	//
	if (HeapLock(hHeap) == FALSE)
	{
		_tprintf(TEXT("Failed to lock heap with LastError %d.\n"),
		         GetLastError());
		return 1;
	}

	_tprintf(TEXT("Walking heap %#p...\n\n"), hHeap);

	Entry.lpData = NULL;
	while (HeapWalk(hHeap, &Entry) != FALSE)
	{
		if ((Entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
		{
			_tprintf(TEXT("Allocated block"));

			if ((Entry.wFlags & PROCESS_HEAP_ENTRY_MOVEABLE) != 0)
			{
				_tprintf(TEXT(", movable with HANDLE %#p"), Entry.Block.hMem);
			}

			if ((Entry.wFlags & PROCESS_HEAP_ENTRY_DDESHARE) != 0)
			{
				_tprintf(TEXT(", DDESHARE"));
			}
		}
		else if ((Entry.wFlags & PROCESS_HEAP_REGION) != 0)
		{
			_tprintf(TEXT("Region\n  %d bytes committed\n") \
			         TEXT("  %d bytes uncommitted\n  First block address: %#p\n") \
			         TEXT("  Last block address: %#p\n"),
			         Entry.Region.dwCommittedSize,
			         Entry.Region.dwUnCommittedSize,
			         Entry.Region.lpFirstBlock,
			         Entry.Region.lpLastBlock);
		}
		else if ((Entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) != 0)
		{
			_tprintf(TEXT("Uncommitted range\n"));
		}
		else
		{
			_tprintf(TEXT("Block\n"));
		}

		_tprintf(TEXT("  Data portion begins at: %#p\n  Size: %d bytes\n") \
		         TEXT("  Overhead: %d bytes\n  Region index: %d\n\n"),
		         Entry.lpData,
		         Entry.cbData,
		         Entry.cbOverhead,
		         Entry.iRegionIndex);
	}
	LastError = GetLastError();
	if (LastError != ERROR_NO_MORE_ITEMS)
	{
		_tprintf(TEXT("HeapWalk failed with LastError %d.\n"), LastError);
	}

	//
	// Unlock the heap to allow other threads to access the heap after
	// enumeration has completed.
	//
	if (HeapUnlock(hHeap) == FALSE)
	{
		_tprintf(TEXT("Failed to unlock heap with LastError %d.\n"),
		         GetLastError());
	}
	return 0;
}

class Event
{
private:
	const char *m_data;
	size_t m_size;
	const IAllocator &m_allocator;

	Event();
	Event(const Event&);
	const Event& operator=(const Event&);

public:
	Event(const IAllocator &allocator)
		: m_data(nullptr)
		, m_allocator(allocator)
		, m_size(0)
	{
		m_size = std::rand() % 1024;
		m_data = (const char *)m_allocator.Alloc(m_size);
	}

	Event(Event &&other)
		: m_data(other.m_data)
		, m_allocator(other.m_allocator)
		, m_size(other.m_size)
	{
		other.m_data = nullptr;
	}

	~Event()
	{
		if (m_data)
			m_allocator.Free((void*)m_data);
	}
	size_t getSize() const { return m_size; }
};

int _tmain(int argc, _TCHAR* argv[])
{
	AllocatorImpl2 allocator;
	
	// Test Allocator
	std::vector<void*> blocks;
	for (int i = 0; i < 64; i++)
		blocks.emplace_back(allocator.Alloc(1023));

	allocator.DumpChunks();
	for (auto& it : blocks)
		allocator.Free(it);
	allocator.DumpChunks();
	blocks.clear();

	void* t1 = allocator.Alloc(1023);
	allocator.DumpChunks();
	allocator.Free(t1);
	allocator.DumpChunks();
	void* t2 = allocator.Alloc(1023);
	void* t3 = allocator.Alloc(1023);
	allocator.Free(t2);
	allocator.DumpChunks();
	void* t4 = allocator.Alloc(1023);
	allocator.DumpChunks();
	allocator.Free(t1);
	allocator.Free(t3);
	allocator.Free(t4);
	allocator.DumpChunks();

 	for (int i = 0; i < 1025; i++)
 	{
 		void* block = allocator.Alloc(i);
 		memset(block, (unsigned char)i, i);
 		blocks.push_back(block);
 	}
 	for (int i = 0; i < 1025; i++)
 	{
 		unsigned char* block = (unsigned char*)blocks[i];
 		for (int j = 0; j < i; j++)
 			if (block[j] != (unsigned char)i) DebugBreak();
 	}
	allocator.DumpChunks();
	for (int i = 0; i < 1025; i++)
		allocator.Free(blocks[i]);
	allocator.DumpChunks();


	std::list<Event> eventsQueue;
	std::list<std::string> otherLongLifeObjects;

	srand(GetTickCount());
	int maxQueueSize = (std::rand() % 10000) + 100000;
	for (int i = 0; i < maxQueueSize; ++i)
	{
		eventsQueue.emplace_back(Event(allocator));

		if (i % 100 == 0)
			otherLongLifeObjects.push_back(std::string(std::rand() % 1024, '?'));
	}
	allocator.DumpChunks();
	::MessageBox(NULL, TEXT("BEFORE"), TEXT(""), MB_OK);
	eventsQueue.clear();
	allocator.DumpChunks();
	::MessageBox(NULL, TEXT("AFTER"), TEXT(""), MB_OK);
	//DebugBreak(); // тут проверяем состояние Heap-а
	otherLongLifeObjects.clear();
	::MessageBox(NULL, TEXT("EXIT"), TEXT(""), MB_OK);

	return 0;
}