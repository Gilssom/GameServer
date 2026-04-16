#pragma once
#include "Allocator.h"

class MemoryPool;

/*
*   Memory (Memory Manager 의 역할)
*/
class Memory
{
	// 1024 byte 까지는 32 단위
	// 1025 ~ 2048 은	128단위
	// 4096 까지는		256 단위
	enum
	{
		POOL_COUNT = (1024 / 32) + (1024 / 128) + (2048 / 256),
		MAX_ALLOC_SIZE = 4096
	};

public:
	Memory();
	~Memory();

	void*	Allocate(int32 size);
	void	Release(void* ptr);

private:
	vector<MemoryPool*> _pools;

	// 메모리 크기 <-> 메모리 풀, O(1) 에 빠르게 찾기 위한 테이블
	MemoryPool* _poolTable[MAX_ALLOC_SIZE + 1];
};



// typename... : 인자의 개수가 가변적으로 변함
template<typename Type, typename... Args>
Type* xnew(Args&&... args)
{
	Type* memory = static_cast<Type*>(PoolAllocator::Alloc(sizeof(Type)));

	new(memory)Type(forward<Args>(args)...); // placement new

	return memory;
}

template<typename Type>
void xdelete(Type* obj)
{
	obj->~Type();
	PoolAllocator::Release(obj);
}

template<typename Type>
shared_ptr<Type> MakeShared()
{
	return shared_ptr<Type>{ xnew<Type>(), xdelete<Type> };
}