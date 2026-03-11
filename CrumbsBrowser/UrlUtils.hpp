#pragma once
#include <string>
#include <Windows.h>
#include <Lmcons.h>

namespace CrumbsBrowser {

    /// Replaces [USERNAME], [COMPUTERNAME], and [OS] tokens in a URL string.
    inline std::wstring ReplaceTokens(std::wstring input)
    {
        struct TokenEntry { std::wstring_view token; std::wstring value; };

        wchar_t userName[UNLEN + 1]{};
        DWORD userNameSize = static_cast<DWORD>(std::size(userName));
        GetUserNameW(userName, &userNameSize);

        wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD computerNameSize = static_cast<DWORD>(std::size(computerName));
        GetComputerNameW(computerName, &computerNameSize);

        wchar_t osEnv[256]{};
        DWORD osLen = GetEnvironmentVariableW(L"OS", osEnv, static_cast<DWORD>(std::size(osEnv)));

        TokenEntry tokens[] = {
            { L"[USERNAME]",     userName },
            { L"[COMPUTERNAME]", computerName },
            { L"[OS]",           (osLen > 0 && osLen < std::size(osEnv)) ? osEnv : L"" }
        };

        for (auto& [token, replacement] : tokens)
        {
            size_t pos = 0;
            while ((pos = input.find(token, pos)) != std::wstring::npos)
            {
                input.replace(pos, token.size(), replacement);
                pos += replacement.size();
            }
        }
        return input;
    }

    /// Normalises a user-typed address bar value to a navigable URI string.
    inline std::wstring NormaliseAddressBarInput(std::wstring text)
    {
        if (text.empty()) return text;
        if (text.find(L"://") == std::wstring::npos)
            text = L"https://" + text;
        return text;
    }

} // namespace CrumbsBrowser

