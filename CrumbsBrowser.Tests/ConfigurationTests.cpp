#include "pch.h"
#include <gtest/gtest.h>
#include "../CrumbsBrowser/configuration.hpp"
#include <fstream>
#include <filesystem>

// ---------------------------------------------------------------------------
// merge()
// ---------------------------------------------------------------------------
TEST(MergeTests, MergesDisjointObjects)
{
    json a = { {"a", 1} };
    json b = { {"b", 2} };
    ConfigLib::merge(a, b);
    EXPECT_EQ(a["a"], 1);
    EXPECT_EQ(a["b"], 2);
}

TEST(MergeTests, SourceOverwritesScalar)
{
    json a = { {"key", "original"} };
    json b = { {"key", "override"} };
    ConfigLib::merge(a, b);
    EXPECT_EQ(a["key"], "override");
}

TEST(MergeTests, DeepMergePreservesUnchangedNestedKeys)
{
    json a = { {"UI", {{"AddressBar", true}, {"HomeButton", true}}} };
    json b = { {"UI", {{"AddressBar", false}}} };
    ConfigLib::merge(a, b);
    EXPECT_EQ(a["UI"]["AddressBar"], false);
    EXPECT_EQ(a["UI"]["HomeButton"], true); // untouched
}

// ---------------------------------------------------------------------------
// split_env_key()
// ---------------------------------------------------------------------------
TEST(SplitEnvKeyTests, SingleSegment)
{
    auto parts = ConfigLib::split_env_key("Startup");
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0], "Startup");
}

TEST(SplitEnvKeyTests, TwoSegments)
{
    auto parts = ConfigLib::split_env_key("Startup__Url");
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0], "Startup");
    EXPECT_EQ(parts[1], "Url");
}

TEST(SplitEnvKeyTests, ThreeSegments)
{
    auto parts = ConfigLib::split_env_key("Security__Tls__RequireHttps");
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[2], "RequireHttps");
}

// ---------------------------------------------------------------------------
// applyEnv()
// ---------------------------------------------------------------------------
TEST(ApplyEnvTests, SetsStringValue)
{
    json config;
    ConfigLib::applyEnv(config, "Startup__Url", "https://example.com");
    EXPECT_EQ(config["Startup"]["Url"], "https://example.com");
}

TEST(ApplyEnvTests, SetsBooleanTrue)
{
    json config;
    ConfigLib::applyEnv(config, "Security__RequireHttps", "true");
    EXPECT_EQ(config["Security"]["RequireHttps"], true);
}

TEST(ApplyEnvTests, SetsBooleanFalse)
{
    json config;
    ConfigLib::applyEnv(config, "Security__RequireHttps", "false");
    EXPECT_EQ(config["Security"]["RequireHttps"], false);
}

TEST(ApplyEnvTests, OverwritesExistingValue)
{
    json config = { {"Startup", {{"Url", "https://old.com"}}} };
    EXPECT_EQ(config["Startup"]["Url"], "https://old.com");

    ConfigLib::applyEnv(config, "Startup__Url", "https://new.com");
    EXPECT_EQ(config["Startup"]["Url"], "https://new.com");
}

// ---------------------------------------------------------------------------
// loadJson()
// ---------------------------------------------------------------------------
class TempJsonFile
{
public:
    explicit TempJsonFile(const std::string& content)
        : path_(std::filesystem::temp_directory_path() / "crumbs_test.json")
    {
        std::ofstream f(path_);
        f << content;
    }
    ~TempJsonFile() { std::filesystem::remove(path_); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

TEST(LoadJsonTests, LoadsValidFile)
{
    TempJsonFile f(R"({"key":"value"})");
    auto j = ConfigLib::loadJson(f.path());
    EXPECT_EQ(j["key"], "value");
}

TEST(LoadJsonTests, ReturnsEmptyObjectForMissingFile)
{
    auto j = ConfigLib::loadJson("nonexistent_file_xyz.json");
    EXPECT_TRUE(j.empty());
}