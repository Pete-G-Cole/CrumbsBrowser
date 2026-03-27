#pragma once
#include <string>
#include <stdexcept>
#include <windows.h>

// UTF-8 to UTF-16
inline std::wstring utf8_to_utf16(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (size_needed <= 0) throw std::runtime_error("MultiByteToWideChar failed");
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &result[0], size_needed);
    return result;
}

// UTF-16 to UTF-8
inline std::string utf16_to_utf8(const std::wstring& utf16)
{
    if (utf16.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), &result[0], size_needed, nullptr, nullptr);
    return result;
}
