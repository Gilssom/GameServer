#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace portfolio::auth
{
    class IAuthStore
    {
    public:
        virtual ~IAuthStore() = default;
        virtual bool Verify(std::string_view account, std::string_view password) = 0;
    };

    std::unique_ptr<IAuthStore> CreateAuthStore(std::string_view mode, std::string& error);
}
