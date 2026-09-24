#pragma once

#include <optional>
#include <string>
#include <vector>

std::optional<std::string> NetHttps(
    const std::wstring& host,
    const std::wstring& path,
    const std::wstring& method,
    const std::vector<std::wstring>& headers,
    const std::string* body,
    std::wstring* errorMessage);
