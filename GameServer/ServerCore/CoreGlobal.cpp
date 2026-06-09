#include "pch.h"
#include "CoreGlobal.h"
#include "ThreadManager.h"
#include "Memory.h"
#include "DeadLockProfiler.h"
#include "SocketUtils.h"
#include "SendBuffer.h"

ThreadManager*		GThreadManager = nullptr;
Memory*				GMemory = nullptr;
DeadLockProfiler*	GDeadLockProfiler = nullptr;
SendBufferManager*	GSendBufferManager = nullptr;

class CoreGlobal
{
public:
	CoreGlobal()
	{
		GThreadManager = new ThreadManager();
		GMemory = new Memory();
		GDeadLockProfiler = new DeadLockProfiler();
		GSendBufferManager = new SendBufferManager();
		SocketUtils::Init();
	}

	~CoreGlobal()
	{
		delete GThreadManager;
		delete GMemory;
		delete GDeadLockProfiler;
		delete GSendBufferManager;
		SocketUtils::Clear();
	}
} GCoreGlobal;