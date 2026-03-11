#include "pch.h"
#include <gtest/gtest.h>
#include "../CrumbsBrowser/UrlUtils.hpp"
#include <fstream>
#include <filesystem>

// ---------------------------------------------------------------------------
// NormaliseAddressBarInput()
// ---------------------------------------------------------------------------
TEST(NormaliseAddressBarInputTests, EmptyInputReturnsEmpty)
{
    EXPECT_EQ(CrumbsBrowser::NormaliseAddressBarInput(L""), L"");
}

TEST(NormaliseAddressBarInputTests, BareHostnamePrefixedWithHttps)
{
    EXPECT_EQ(CrumbsBrowser::NormaliseAddressBarInput(L"example.com"), L"https://example.com");
}

TEST(NormaliseAddressBarInputTests, ExistingSchemeIsPreserved)
{
    EXPECT_EQ(CrumbsBrowser::NormaliseAddressBarInput(L"http://example.com"), L"http://example.com");
    EXPECT_EQ(CrumbsBrowser::NormaliseAddressBarInput(L"https://example.com"), L"https://example.com");
}

TEST(NormaliseAddressBarInputTests, FtpSchemeIsPreserved)
{
    EXPECT_EQ(CrumbsBrowser::NormaliseAddressBarInput(L"ftp://files.example.com"), L"ftp://files.example.com");
}

// ---------------------------------------------------------------------------
// ReplaceTokens() — tokens that can be validated at runtime
// ---------------------------------------------------------------------------
TEST(ReplaceTokensTests, NoTokensReturnedUnchanged)
{
    const std::wstring input = L"https://example.com/page";
    EXPECT_EQ(CrumbsBrowser::ReplaceTokens(input), input);
}

TEST(ReplaceTokensTests, UsernameTokenIsReplaced)
{
    // The replacement must be non-empty and the literal token must be gone.
    auto result = CrumbsBrowser::ReplaceTokens(L"https://example.com?u=[USERNAME]");
    EXPECT_EQ(result.find(L"[USERNAME]"), std::wstring::npos);
    EXPECT_NE(result.find(L"https://example.com?u="), std::wstring::npos);
}

TEST(ReplaceTokensTests, OsTokenIsReplaced)
{
    auto result = CrumbsBrowser::ReplaceTokens(L"?os=[OS]");
    EXPECT_EQ(result.find(L"[OS]"), std::wstring::npos);
}

TEST(ReplaceTokensTests, MultipleTokensInSameString)
{
    auto result = CrumbsBrowser::ReplaceTokens(L"?u=[USERNAME]&pc=[COMPUTERNAME]");
    EXPECT_EQ(result.find(L"[USERNAME]"), std::wstring::npos);
    EXPECT_EQ(result.find(L"[COMPUTERNAME]"), std::wstring::npos);
}