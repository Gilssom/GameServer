#pragma once

#include "Session.h"

enum class DummyTestScenario
{
	SignUp,
	Login,
	E2E,
};

struct DummyTestConfig
{
	DummyTestScenario scenario = DummyTestScenario::E2E;
	string accountName;
	string password;
	string characterName;
};

void ConfigureDummyClientTest(DummyTestConfig config);
void BeginDummyClientTest(PacketSessionRef session);
void NotifyDummyClientDisconnected();
void TimeoutDummyClientTest();
void StopDummyClientTestSession();
bool IsDummyClientTestComplete();
bool DidDummyClientTestPass();
int32 GetDummyClientTestExitCode();
string GetDummyClientTestMessage();
