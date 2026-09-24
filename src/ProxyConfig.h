#pragma once

#include <optional>
#include <string>
#include <vector>

struct ProxyConfig {
    enum class Mode {
        System = 0,
        Http = 1,
        Socks5 = 2,
    };

    Mode mode = Mode::System;
    std::wstring server;
    std::wstring user;
    std::wstring password;
};

ProxyConfig GetProcessProxy();
void SetProcessProxy(const ProxyConfig& config);
bool SaveProxyPassword(const ProxyConfig& config);
bool LoadProxyPassword(ProxyConfig* config);
std::string BuildSocksGreeting(bool withUserPass);
bool ProxyIniContainsSecret(const std::string& iniText);
std::wstring DescribeProxy(const ProxyConfig& config);
std::optional<std::string> Socks5Https(
    const std::wstring& targetHost,
    const std::wstring& pathAndQuery,
    const std::wstring& method,
    const std::vector<std::wstring>& headers,
    const std::string* body,
    const ProxyConfig& proxy,
    std::wstring* errorMessage);
