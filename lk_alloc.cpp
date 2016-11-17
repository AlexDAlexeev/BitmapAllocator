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


double PCFreq = 0.0;
__int64 CounterStart = 0;

void StartCounter()
{
	LARGE_INTEGER li;
	if (!QueryPerformanceFrequency(&li))
		return;

	PCFreq = double(li.QuadPart) / 1000.0;

	QueryPerformanceCounter(&li);
	CounterStart = li.QuadPart;
}
double GetCounter()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return double(li.QuadPart - CounterStart) / PCFreq;
}

class AllocatorImpl2
	: public IAllocator
{
	static const size_t BLOCK_MAX_MANGED_SIZE = 1024; // Maximum managed size
	static const size_t BLOCK_SIMPLE_RANGE    = BLOCK_MAX_MANGED_SIZE / 64; // Range of every block
	static const size_t BLOCK_CHUNK_COUNT     = 1024; // Number of allocation blocks in every class

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
#ifdef _WIN64
		unsigned char bitmap[((BLOCK_CHUNK_COUNT / 8) + 7) & ~7];
		static char bitmapEmpty[((BLOCK_CHUNK_COUNT / 8) + 7) & ~7];
		// Lock block
		size_t LockAndGetFreeBlock()
		{
			for (int b = 0; b < _countof(bitmap); b += sizeof(DWORD64))
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
		static char bitmapEmpty[((BLOCK_CHUNK_COUNT / 8) + 3) & ~3];
		// Lock block
		size_t LockAndGetFreeBlock()
		{
			for (int b = 0; b < _countof(bitmap); b += sizeof(DWORD))
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
			bitmap[block/8] |= 1 << (block % 8);
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
			if (memcmp(chunkHeader->bitmap, BlockChunkHeader::bitmapEmpty, sizeof(chunkHeader->bitmap)) != 0)
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
		memset(BlockChunkHeader::bitmapEmpty, 0xff, sizeof(BlockChunkHeader::bitmapEmpty));
//		total_alloc = overhead_alloc = allocations = overhead = 0;
	}
	~AllocatorImpl2()
	{
		//_tprintf(TEXT("allocations:%I64u total:%I64u total_overhead:%I64u total_overhead:%I64u average:%f\n"), allocations, total_alloc, overhead_alloc, overhead, (double)overhead / (double)(allocations == 0 ? 1.0 : allocations));
		// Cleanup allocated chunks
		for (auto& chunk : m_allocatedChunks)
			free(chunk);
	}

	//unsigned long long overhead;
	//unsigned long long allocations;
	//unsigned long long total_alloc;
	//unsigned long long overhead_alloc;

	void *Alloc(size_t size) const
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

	//void DumpChunks()
	//{
	//	if (m_allocatedChunks.empty())
	//		_tprintf(TEXT("empty!\n"));
	//	size_t chunkNr = 0;
	//	for (auto& chunk : m_allocatedChunks)
	//	{
	//		for (size_t blockClass = 0; blockClass < GetBlockClassCount(); blockClass++)
	//		{
	//			void* block = GetBlockChunk(chunk, blockClass);
	//			BlockChunkHeader* chunkHeader = (BlockChunkHeader*)block;
	//			size_t freeBlocks = chunkHeader->GetFreeBlockCount();
	//			_tprintf(TEXT("chunk:%02Iu class:%02Iu free:%02Iu\n"), chunkNr, blockClass, freeBlocks);
	//		}
	//		chunkNr++;
	//	}
	//	_tprintf(TEXT("\n"));
	//}
};
char AllocatorImpl2::BlockChunkHeader::bitmapEmpty[];

class Event
{
private:
	const char *m_data;
	const IAllocator &m_allocator;

	Event();
	Event(const Event&);
	const Event& operator=(const Event&);

public:
	Event(const IAllocator &allocator)
		: m_data(nullptr)
		, m_allocator(allocator)
	{
		m_data = (const char *)m_allocator.Alloc(std::rand() % 1024);
	}

	Event(Event &&other)
		: m_data(other.m_data)
		, m_allocator(other.m_allocator)
	{
		other.m_data = nullptr;
	}

	~Event()
	{
		if (m_data)
			m_allocator.Free((void*)m_data);
	}
};

int _tmain(int argc, _TCHAR* argv[])
{
	int iterations = 1000000;
	for (int i = 0; i < 10; i++)
	{

		{
			StartCounter();
			AllocatorImpl2 allocator;

			std::list<Event> eventsQueue;
			std::list<std::string> otherLongLifeObjects;

			srand(GetTickCount());
			int maxQueueSize = /*(std::rand() % 10000)*/ +iterations;
			for (int i = 0; i < maxQueueSize; ++i)
			{
				eventsQueue.emplace_back(Event(allocator));

				if (i % 100 == 0)
					otherLongLifeObjects.push_back(std::string(/*std::rand() % */1024, '?'));
			}

			eventsQueue.clear();
			//DebugBreak(); // тут проверяем состояние Heap-а
			otherLongLifeObjects.clear();
			_tprintf(TEXT("AllocatorImpl:%f\n"), GetCounter());
		}
		{
			StartCounter();
			AllocatorImpl allocator;

			std::list<Event> eventsQueue;
			std::list<std::string> otherLongLifeObjects;

			srand(GetTickCount());
			int maxQueueSize = /*(std::rand() % 10000)*/ +iterations;
			for (int i = 0; i < maxQueueSize; ++i)
			{
				eventsQueue.emplace_back(Event(allocator));

				if (i % 100 == 0)
					otherLongLifeObjects.push_back(std::string(/*std::rand() % */1024, '?'));
			}

			eventsQueue.clear();
			//DebugBreak(); // тут проверяем состояние Heap-а
			otherLongLifeObjects.clear();
			_tprintf(TEXT("AllocatorImpl:%f\n"), GetCounter());
		}
		_tprintf(TEXT("%d\n"), i);
	}
	return 0;
}