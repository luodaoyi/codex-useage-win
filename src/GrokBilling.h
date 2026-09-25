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
// Weekly remaining percent, 0-100. -1 when the snapshot has no usage field.
int GrokWeeklyRemainingPercent(const GrokSnapshot& snapshot);
// True when the access JWT exp is missing-or-due within leadSeconds.
bool GrokTokenNeedsRefresh(const std::string& jwt, long long nowUnix, long long leadSeconds);

struct GrokTokenRefreshResult {
    bool attempted = false;
    bool success = false;
    std::wstring error;
};

// Grok access tokens last about 6 hours. Refresh while an hour remains so a
// 5-minute background check cannot miss the window the way a 5-minute lead can.
constexpr long long kGrokRefreshLeadSeconds = 60LL * 60;

// Refresh only when the access token is due. force skips the expiry check.
// Writes the rotated tokens back to authPath.
GrokTokenRefreshResult RefreshGrokAuthIfNeeded(
    const std::wstring& authPath,
    long long leadSeconds,
    bool force);
