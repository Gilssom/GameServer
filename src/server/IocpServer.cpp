#include "server/IocpServer.h"
#include "common/Protocol.h"

#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <utility>

namespace portfolio::server
{
    namespace
    {
        enum class Operation { Receive, Send };

        void Log(const std::uint64_t sessionId, const char* event,
            const char* result, const int errorCode = 0)
        {
            std::cout << "session_id=" << sessionId << " event=" << event
                << " result=" << result << " error_code=" << errorCode << '\n';
        }
    }

    struct IocpServer::IoContext
    {
        OVERLAPPED overlapped{};
        Operation operation = Operation::Receive;
        WSABUF buffer{};
        std::array<std::uint8_t, 4096> receiveStorage{};
        std::vector<std::uint8_t> sendStorage;
        std::size_t sent = 0;
    };

    struct IocpServer::Session
    {
        SOCKET socket = INVALID_SOCKET;
        std::uint64_t id = 0;
        std::vector<std::uint8_t> pending;
        std::atomic_bool closing = false;
    };

    IocpServer::IocpServer(std::unique_ptr<auth::IAuthStore> authStore)
        : authStore_(std::move(authStore))
    {
    }

    IocpServer::~IocpServer() { Stop(); }

    bool IocpServer::Run(const std::uint16_t port, const std::uint32_t workerCount)
    {
        WSADATA winsock{};
        if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return false;

        completionPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, workerCount);
        if (completionPort_ == nullptr) return false;

        listenSocket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (listenSocket_ == INVALID_SOCKET) return false;

        BOOL reuse = TRUE;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
            return false;

        running_ = true;
        const std::uint32_t count = std::max<std::uint32_t>(1, workerCount);
        workers_.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
            workers_.emplace_back(&IocpServer::WorkerLoop, this);

