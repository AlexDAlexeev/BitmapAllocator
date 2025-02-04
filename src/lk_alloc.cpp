// lk_alloc.cpp : Defines the entry point for the console application.
//

#include <BitmapAllocatorImpl.h>
#include <Windows.h>
#include <malloc.h>

#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

class AllocatorImpl
    : public IAllocator {
   public:
    void* Alloc(size_t size) const {
        return malloc(size);
    }

    void Free(void* ptr) const {
        free(ptr);
    }
};

double PCFreq = 0.0;
__int64 CounterStart = 0;

void StartCounter() {
    LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li))
        return;

    PCFreq = double(li.QuadPart) / 1000.0;

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}
double GetCounter() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart - CounterStart) / PCFreq;
}

class Event {
   private:
    const char* m_data;
    const IAllocator& m_allocator;

    Event();
    Event(const Event&);
    const Event& operator=(const Event&);

   public:
    Event(const IAllocator& allocator)
        : m_data(nullptr), m_allocator(allocator) {
        m_data = (const char*)m_allocator.Alloc(std::rand() % 1024);
    }

    Event(Event&& other)
        : m_data(other.m_data), m_allocator(other.m_allocator) {
        other.m_data = nullptr;
    }

    ~Event() {
        if (m_data)
            m_allocator.Free((void*)m_data);
    }
};

int main(int argc, char* argv[]) {
    BitmapAllocatorImpl allocator;
    std::list<Event> eventsQueue;
    eventsQueue.emplace_back(Event(allocator));
    eventsQueue.clear();

    int iterations = 100000;
    for (int i = 0; i < 10; i++) {
        {
            StartCounter();
            BitmapAllocatorImpl allocator;

            std::list<Event> eventsQueue;
            std::list<std::string> otherLongLifeObjects;

            srand(GetTickCount());
            int maxQueueSize = /*(std::rand() % 10000)*/ +iterations;
            for (int i = 0; i < maxQueueSize; ++i) {
                eventsQueue.emplace_back(Event(allocator));

                if (i % 100 == 0)
                    otherLongLifeObjects.push_back(std::string(/*std::rand() % */ 1024, '?'));
            }

            eventsQueue.clear();
            // DebugBreak(); // ˜˜˜ ˜˜˜˜˜˜˜˜˜ ˜˜˜˜˜˜˜˜˜ Heap-˜
            otherLongLifeObjects.clear();
            std::cout << "AllocatorImpl:" << GetCounter() << std::endl;
        }
        {
            StartCounter();
            AllocatorImpl allocator;

            std::list<Event> eventsQueue;
            std::list<std::string> otherLongLifeObjects;

            srand(GetTickCount());
            int maxQueueSize = /*(std::rand() % 10000)*/ +iterations;
            for (int i = 0; i < maxQueueSize; ++i) {
                eventsQueue.emplace_back(Event(allocator));

                if (i % 100 == 0)
                    otherLongLifeObjects.push_back(std::string(/*std::rand() % */ 1024, '?'));
            }

            eventsQueue.clear();
            // DebugBreak(); // ˜˜˜ ˜˜˜˜˜˜˜˜˜ ˜˜˜˜˜˜˜˜˜ Heap-˜
            otherLongLifeObjects.clear();
            std::cout << "AllocatorImpl:" << GetCounter() << std::endl;
        }
        std::cout << i << std::endl;
    }
    return 0;
}