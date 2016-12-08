// lk_alloc_test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <malloc.h>

#include <gtest/gtest.h>
#include "../BitmapAllocatorImpl.h"
#include <list>

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
		m_data = (const char *)m_allocator.Alloc(/*std::rand() % */1023);
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

class BitmapAllocatorTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{

	}
	virtual void TearDown()
	{

	}
	BitmapAllocatorImpl m_allocator;
};

TEST_F(BitmapAllocatorTest, CleanOne_NewSame)
{
	void *p1 = m_allocator.Alloc(1023);
	void *p3 = m_allocator.Alloc(1023);
	m_allocator.Free(p1);
	void *p2 = m_allocator.Alloc(1023);
	m_allocator.Free(p2);
	EXPECT_EQ(p1, p2);
}

TEST_F(BitmapAllocatorTest, Clean_IsEmpty)
{
	std::list<Event> eventList;
	for (size_t i = 0; i < 1000; i++)
		eventList.emplace_back(Event(m_allocator));
	eventList.clear();
	EXPECT_TRUE(m_allocator.m_allocatedChunks.empty());
}

TEST_F(BitmapAllocatorTest, FullFill)
{
	std::list<Event> eventList;
	for (size_t i = 0; i < BitmapAllocatorImpl::BLOCK_CHUNK_COUNT + 1; i++)
		eventList.emplace_back(Event(m_allocator));
	EXPECT_EQ(m_allocator.m_allocatedChunks.size(), 2);
}

TEST_F(BitmapAllocatorTest, ExtraSize)
{
	for (size_t i = 0; i < BitmapAllocatorImpl::BLOCK_CHUNK_COUNT + 1; i++)
		m_allocator.Alloc(1024);
	EXPECT_EQ(m_allocator.m_allocatedChunks.size(), 0);
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

