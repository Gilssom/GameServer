#pragma once

#include "AccountDB.h"
#include "PasswordHasher.h"

namespace AccountService
{
    enum class CreateResult
    {
        Success,
        AlreadyExists,
        InvalidAccountName,
        InvalidPassword,
        ReservedAccountName,
        HashFailed,
		DatabaseInsertError,
		DatabaseIdentityError,
    };

    inline bool IsValidAccountName(const String& accountName)
    {
        if (accountName.size() < 3 || accountName.size() > 50)
            return false;

        for (WCHAR ch : accountName)
        {
            const bool valid =
                (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'a' && ch <= L'z') ||
                (ch >= L'0' && ch <= L'9') ||
                ch == L'_';
            if (!valid)
                return false;
        }
        return true;
    }

    inline bool FindAccount(
        DBConnection& connection,
        const String& accountName,
        int64& accountId,
        WCHAR(&passwordHash)[256])
    {
        AccountDB::GetAccount getAccount(connection);
        getAccount.In_AccountName(accountName.c_str());
        getAccount.Out_AccountId(accountId);
        getAccount.Out_PasswordHash(passwordHash);
        return getAccount.Execute() && getAccount.Fetch();
    }

    inline CreateResult CreateAccount(
        DBConnection& connection,
        const String& accountName,
        const string& password,
        int64& accountId,
        bool allowReservedAdminName = false)
    {
        accountId = 0;
        if (!IsValidAccountName(accountName))
            return CreateResult::InvalidAccountName;
        if (password.size() < 8 || password.size() > 128)
            return CreateResult::InvalidPassword;
        if (!allowReservedAdminName &&
            ::_wcsicmp(accountName.c_str(), L"admin") == 0)
            return CreateResult::ReservedAccountName;

        WCHAR existingHash[256] = {};
        const bool exists = FindAccount(
            connection, accountName, accountId, existingHash);
        ::SecureZeroMemory(existingHash, sizeof(existingHash));
        if (exists)
            return CreateResult::AlreadyExists;

        String passwordHash;
        if (!PasswordHasher::Hash(password, passwordHash))
            return CreateResult::HashFailed;

        AccountDB::CreateAccount createAccount(connection);
        createAccount.In_AccountName(accountName.c_str());
        createAccount.In_PasswordHash(passwordHash.c_str());
        if (!createAccount.Execute())
        {
			const bool duplicateAccountName =
				connection.GetLastNativeError() == 1062;
            ::SecureZeroMemory(
                passwordHash.data(), passwordHash.size() * sizeof(WCHAR));
			return duplicateAccountName
				? CreateResult::AlreadyExists
				: CreateResult::DatabaseInsertError;
        }

        AccountDB::GetLastInsertId getLastInsertId(connection);
        getLastInsertId.Out_AccountId(accountId);
        const bool created = getLastInsertId.Execute() &&
            getLastInsertId.Fetch() && accountId > 0;
        ::SecureZeroMemory(
            passwordHash.data(), passwordHash.size() * sizeof(WCHAR));
		return created
			? CreateResult::Success
			: CreateResult::DatabaseIdentityError;
    }

    inline bool UpdatePassword(
        DBConnection& connection,
        int64 accountId,
        const string& password)
    {
        if (accountId <= 0 || password.size() < 8 || password.size() > 128)
            return false;

        String passwordHash;
        if (!PasswordHasher::Hash(password, passwordHash))
            return false;

        AccountDB::UpdatePasswordHash updatePassword(connection);
        updatePassword.In_PasswordHash(passwordHash.c_str());
        updatePassword.In_AccountId(accountId);
        const bool updated = updatePassword.Execute();
        ::SecureZeroMemory(
            passwordHash.data(), passwordHash.size() * sizeof(WCHAR));
        return updated;
    }
}
