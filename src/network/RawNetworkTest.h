#pragma once

enum class RawNetworkCase
{
	Split2,
	Split5,
	Merge3,
	SizeZero,
	Oversize,
	PartialClose,
};

struct RawNetworkTestConfig
{
	RawNetworkCase testCase = RawNetworkCase::Split2;
	wstring host = L"127.0.0.1";
	uint16 port = 7777;
	uint32 timeoutSeconds = 5;
	string accountName;
	string password;
};

struct RawNetworkTestResult
{
	bool passed = false;
	int32 exitCode = 1;
	int64 durationMs = 0;
	string message;
};

bool TryParseRawNetworkCase(const wstring& value, RawNetworkCase& testCase);
const char* RawNetworkCaseName(RawNetworkCase testCase);
RawNetworkTestResult RunRawNetworkTest(RawNetworkTestConfig config);
