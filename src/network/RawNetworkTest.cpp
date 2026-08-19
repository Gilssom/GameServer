#include "pch.h"
#include "RawNetworkTest.h"
#include "ServerPacketHandler.h"

#include <chrono>

namespace
{
	using Clock = std::chrono::steady_clock;

	void ClearPassword(string& password)
	{
		if (!password.empty())
		{
			::SecureZeroMemory(&password[0], password.size());
			password.clear();
		}
	}

	vector<BYTE> MakeLoginPacket(const string& accountName, string& password)
	{
		Protocol::C_LOGIN packet;
		packet.set_account_name(accountName);
		packet.set_password(password);

		const uint16 payloadSize = static_cast<uint16>(packet.ByteSizeLong());
		const uint16 packetSize = payloadSize + sizeof(PacketHeader);
		vector<BYTE> bytes(packetSize);
		PacketHeader* header = reinterpret_cast<PacketHeader*>(bytes.data());
		header->size = packetSize;
		header->id = PKT_C_LOGIN;
		packet.SerializeToArray(bytes.data() + sizeof(PacketHeader), payloadSize);

		string* packetPassword = packet.mutable_password();
		if (!packetPassword->empty())
			::SecureZeroMemory(&(*packetPassword)[0], packetPassword->size());
		packet.clear_password();
		return bytes;
	}

	bool SendAll(SOCKET socket, const BYTE* data, int32 size)
	{
		int32 sent = 0;
		while (sent < size)
		{
			const int32 result = ::send(
				socket,
				reinterpret_cast<const char*>(data + sent),
				size - sent,
				0);
			if (result == SOCKET_ERROR || result == 0)
				return false;
			sent += result;
		}
		return true;
	}

	bool ConnectSocket(
		const RawNetworkTestConfig& config,
		SOCKET& socket,
		string& errorMessage)
	{
		socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socket == INVALID_SOCKET)
		{
			errorMessage = "socket creation failed";
			return false;
		}

		const int32 timeoutMs = static_cast<int32>(config.timeoutSeconds * 1000);
		::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
		::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
			reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

		SOCKADDR_IN address = {};
		address.sin_family = AF_INET;
		address.sin_port = ::htons(config.port);
		if (::InetPtonW(AF_INET, config.host.c_str(), &address.sin_addr) != 1)
		{
			errorMessage = "invalid IPv4 address";
			return false;
		}

		if (::connect(
			socket,
			reinterpret_cast<SOCKADDR*>(&address),
			sizeof(address)) == SOCKET_ERROR)
		{
			errorMessage = "connect failed: " + std::to_string(::WSAGetLastError());
			return false;
		}

		return true;
	}

	RawNetworkTestResult Finish(
		const Clock::time_point& startedAt,
		bool passed,
		int32 exitCode,
		const string& message)
	{
		RawNetworkTestResult result;
		result.passed = passed;
		result.exitCode = exitCode;
		result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			Clock::now() - startedAt).count();
		result.message = message;
		cout << (passed ? "[PASS] " : "[FAIL] ") << message << endl;
		return result;
	}

	RawNetworkTestResult WaitForResponse(
		SOCKET socket,
		const Clock::time_point& startedAt,
		const string& successMessage)
	{
		char buffer[4096] = {};
		const int32 received = ::recv(socket, buffer, sizeof(buffer), 0);
		if (received > 0)
			return Finish(startedAt, true, 0, successMessage);

		if (received == 0)
			return Finish(startedAt, false, 1, "server closed before a response");

		const int32 errorCode = ::WSAGetLastError();
		return Finish(
			startedAt,
			false,
			errorCode == WSAETIMEDOUT ? 3 : 1,
			"response receive failed: " + std::to_string(errorCode));
	}

	RawNetworkTestResult WaitForServerClose(
		SOCKET socket,
		const Clock::time_point& startedAt,
		const string& successMessage)
	{
		char buffer[256] = {};
		const int32 received = ::recv(socket, buffer, sizeof(buffer), 0);
		if (received == 0)
			return Finish(startedAt, true, 0, successMessage);

		if (received > 0)
			return Finish(startedAt, false, 1, "unexpected server payload");

		const int32 errorCode = ::WSAGetLastError();
		return Finish(
			startedAt,
			false,
			errorCode == WSAETIMEDOUT ? 3 : 1,
			"server did not close as expected: " + std::to_string(errorCode));
	}
}

bool TryParseRawNetworkCase(const wstring& value, RawNetworkCase& testCase)
{
	if (::_wcsicmp(value.c_str(), L"split-2") == 0) testCase = RawNetworkCase::Split2;
	else if (::_wcsicmp(value.c_str(), L"split-5") == 0) testCase = RawNetworkCase::Split5;
	else if (::_wcsicmp(value.c_str(), L"merge-3") == 0) testCase = RawNetworkCase::Merge3;
	else if (::_wcsicmp(value.c_str(), L"size-zero") == 0) testCase = RawNetworkCase::SizeZero;
	else if (::_wcsicmp(value.c_str(), L"oversize") == 0) testCase = RawNetworkCase::Oversize;
	else if (::_wcsicmp(value.c_str(), L"partial-close") == 0) testCase = RawNetworkCase::PartialClose;
	else return false;
	return true;
}

