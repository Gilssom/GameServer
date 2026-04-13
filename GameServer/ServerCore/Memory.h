#pragma once
#include "Allocator.h"


// typename... : 인자의 개수가 가변적으로 변함
template<typename Type, typename... Args>
Type* xnew(Args&&... args)
{
	Type* memory = static_cast<Type*>(x_alloc(sizeof(Type)));

	new(memory)Type(forward<Args>(args)...); // placement new

	return memory;
}

template<typename Type>
void xdelete(Type* obj)
{
	obj->~Type();
	x_release(obj);
}