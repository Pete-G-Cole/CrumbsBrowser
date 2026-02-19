#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <Windows.h>
#include <ShlObj.h>

using json = nlohmann::json;

namespace ConfigLib {

    // ------------ Utility: Deep merge two JSON objects -------------
    inline void merge(json& target, const json& source)
    {
        for (auto it = source.begin(); it != source.end(); ++it)
        {
            if (target.contains(it.key()) &&
                target[it.key()].is_object() &&
                it.value().is_object())
            {
                merge(target[it.key()], it.value());
            }
            else
            {
                target[it.key()] = it.value();
            }
        }
    }

    // ------------ Load JSON if file exists -------------
    inline json loadJson(const std::filesystem::path& p)
    {
        if (!std::filesystem::exists(p))
            return json::object();

        std::ifstream f(p);
        if (!f.is_open())
            return json::object();

        json j;
        f >> j;
        return j;
    }

    // ------------ Convert ENV style KEY__SUBKEY to JSON path -------------
    inline std::vector<std::string> split_env_key(std::string key)
    {
        std::vector<std::string> out;
        size_t pos;
        while ((pos = key.find("__")) != std::string::npos) {
            out.push_back(key.substr(0, pos));
            key.erase(0, pos + 2);
        }
        out.push_back(key);
        return out;
    }

    inline void applyEnv(json& config, const std::string& envKey, const std::string& value)
    {
        auto parts = split_env_key(envKey);

        json* current = &config;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            std::string p = parts[i];

            // optional normalisation: capitalise first letter
            if (!p.empty())
                p[0] = std::toupper(p[0]);

            if (i == parts.size() - 1)
            {
                // Detect numbers / bools automatically
                if (value == "true" || value == "false")
                    (*current)[p] = (value == "true");
                else {
                    try { (*current)[p] = std::stod(value); }
                    catch (...) { (*current)[p] = value; }
                }
            }
            else
            {
                current = &((*current)[p]);
            }
        }
    }

    // ------------ Class: Configuration -------------
    class Configuration
    {
    public:
        Configuration(const std::string& appName,
            const std::string& environment = "")
        {
            load(appName, environment);
        }

        // -------- Typed accessors --------
        std::string getString(const std::string& path, const std::string& def = "") const
        {
            const json* j = lookup(path);
            return j && j->is_string() ? j->get<std::string>() : def;
        }

        int getInt(const std::string& path, int def = 0) const
        {
            const json* j = lookup(path);
            return j && j->is_number() ? j->get<int>() : def;
        }

        double getDouble(const std::string& path, double def = 0.0) const
        {
            const json* j = lookup(path);
            return j && j->is_number() ? j->get<double>() : def;
        }

        bool getBool(const std::string& path, bool def = false) const
        {
            const json* j = lookup(path);
            return j && j->is_boolean() ? j->get<bool>() : def;
        }

        json getObject(const std::string& path) const
        {
            const json* j = lookup(path);
            return j && j->is_object() ? *j : json::object();
        }

        json getArray(const std::string& path) const
        {
            const json* j = lookup(path);
            return j && j->is_array() ? *j : json::array();
        }

        const json& raw() const { return _config; }

    private:
        json _config;

        // Convert dotted path to JSON pointer
        const json* lookup(const std::string& dotted) const
        {
            json::json_pointer ptr = json::json_pointer("/" + dotted);
            if (_config.contains(ptr))
                return &_config.at(ptr);
            return nullptr;
        }

        void load(const std::string& appName, const std::string& env)
        {
            // 1. Resolve executable directory
            wchar_t buf[MAX_PATH];
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            auto exeDir = std::filesystem::path(buf).parent_path();

            // 2. Base file
            merge(_config, loadJson(exeDir / "appsettings.json"));

            // 3. Environment-specific file: appsettings.<ENV>.json
            if (!env.empty())
                merge(_config, loadJson(exeDir / ("appsettings." + env + ".json")));

            // 4. Local override: appsettings.local.json
            merge(_config, loadJson(exeDir / "appsettings.local.json"));

            // 5. Scan wildcard files: appsettings.*.json
            for (auto& p : std::filesystem::directory_iterator(exeDir))
            {
                if (p.path().filename().string().rfind("appsettings.", 0) == 0 &&
                    p.path().extension() == ".json")
                {
                    if (p.path().filename() != "appsettings.json")
                        merge(_config, loadJson(p.path()));
                }
            }

            // 6a. User roaming directory
            PWSTR roaming = nullptr;
            SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
            std::filesystem::path userDir(roaming);
            CoTaskMemFree(roaming);

            auto userCfg = userDir / appName / "appsettings.json";
            merge(_config, loadJson(userCfg));

            // 6b. User local directory
            roaming = nullptr;
            SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &roaming);
            userDir = roaming;
            CoTaskMemFree(roaming);

            userCfg = userDir / appName / "appsettings.json";
            merge(_config, loadJson(userCfg));

            // 7. Apply environment variables (all of them)
            for (auto& e : getAllEnvVars())
            {
                applyEnv(_config, e.first, e.second);
            }
        }

        // Enumerate environment variables
        static std::vector<std::pair<std::string, std::string>> getAllEnvVars()
        {
            std::vector<std::pair<std::string, std::string>> out;
            LPWCH env = GetEnvironmentStringsW();
            LPWCH cur = env;

            while (*cur)
            {
                std::wstring ws(cur);
                cur += ws.size() + 1;

                auto pos = ws.find(L'=');
                if (pos == std::wstring::npos) continue;

                std::string key(ws.begin(), ws.begin() + pos);
                std::string val(ws.begin() + pos + 1, ws.end());
                out.emplace_back(key, val);
            }

            FreeEnvironmentStringsW(env);
            return out;
        }
    };

} // namespace ConfigLib