const char* RawNetworkCaseName(RawNetworkCase testCase)
{
	switch (testCase)
	{
	case RawNetworkCase::Split2: return "split-2";
	case RawNetworkCase::Split5: return "split-5";
	case RawNetworkCase::Merge3: return "merge-3";
	case RawNetworkCase::SizeZero: return "size-zero";
	case RawNetworkCase::Oversize: return "oversize";
	case RawNetworkCase::PartialClose: return "partial-close";
	default: return "unknown";
	}
}

RawNetworkTestResult RunRawNetworkTest(RawNetworkTestConfig config)
{
	const auto startedAt = Clock::now();
	cout << "[CONFIG] network_case=" << RawNetworkCaseName(config.testCase)
		<< " port=" << config.port
		<< " timeout=" << config.timeoutSeconds << "s" << endl;

	SOCKET socket = INVALID_SOCKET;
	string errorMessage;
	if (!ConnectSocket(config, socket, errorMessage))
	{
		if (socket != INVALID_SOCKET)
			::closesocket(socket);
		ClearPassword(config.password);
		return Finish(startedAt, false, 1, errorMessage);
	}

	vector<BYTE> packet = MakeLoginPacket(config.accountName, config.password);
	ClearPassword(config.password);
	bool sent = false;

	switch (config.testCase)
	{
	case RawNetworkCase::Split2:
		sent = SendAll(socket, packet.data(), sizeof(PacketHeader));
		this_thread::sleep_for(50ms);
		sent = sent && SendAll(
			socket,
			packet.data() + sizeof(PacketHeader),
			static_cast<int32>(packet.size() - sizeof(PacketHeader)));
		cout << "[STEP] packet sent in 2 chunks." << endl;
		break;

	case RawNetworkCase::Split5:
	{
		const int32 payloadRemaining = static_cast<int32>(packet.size()) - 4;
		const int32 fourthSize = payloadRemaining / 2;
		const int32 chunkSizes[5] = { 1, 1, 2, fourthSize, payloadRemaining - fourthSize };
		int32 offset = 0;
		sent = true;
		for (const int32 chunkSize : chunkSizes)
		{
			sent = sent && SendAll(socket, packet.data() + offset, chunkSize);
			offset += chunkSize;
			this_thread::sleep_for(30ms);
		}
		cout << "[STEP] packet sent in 5 chunks." << endl;
		break;
	}

	case RawNetworkCase::Merge3:
	{
		vector<BYTE> merged(packet.size() * 3);
		for (int32 i = 0; i < 3; ++i)
			::memcpy(merged.data() + (packet.size() * i), packet.data(), packet.size());
		sent = SendAll(socket, merged.data(), static_cast<int32>(merged.size()));
		cout << "[STEP] 3 packets sent in one buffer." << endl;
		break;
	}

	case RawNetworkCase::SizeZero:
	{
		PacketHeader header = { 0, PKT_C_LOGIN };
		sent = SendAll(socket, reinterpret_cast<BYTE*>(&header), sizeof(header));
		cout << "[STEP] size-zero header sent." << endl;
		break;
	}

	case RawNetworkCase::Oversize:
	{
		PacketHeader header = {
			static_cast<uint16>(MAX_PACKET_SIZE + 1),
			PKT_C_LOGIN };
		sent = SendAll(socket, reinterpret_cast<BYTE*>(&header), sizeof(header));
		cout << "[STEP] oversize header sent. declared_size=" << header.size << endl;
		break;
	}

	case RawNetworkCase::PartialClose:
	{
		const int32 partialSize = sizeof(PacketHeader) +
			static_cast<int32>((packet.size() - sizeof(PacketHeader)) / 2);
		sent = SendAll(socket, packet.data(), partialSize);
		cout << "[STEP] partial payload sent. bytes=" << partialSize << endl;
		::shutdown(socket, SD_BOTH);
		::closesocket(socket);
		socket = INVALID_SOCKET;
		if (!sent)
			return Finish(startedAt, false, 1, "partial payload send failed");
		return Finish(startedAt, true, 0, "partial-close injection completed");
	}
	}

	if (!sent)
	{
		::closesocket(socket);
		return Finish(startedAt, false, 1, "raw packet send failed");
	}

	RawNetworkTestResult result;
	if (config.testCase == RawNetworkCase::SizeZero ||
		config.testCase == RawNetworkCase::Oversize)
	{
		result = WaitForServerClose(socket, startedAt, "invalid header rejected by server");
	}
	else
	{
		result = WaitForResponse(socket, startedAt, "framing case received a server response");
	}

	::shutdown(socket, SD_BOTH);
	::closesocket(socket);
	return result;
}
