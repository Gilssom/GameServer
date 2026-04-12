#include "pch.h"
#include <iostream>
#include "CorePch.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <Windows.h>
#include <future>
#include "ThreadManager.h"

bool IsCheck(int n)
{
	if (n < 2)
		return false;

	for (int i = 2; i * i <= n; i++)
	{
		if (n % i == 0)
			return false;
	}

	return true;
}

int main()
{
	// Thread Count = 2
	// Thread A = 0 ~ 500000
	// Thread B = 500001 ~ 1000000

	unsigned int MAX_THREAD_COUNT = thread::hardware_concurrency();
	cout << "Max Thread Count : " << MAX_THREAD_COUNT << endl;
	const int MAX_NUMBER = 100'0000;
	const int NUMBER_HALF_COUNT = MAX_THREAD_COUNT - 5;
	cout << "Use Thread Count : " << NUMBER_HALF_COUNT << endl;
	const int MAX_NUMBER_HALF = MAX_NUMBER / NUMBER_HALF_COUNT;

	atomic<int> result = 0;

	const int64 beginTick = ::GetTickCount64();
	for (int i = 0; i < NUMBER_HALF_COUNT; i++)
	{
		GThreadManager->Launch([=, &result]
			{
				int localCount = 0;

				int start = MAX_NUMBER_HALF * i;
				int end = start + MAX_NUMBER_HALF;

				for (int j = start; j < end; j++)
				{
					if (IsCheck(j))
						localCount++;
				}

				result.fetch_add(localCount);
			});
	}

	GThreadManager->Join();

	const int64 endTick = ::GetTickCount64();
	cout << "Total Result : " << result.load() << endl;
	cout << "Elapsed Time : " << (endTick - beginTick) << "ms" << endl;
}