#pragma once

#include "Types.h"
#include "CoreMacro.h"
#include "CoreGlobal.h"
#include "CoreTLS.h"
#include "Container.h"

#include <Windows.h>
#include <iostream>
using namespace std;

#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "Lock.h"
#include "ObjectPool.h"
#include "TypeCast.h"
#include "Memory.h"