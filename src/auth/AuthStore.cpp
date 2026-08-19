#include "auth/AuthStore.h"
#include "auth/PasswordHasher.h"

#include <Windows.h>
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

namespace portfolio::auth
{
    namespace
    {
        std::string GetEnvironment(const char* name)
        {
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
                return {};
            std::string result(value);
            SecureZeroMemory(value, length);
            std::free(value);
            return result;
        }

        std::wstring ToWide(const std::string_view value)
        {
            if (value.empty()) return {};
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (length <= 0) return {};
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), result.data(), length);
            return result;
        }

        class MemoryAuthStore final : public IAuthStore
        {
        public:
            MemoryAuthStore(std::string account, const std::string_view password)
                : account_(std::move(account))
            {
                PasswordHasher::Hash(password, passwordHash_);
            }

            bool IsReady() const { return !account_.empty() && !passwordHash_.empty(); }

            bool Verify(const std::string_view account, const std::string_view password) override
            {
                const bool passwordMatches = PasswordHasher::Verify(password, passwordHash_);
                return account == account_ && passwordMatches;
            }

            ~MemoryAuthStore() override
            {
                if (!passwordHash_.empty())
                    SecureZeroMemory(passwordHash_.data(), passwordHash_.size() * sizeof(wchar_t));
            }

        private:
            std::string account_;
            std::wstring passwordHash_;
        };

        class OdbcAuthStore final : public IAuthStore
        {
        public:
            explicit OdbcAuthStore(const std::wstring_view connectionString)
            {
                if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment_)))
                    return;
                if (!SQL_SUCCEEDED(SQLSetEnvAttr(environment_, SQL_ATTR_ODBC_VERSION,
                    reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0)) ||
                    !SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, environment_, &connection_)))
                    return;

                std::wstring mutableConnection(connectionString);
                SQLWCHAR output[1024]{};
                SQLSMALLINT outputLength = 0;
                ready_ = SQL_SUCCEEDED(SQLDriverConnectW(connection_, nullptr,
                    reinterpret_cast<SQLWCHAR*>(mutableConnection.data()), SQL_NTS,
                    output, static_cast<SQLSMALLINT>(std::size(output)), &outputLength,
                    SQL_DRIVER_NOPROMPT));
                if (ready_) ready_ = PasswordHasher::Hash("timing-only-dummy-password", dummyHash_);
                SecureZeroMemory(mutableConnection.data(), mutableConnection.size() * sizeof(wchar_t));
                SecureZeroMemory(output, sizeof(output));
            }

            ~OdbcAuthStore() override
            {
                if (connection_ != SQL_NULL_HDBC)
                {
                    SQLDisconnect(connection_);
                    SQLFreeHandle(SQL_HANDLE_DBC, connection_);
                }
                if (environment_ != SQL_NULL_HENV)
                    SQLFreeHandle(SQL_HANDLE_ENV, environment_);
                if (!dummyHash_.empty())
                    SecureZeroMemory(dummyHash_.data(), dummyHash_.size() * sizeof(wchar_t));
            }

            bool IsReady() const { return ready_; }

            bool Verify(const std::string_view account, const std::string_view password) override
            {
                std::scoped_lock lock(mutex_);
                const std::wstring wideAccount = ToWide(account);
                if (!ready_ || wideAccount.empty()) return false;

                SQLHSTMT statement = SQL_NULL_HSTMT;
                if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, connection_, &statement)))
                    return false;

                constexpr wchar_t query[] =
                    L"SELECT PasswordHash FROM Account WHERE AccountName = ? LIMIT 1";
                bool accountFound = false;
                std::wstring encoded = dummyHash_;
                if (SQL_SUCCEEDED(SQLPrepareW(statement,
                        reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(query)), SQL_NTS)))
                {
                    SQLLEN accountLength = static_cast<SQLLEN>(wideAccount.size() * sizeof(wchar_t));
                    if (SQL_SUCCEEDED(SQLBindParameter(statement, 1, SQL_PARAM_INPUT,
                            SQL_C_WCHAR, SQL_WVARCHAR, 50, 0,
                            const_cast<wchar_t*>(wideAccount.data()),
                            static_cast<SQLLEN>((wideAccount.size() + 1) * sizeof(wchar_t)),
                            &accountLength)) && SQL_SUCCEEDED(SQLExecute(statement)) &&
                        SQL_SUCCEEDED(SQLFetch(statement)))
                    {
                        wchar_t encodedBuffer[256]{};
                        SQLLEN indicator = 0;
                        if (SQL_SUCCEEDED(SQLGetData(statement, 1, SQL_C_WCHAR,
                                encodedBuffer, sizeof(encodedBuffer), &indicator)) && indicator != SQL_NULL_DATA)
                        {
                            accountFound = true;
                            const std::size_t characterCount = indicator > 0
                                ? std::min<std::size_t>(static_cast<std::size_t>(indicator) / sizeof(wchar_t),
                                    std::size(encodedBuffer) - 1)
                                : wcsnlen_s(encodedBuffer, std::size(encodedBuffer));
                            encoded.assign(encodedBuffer, characterCount);
                            SecureZeroMemory(encodedBuffer, sizeof(encodedBuffer));
                        }
                    }
                }
                SQLFreeHandle(SQL_HANDLE_STMT, statement);
                const bool passwordMatches = PasswordHasher::Verify(password, encoded);
                if (!encoded.empty())
                    SecureZeroMemory(encoded.data(), encoded.size() * sizeof(wchar_t));
                return accountFound && passwordMatches;
            }

        private:
            SQLHENV environment_ = SQL_NULL_HENV;
            SQLHDBC connection_ = SQL_NULL_HDBC;
            bool ready_ = false;
            std::mutex mutex_;
            std::wstring dummyHash_;
        };
    }

    std::unique_ptr<IAuthStore> CreateAuthStore(const std::string_view mode, std::string& error)
    {
        if (mode == "memory")
        {
            std::string account = GetEnvironment("PORTFOLIO_DEMO_ACCOUNT");
            std::string password = GetEnvironment("PORTFOLIO_DEMO_PASSWORD");
            if (account.empty() || password.empty())
            {
                error = "PORTFOLIO_DEMO_ACCOUNT and PORTFOLIO_DEMO_PASSWORD are required";
                return nullptr;
            }
            auto store = std::make_unique<MemoryAuthStore>(std::move(account), password);
            SecureZeroMemory(password.data(), password.size());
            if (!store->IsReady())
            {
                error = "failed to initialize password hash";
                return nullptr;
            }
            return store;
        }

        if (mode == "odbc")
        {
            std::string connection = GetEnvironment("GAMESERVER_ODBC_CONNECTION");
            std::wstring wideConnection = ToWide(connection);
            SecureZeroMemory(connection.data(), connection.size());
            if (wideConnection.empty())
            {
                error = "GAMESERVER_ODBC_CONNECTION is required";
                return nullptr;
            }
            auto store = std::make_unique<OdbcAuthStore>(wideConnection);
            SecureZeroMemory(wideConnection.data(), wideConnection.size() * sizeof(wchar_t));
            if (!store->IsReady())
            {
                error = "ODBC connection failed";
                return nullptr;
            }
            return store;
        }

        error = "auth mode must be memory or odbc";
        return nullptr;
    }
}
