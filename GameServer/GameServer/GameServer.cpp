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

using KnightRef = TSharedPtr<class Knight>;

class Knight : public RefCountable
{
public:
	Knight()
	{
		cout << "Knight()" << endl;
	}

	~Knight()
	{
		cout << "~Knight()" << endl;
	}

	void SetTarget(KnightRef target)
	{
		_target = target;
	}

	KnightRef _target = nullptr;
};

int main()
{
	// 1) 이미 만들어진 클래스 대상으로는 사용이 불가능하다.
	// 2) 순환 (Cycle) 문제

	KnightRef k1(new Knight);
	k1->ReleaseRef();	
	KnightRef k2(new Knight);
	k2->ReleaseRef();

	k1->SetTarget(k2);
	k2->SetTarget(k1);

	k1->SetTarget(nullptr);
	k2->SetTarget(nullptr);

	k1 = nullptr;
	k2 = nullptr;
}