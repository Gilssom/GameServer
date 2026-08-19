#include "auth/AuthStore.h"
#include "server/IocpServer.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::uint16_t port = 17777;
    std::string authMode = "memory";
    std::uint32_t workers = std::max(1u, std::thread::hardware_concurrency());
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.starts_with("--port=")) port = static_cast<std::uint16_t>(std::stoi(argument.substr(7)));
        else if (argument.starts_with("--workers=")) workers = static_cast<std::uint32_t>(std::stoul(argument.substr(10)));
        else if (argument.starts_with("--auth-mode=")) authMode = argument.substr(12);
    }

    std::string error;
    auto authStore = portfolio::auth::CreateAuthStore(authMode, error);
    if (!authStore)
    {
        std::cerr << "event=auth_store_init result=FAIL error=" << error << '\n';
        return 2;
    }
    portfolio::server::IocpServer server(std::move(authStore));
    return server.Run(port, workers) ? 0 : 3;
}
