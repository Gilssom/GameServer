#include "common/Protocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using portfolio::protocol::PacketId;

    bool SendAll(const SOCKET socket, const std::span<const std::uint8_t> data)
    {
        std::size_t offset = 0;
        while (offset < data.size())
        {
            const int sent = send(socket, reinterpret_cast<const char*>(data.data() + offset),
                static_cast<int>(data.size() - offset), 0);
            if (sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }

    bool ReceiveExact(const SOCKET socket, std::uint8_t* output, const std::size_t size)
    {
        std::size_t offset = 0;
        while (offset < size)
        {
            const int received = recv(socket, reinterpret_cast<char*>(output + offset),
                static_cast<int>(size - offset), 0);
            if (received <= 0) return false;
            offset += static_cast<std::size_t>(received);
        }
        return true;
    }

    bool ReceivePacket(const SOCKET socket, std::vector<std::uint8_t>& packet)
    {
        std::array<std::uint8_t, portfolio::protocol::kHeaderSize> headerBytes{};
        if (!ReceiveExact(socket, headerBytes.data(), headerBytes.size())) return false;
        const auto header = portfolio::protocol::ReadHeader(headerBytes);
        if (header.size < portfolio::protocol::kHeaderSize ||
            header.size > portfolio::protocol::kMaxPacketSize) return false;
        packet.assign(headerBytes.begin(), headerBytes.end());
        packet.resize(header.size);
        return ReceiveExact(socket, packet.data() + headerBytes.size(), header.size - headerBytes.size());
    }

    SOCKET Connect(const std::string& host, const std::uint16_t port)
    {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) return INVALID_SOCKET;
        DWORD timeout = 3000;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
            connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(socket);
            return INVALID_SOCKET;
        }
        return socket;
    }

    bool SendChunks(const SOCKET socket, const std::vector<std::uint8_t>& packet, const int chunks)
    {
        std::size_t offset = 0;
        for (int index = 0; index < chunks; ++index)
        {
            const std::size_t remaining = packet.size() - offset;
            const std::size_t count = index == chunks - 1
                ? remaining : std::max<std::size_t>(1, remaining / static_cast<std::size_t>(chunks - index));
            if (!SendAll(socket, std::span(packet).subspan(offset, count))) return false;
            offset += count;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        return true;
    }

    bool ValidateLogin(const SOCKET socket)
    {
        std::vector<std::uint8_t> response;
        if (!ReceivePacket(socket, response)) return false;
        const auto header = portfolio::protocol::ReadHeader(response);
        return header.id == static_cast<std::uint16_t>(PacketId::LoginResponse) &&
            response.size() > portfolio::protocol::kHeaderSize &&
            response[portfolio::protocol::kHeaderSize] == 1;
    }
}

int main(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 17777;
    std::string account;
    std::string password;
    std::string scenario = "e2e";
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.starts_with("--host=")) host = argument.substr(7);
        else if (argument.starts_with("--port=")) port = static_cast<std::uint16_t>(std::stoi(argument.substr(7)));
        else if (argument.starts_with("--account=")) account = argument.substr(10);
        else if (argument.starts_with("--password=")) password = argument.substr(11);
        else if (argument.starts_with("--scenario=")) scenario = argument.substr(11);
    }

    if (password.empty())
    {
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "PORTFOLIO_DEMO_PASSWORD") == 0 && value != nullptr)
        {
            password.assign(value);
            SecureZeroMemory(value, length);
            std::free(value);
        }
    }

    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 2;
    const auto started = std::chrono::steady_clock::now();
    SOCKET socket = Connect(host, port);
    bool passed = socket != INVALID_SOCKET;

    if (passed && (scenario == "e2e" || scenario == "split2" || scenario == "split5"))
    {
        auto packet = portfolio::protocol::MakeLoginRequest(account, password);
        if (!password.empty()) SecureZeroMemory(password.data(), password.size());
        passed = scenario == "e2e" ? SendAll(socket, packet) :
            SendChunks(socket, packet, scenario == "split2" ? 2 : 5);
        passed = passed && ValidateLogin(socket);
    }
    else if (passed && scenario == "merge3")
    {
        const std::vector<std::uint8_t> payload{0x42};
        const auto ping = portfolio::protocol::MakePacket(PacketId::PingRequest, payload);
        std::vector<std::uint8_t> merged;
        for (int index = 0; index < 3; ++index) merged.insert(merged.end(), ping.begin(), ping.end());
        passed = SendAll(socket, merged);
        for (int index = 0; passed && index < 3; ++index)
        {
            std::vector<std::uint8_t> response;
            passed = ReceivePacket(socket, response) &&
                portfolio::protocol::ReadHeader(response).id == static_cast<std::uint16_t>(PacketId::PingResponse);
        }
    }
    else if (passed && (scenario == "sizezero" || scenario == "oversize"))
    {
        std::vector<std::uint8_t> header;
        portfolio::protocol::AppendU16(header, scenario == "sizezero" ? 0 :
            static_cast<std::uint16_t>(portfolio::protocol::kMaxPacketSize + 1));
        portfolio::protocol::AppendU16(header, static_cast<std::uint16_t>(PacketId::LoginRequest));
        passed = SendAll(socket, header);
        std::uint8_t ignored = 0;
        passed = passed && recv(socket, reinterpret_cast<char*>(&ignored), 1, 0) <= 0;
    }
    else if (passed && scenario == "partial-close")
    {
        auto packet = portfolio::protocol::MakeLoginRequest(account, password);
        if (!password.empty()) SecureZeroMemory(password.data(), password.size());
        passed = SendAll(socket, std::span(packet).first(packet.size() / 2));
        shutdown(socket, SD_SEND);
    }
    else if (passed)
    {
        passed = false;
    }

    if (socket != INVALID_SOCKET) closesocket(socket);
    WSACleanup();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "{\"result\":\"" << (passed ? "PASS" : "FAIL")
        << "\",\"scenario\":\"" << scenario << "\",\"duration_ms\":" << duration
        << ",\"exit_code\":" << (passed ? 0 : 1) << "}\n";
    return passed ? 0 : 1;
}
