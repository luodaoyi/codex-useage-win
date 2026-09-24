#pragma once

#include <string>
#include <vector>

struct GrokProduct {
    std::wstring name;
    double usagePercent = 0;
    bool hasPercent = false;
};

struct GrokSnapshot {
    bool success = false;
    std::wstring error;
    double usagePercent = 0;
    bool hasUsagePercent = false;
    std::wstring period;
    std::wstring periodEnd;
    double prepaidCents = 0;
    bool hasPrepaid = false;
    double onDemandCents = 0;
    bool hasOnDemand = false;
    std::vector<GrokProduct> products;
};

GrokSnapshot ParseGrokBillingJson(const std::string& jsonText);
GrokSnapshot FetchGrokBilling(const std::string& accessToken);
// Reads the account copy, refreshes it on HTTP 401, and writes only that file.
GrokSnapshot FetchGrokBillingFile(const std::wstring& authPath);
