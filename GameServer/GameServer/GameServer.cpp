#include "pch.h"
#include <iostream>
#include "CorePch.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <Windows.h>
#include <future>
#include "ThreadManager.h"

#include "RefCountable.h"
#include "Memory.h"
#include "Allocator.h"

class Player
{
public:
	Player() { }
	virtual ~Player() { }
};

class Knight : public Player
{
public:
	Knight()
	{
		cout << "Knight" << endl;
	}

	~Knight()
	{
		cout << "~Knight" << endl;
	}

	/*static void* operator new(size_t size)
	{
		cout << "knight new !" << size << endl;
		void* ptr = ::malloc(size);
		return ptr;
	}

	static void operator delete(void* ptr)
	{
		cout << "knight delete !" << endl;
		::free(ptr);
	}*/

	int _hp = 100;
	int _mp = 100;
};

int main()
{
	for (int32 i = 0; i < 5; i++)
	{
		GThreadManager->Launch([]()
			{
				while (true)
				{
					Vector<Knight> v(10);

					Map<int32, Knight> m;
					m[100] = Knight();

					this_thread::sleep_for(10ms);
				}
			});
	}

	GThreadManager->Join();
}