        std::cout << "event=server_started result=PASS port=" << port
            << " workers=" << count << '\n';
        while (running_)
        {
            SOCKET client = accept(listenSocket_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                if (running_) std::cerr << "event=accept_failed error_code=" << WSAGetLastError() << '\n';
                break;
            }

            auto session = std::make_shared<Session>();
            session->socket = client;
            session->id = nextSessionId_.fetch_add(1);
            {
                std::scoped_lock lock(sessionsMutex_);
                sessions_.emplace(client, session);
            }
            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(client), completionPort_,
                    static_cast<ULONG_PTR>(client), 0) == nullptr || !IssueReceive(session))
            {
                CloseSession(client, "session_setup_failed", WSAGetLastError());
                continue;
            }
            Log(session->id, "session_connected", "PASS");
        }
        Stop();
        return true;
    }

    void IocpServer::Stop()
    {
        if (!running_.exchange(false) && completionPort_ == nullptr) return;
        if (listenSocket_ != INVALID_SOCKET)
        {
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
        }

        std::vector<SOCKET> sockets;
        {
            std::scoped_lock lock(sessionsMutex_);
            for (const auto& [socket, session] : sessions_) sockets.push_back(socket);
        }
        for (const SOCKET socket : sockets) CloseSession(socket, "server_stop", 0);
        for (std::size_t index = 0; index < workers_.size(); ++index)
            PostQueuedCompletionStatus(completionPort_, 0, 0, nullptr);
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
        workers_.clear();
        if (completionPort_ != nullptr)
        {
            CloseHandle(completionPort_);
            completionPort_ = nullptr;
        }
        WSACleanup();
    }

    void IocpServer::WorkerLoop()
    {
        while (true)
        {
            DWORD transferred = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL completed = GetQueuedCompletionStatus(
                completionPort_, &transferred, &key, &overlapped, INFINITE);
            const int completionError = completed ? 0 : static_cast<int>(GetLastError());
            if (overlapped == nullptr)
            {
                if (!running_) return;
                continue;
            }

            std::unique_ptr<IoContext> context(reinterpret_cast<IoContext*>(overlapped));
            const SOCKET socket = static_cast<SOCKET>(key);
            auto session = FindSession(socket);
            if (!session) continue;
            if (!completed || transferred == 0)
            {
                CloseSession(socket, transferred == 0 ? "peer_closed" : "io_failed", completionError);
                continue;
            }

            if (context->operation == Operation::Receive)
            {
                if (ProcessReceive(session, context->receiveStorage.data(), transferred))
                    IssueReceive(session);
                continue;
            }

            context->sent += transferred;
            if (context->sent < context->sendStorage.size())
            {
                ZeroMemory(&context->overlapped, sizeof(context->overlapped));
                context->buffer.buf = reinterpret_cast<char*>(context->sendStorage.data() + context->sent);
                context->buffer.len = static_cast<ULONG>(context->sendStorage.size() - context->sent);
                DWORD sent = 0;
                if (WSASend(socket, &context->buffer, 1, &sent, 0,
                        &context->overlapped, nullptr) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
                {
                    CloseSession(socket, "send_failed", WSAGetLastError());
                    continue;
                }
                context.release();
            }
        }
    }

    bool IocpServer::IssueReceive(const std::shared_ptr<Session>& session)
    {
        if (session->closing) return false;
        auto context = std::make_unique<IoContext>();
        context->operation = Operation::Receive;
        context->buffer.buf = reinterpret_cast<char*>(context->receiveStorage.data());
        context->buffer.len = static_cast<ULONG>(context->receiveStorage.size());
        DWORD flags = 0;
        DWORD received = 0;
        const int result = WSARecv(session->socket, &context->buffer, 1, &received,
            &flags, &context->overlapped, nullptr);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) return false;
        context.release();
        return true;
    }

    bool IocpServer::QueueSend(const std::shared_ptr<Session>& session, std::vector<std::uint8_t> packet)
    {
        if (session->closing) return false;
        auto context = std::make_unique<IoContext>();
        context->operation = Operation::Send;
        context->sendStorage = std::move(packet);
        context->buffer.buf = reinterpret_cast<char*>(context->sendStorage.data());
        context->buffer.len = static_cast<ULONG>(context->sendStorage.size());
        DWORD sent = 0;
        if (WSASend(session->socket, &context->buffer, 1, &sent, 0,
                &context->overlapped, nullptr) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            return false;
        context.release();
        return true;
    }

    bool IocpServer::ProcessReceive(const std::shared_ptr<Session>& session,
        const std::uint8_t* data, const std::size_t size)
    {
        session->pending.insert(session->pending.end(), data, data + size);
        while (session->pending.size() >= protocol::kHeaderSize)
        {
            const auto header = protocol::ReadHeader(session->pending);
            if (header.size < protocol::kHeaderSize || header.size > protocol::kMaxPacketSize)
            {
                Log(session->id, header.size < protocol::kHeaderSize ? "invalid_header" : "packet_too_large",
                    "REJECT", header.size);
                CloseSession(session->socket, "invalid_packet", header.size);
                return false;
            }
            if (session->pending.size() < header.size)
            {
                Log(session->id, "wait_more_data", "PASS");
                break;
            }
            std::vector<std::uint8_t> payload(session->pending.begin() + protocol::kHeaderSize,
                session->pending.begin() + header.size);
            session->pending.erase(session->pending.begin(), session->pending.begin() + header.size);
            if (!DispatchPacket(session, header.id, payload))
            {
                CloseSession(session->socket, "dispatch_rejected", header.id);
                return false;
            }
        }
        return true;
    }

    bool IocpServer::DispatchPacket(const std::shared_ptr<Session>& session,
        const std::uint16_t packetId, const std::vector<std::uint8_t>& payload)
    {
        if (packetId == static_cast<std::uint16_t>(protocol::PacketId::PingRequest))
            return QueueSend(session, protocol::MakePacket(protocol::PacketId::PingResponse, payload));

        if (packetId != static_cast<std::uint16_t>(protocol::PacketId::LoginRequest)) return false;
        std::size_t offset = 0;
        std::string account;
        std::string password;
        if (!protocol::ReadString(payload, offset, account) ||
            !protocol::ReadString(payload, offset, password) || offset != payload.size())
            return false;

        const bool verified = authStore_->Verify(account, password);
        SecureZeroMemory(password.data(), password.size());
        std::vector<std::uint8_t> response{static_cast<std::uint8_t>(verified ? 1 : 0)};
        protocol::AppendString(response, verified ? "authenticated" : "invalid credentials");
        Log(session->id, "login", verified ? "PASS" : "REJECT");
        return QueueSend(session, protocol::MakePacket(protocol::PacketId::LoginResponse, response));
    }

    std::shared_ptr<IocpServer::Session> IocpServer::FindSession(const SOCKET socket)
    {
        std::scoped_lock lock(sessionsMutex_);
        const auto iterator = sessions_.find(socket);
        return iterator == sessions_.end() ? nullptr : iterator->second;
    }

    void IocpServer::CloseSession(const SOCKET socket, const char* event, const int errorCode)
    {
        std::shared_ptr<Session> session;
        {
            std::scoped_lock lock(sessionsMutex_);
            const auto iterator = sessions_.find(socket);
            if (iterator == sessions_.end()) return;
            session = iterator->second;
            sessions_.erase(iterator);
        }
        if (session->closing.exchange(true)) return;
        CancelIoEx(reinterpret_cast<HANDLE>(socket), nullptr);
        shutdown(socket, SD_BOTH);
        closesocket(socket);
        const bool expectedClose = std::strcmp(event, "peer_closed") == 0 ||
            std::strcmp(event, "server_stop") == 0;
        Log(session->id, event, expectedClose ? "PASS" : "CLOSED", errorCode);
    }
}
