#pragma once

#include "auth/AuthStore.h"

#include <WinSock2.h>
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace portfolio::server
{
    class IocpServer final
    {
    public:
        explicit IocpServer(std::unique_ptr<auth::IAuthStore> authStore);
        ~IocpServer();
        IocpServer(const IocpServer&) = delete;
        IocpServer& operator=(const IocpServer&) = delete;

        bool Run(std::uint16_t port, std::uint32_t workerCount);
        void Stop();

    private:
        struct Session;
        struct IoContext;

        void WorkerLoop();
        bool IssueReceive(const std::shared_ptr<Session>& session);
        bool QueueSend(const std::shared_ptr<Session>& session, std::vector<std::uint8_t> packet);
        bool ProcessReceive(const std::shared_ptr<Session>& session, const std::uint8_t* data, std::size_t size);
        bool DispatchPacket(const std::shared_ptr<Session>& session,
            std::uint16_t packetId, const std::vector<std::uint8_t>& payload);
        std::shared_ptr<Session> FindSession(SOCKET socket);
        void CloseSession(SOCKET socket, const char* event, int errorCode);

        std::unique_ptr<auth::IAuthStore> authStore_;
        HANDLE completionPort_ = nullptr;
        SOCKET listenSocket_ = INVALID_SOCKET;
        std::atomic_bool running_ = false;
        std::mutex sessionsMutex_;
        std::unordered_map<SOCKET, std::shared_ptr<Session>> sessions_;
        std::vector<std::thread> workers_;
        std::atomic_uint64_t nextSessionId_ = 1;
    };
}
