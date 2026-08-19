#pragma once

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace portfolio::auth
{
    class PasswordHasher final
    {
    public:
        static constexpr std::uint64_t kIterations = 210000;
        static constexpr std::size_t kSaltSize = 16;
        static constexpr std::size_t kHashSize = 32;

        static bool Hash(const std::string_view password, std::wstring& encoded)
        {
            std::array<std::uint8_t, kSaltSize> salt{};
            std::array<std::uint8_t, kHashSize> hash{};
            if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt.data(),
                static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
                !Derive(password, salt, hash))
            {
                SecureZeroMemory(hash.data(), hash.size());
                return false;
            }
            encoded = L"pbkdf2_sha256$" + std::to_wstring(kIterations) + L"$" +
                ToHex(salt) + L"$" + ToHex(hash);
            SecureZeroMemory(hash.data(), hash.size());
            return true;
        }

        static bool Verify(const std::string_view password, const std::wstring_view encoded)
        {
            const auto first = encoded.find(L'$');
            const auto second = encoded.find(L'$', first + 1);
            const auto third = encoded.find(L'$', second + 1);
            if (first == std::wstring_view::npos || second == std::wstring_view::npos ||
                third == std::wstring_view::npos || encoded.substr(0, first) != L"pbkdf2_sha256" ||
                encoded.substr(first + 1, second - first - 1) != std::to_wstring(kIterations))
                return false;

            std::array<std::uint8_t, kSaltSize> salt{};
            std::array<std::uint8_t, kHashSize> expected{};
            std::array<std::uint8_t, kHashSize> actual{};
            if (!FromHex(encoded.substr(second + 1, third - second - 1), salt) ||
                !FromHex(encoded.substr(third + 1), expected) || !Derive(password, salt, actual))
                return false;

            std::uint8_t difference = 0;
            for (std::size_t index = 0; index < actual.size(); ++index)
                difference |= static_cast<std::uint8_t>(actual[index] ^ expected[index]);
            SecureZeroMemory(actual.data(), actual.size());
            SecureZeroMemory(expected.data(), expected.size());
            return difference == 0;
        }

    private:
        template<std::size_t Size>
        static bool Derive(const std::string_view password,
            const std::array<std::uint8_t, kSaltSize>& salt,
            std::array<std::uint8_t, Size>& output)
        {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
                return false;
            const NTSTATUS status = BCryptDeriveKeyPBKDF2(algorithm,
                reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                static_cast<ULONG>(password.size()), const_cast<PUCHAR>(salt.data()),
                static_cast<ULONG>(salt.size()), kIterations, output.data(),
                static_cast<ULONG>(output.size()), 0);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return BCRYPT_SUCCESS(status);
        }

        template<std::size_t Size>
        static std::wstring ToHex(const std::array<std::uint8_t, Size>& input)
        {
            constexpr wchar_t digits[] = L"0123456789abcdef";
            std::wstring output(Size * 2, L'0');
            for (std::size_t index = 0; index < Size; ++index)
            {
                output[index * 2] = digits[input[index] >> 4];
                output[index * 2 + 1] = digits[input[index] & 0x0f];
            }
            return output;
        }

        template<std::size_t Size>
        static bool FromHex(const std::wstring_view input, std::array<std::uint8_t, Size>& output)
        {
            if (input.size() != Size * 2) return false;
            const auto value = [](const wchar_t ch) -> int
            {
                if (ch >= L'0' && ch <= L'9') return ch - L'0';
                if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
                if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
                return -1;
            };
            for (std::size_t index = 0; index < Size; ++index)
            {
                const int high = value(input[index * 2]);
                const int low = value(input[index * 2 + 1]);
                if (high < 0 || low < 0) return false;
                output[index] = static_cast<std::uint8_t>((high << 4) | low);
            }
            return true;
        }
    };
}
