#pragma once

#include <string>
#include <vector>

enum class UsageRange {
    Today = 0,
    Cycle = 1,
    PreviousCycle = 2,
    Month = 3,
};

struct UsageBucket {
    std::string key;
    long long tokens = 0;
    double costUsd = 0;
    bool hasCost = true;
};

struct SessionRow {
    std::wstring name;
    std::wstring model;
    std::wstring status;
    std::string day;
    long long tokens = 0;
    bool hasCost = false;
    double costUsd = 0;
};

struct SessionScan {
    bool success = false;
    int filesRead = 0;
    int filesSkipped = 0;
    std::wstring error;
    std::vector<SessionRow> recent;
    std::vector<UsageBucket> daily;
    long long rangeTokens = 0;
    double rangeCostUsd = 0;
    bool rangeCostComplete = true;
};

class SessionIndex {
public:
    SessionIndex(std::wstring logsRoot, std::wstring indexDirectory);

    SessionScan Scan(UsageRange range);
    bool RepairSessionIndex(const std::wstring& sessionIndexPath, std::wstring* error);
    void SetPriceForTest(std::wstring model, double inputPerMillion, double cachedPerMillion, double outputPerMillion);

private:
    std::wstring logsRoot_;
    std::wstring indexDirectory_;
};
