#pragma once

#include <optional>
#include <string>

struct ResetStatusInfo {
    bool success = false;
    std::wstring summary;
    std::wstring url;
    std::wstring whenText;
    std::wstring error;
};

ResetStatusInfo ParseResetStatusJson(const std::string& jsonText);
ResetStatusInfo FetchResetStatus();
