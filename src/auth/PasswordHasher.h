#pragma once

#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace PasswordHasher
{
    constexpr ULONG SALT_SIZE = 16;
    constexpr ULONG HASH_SIZE = 32;
    constexpr ULONGLONG ITERATIONS = 210000;
    constexpr WCHAR FORMAT_PREFIX[] = L"pbkdf2_sha256";

    inline WCHAR ToHexDigit(BYTE value)
    {
        return value < 10
            ? static_cast<WCHAR>(L'0' + value)
            : static_cast<WCHAR>(L'a' + value - 10);
    }

    inline String ToHex(const BYTE* data, size_t size)
    {
        String result(size * 2, L'0');
        for (size_t i = 0; i < size; ++i)
        {
            result[i * 2] = ToHexDigit(data[i] >> 4);
            result[i * 2 + 1] = ToHexDigit(data[i] & 0x0F);
        }
        return result;
    }

    inline int32 FromHexDigit(WCHAR value)
    {
        if (value >= L'0' && value <= L'9') return value - L'0';
        if (value >= L'a' && value <= L'f') return value - L'a' + 10;
        if (value >= L'A' && value <= L'F') return value - L'A' + 10;
        return -1;
    }

    template<size_t N>
    bool FromHex(const String& text, BYTE(&output)[N])
    {
        if (text.size() != N * 2)
            return false;

        for (size_t i = 0; i < N; ++i)
        {
            const int32 high = FromHexDigit(text[i * 2]);
            const int32 low = FromHexDigit(text[i * 2 + 1]);
            if (high < 0 || low < 0)
                return false;
            output[i] = static_cast<BYTE>((high << 4) | low);
        }
        return true;
    }

    inline bool Derive(
        const string& password,
        const BYTE* salt,
        ULONG saltSize,
        ULONGLONG iterations,
        BYTE* output,
        ULONG outputSize)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        const NTSTATUS openStatus = ::BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            BCRYPT_ALG_HANDLE_HMAC_FLAG);
        if (!BCRYPT_SUCCESS(openStatus))
            return false;

        const NTSTATUS deriveStatus = ::BCryptDeriveKeyPBKDF2(
            algorithm,
            reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
            static_cast<ULONG>(password.size()),
            const_cast<PUCHAR>(salt),
            saltSize,
            iterations,
            output,
            outputSize,
            0);
        ::BCryptCloseAlgorithmProvider(algorithm, 0);
        return BCRYPT_SUCCESS(deriveStatus);
    }

    inline bool Hash(const string& password, String& encoded)
    {
        if (password.empty())
            return false;

        BYTE salt[SALT_SIZE] = {};
        BYTE hash[HASH_SIZE] = {};
        const NTSTATUS randomStatus = ::BCryptGenRandom(
            nullptr, salt, SALT_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        if (!BCRYPT_SUCCESS(randomStatus) ||
            !Derive(password, salt, SALT_SIZE, ITERATIONS, hash, HASH_SIZE))
        {
            ::SecureZeroMemory(salt, sizeof(salt));
            ::SecureZeroMemory(hash, sizeof(hash));
            return false;
        }

        encoded = FORMAT_PREFIX;
        encoded += L"$" + std::to_wstring(ITERATIONS);
        encoded += L"$" + ToHex(salt, SALT_SIZE);
        encoded += L"$" + ToHex(hash, HASH_SIZE);
        ::SecureZeroMemory(salt, sizeof(salt));
        ::SecureZeroMemory(hash, sizeof(hash));
        return true;
    }

    inline bool Verify(const string& password, const String& encoded)
    {
        const size_t first = encoded.find(L'$');
        const size_t second = encoded.find(L'$', first + 1);
        const size_t third = encoded.find(L'$', second + 1);
        if (first == String::npos || second == String::npos ||
            third == String::npos || encoded.substr(0, first) != FORMAT_PREFIX)
            return false;

        ULONGLONG iterations = 0;
        for (size_t i = first + 1; i < second; ++i)
        {
            if (encoded[i] < L'0' || encoded[i] > L'9')
                return false;
            iterations = iterations * 10 + (encoded[i] - L'0');
        }
        if (iterations < 100000 || iterations > 1000000)
            return false;

        BYTE salt[SALT_SIZE] = {};
        BYTE expected[HASH_SIZE] = {};
        BYTE actual[HASH_SIZE] = {};
        const bool parsed =
            FromHex(encoded.substr(second + 1, third - second - 1), salt) &&
            FromHex(encoded.substr(third + 1), expected);
        const bool derived = parsed &&
            Derive(password, salt, SALT_SIZE, iterations, actual, HASH_SIZE);

        BYTE difference = 0;
        if (derived)
            for (size_t i = 0; i < HASH_SIZE; ++i)
                difference |= actual[i] ^ expected[i];

        ::SecureZeroMemory(salt, sizeof(salt));
        ::SecureZeroMemory(expected, sizeof(expected));
        ::SecureZeroMemory(actual, sizeof(actual));
        return derived && difference == 0;
    }
}
