#include "pch.h"
#include "MemoryPool.h"

MemoryPool::MemoryPool(int32 allocSize) : _allocSize(allocSize)
{

}

MemoryPool::~MemoryPool()
{
	while (!_queue.empty())
	{
		MemoryHeader* header = _queue.front();
		_queue.pop();
		::free(header);
	}
}

void MemoryPool::Push(MemoryHeader* ptr)
{
	WRITE_LOCK;
	ptr->allocSize = 0;

	// Pool 에 메모리 반납
	_queue.push(ptr);

	_allocCount.fetch_add(1);
}

MemoryHeader* MemoryPool::Pop()
{
	MemoryHeader* header = nullptr;

	{
		WRITE_LOCK;

		// Pool 에 현재 여분이 있는지?
		if (!_queue.empty())
		{
			// 있으면 하나 꺼내온다.
			header = _queue.front();
			_queue.pop();
		}
	}

	// 데이터가 없으면 새로 할당
	if (!header)
	{
		header = reinterpret_cast<MemoryHeader*>(::malloc(_allocSize));
	}
	// 데이터가 이미 여분이 있고, 데이터를 꺼냈다.
	else
	{
		ASSERT_CRASH(header->allocSize == 0);
	}

	_allocCount.fetch_add(1);

	return header;
}
