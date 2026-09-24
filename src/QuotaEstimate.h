#pragma once

#include <string>

struct QuotaEstimateView {
    bool shown = false;
    bool wroteHistory = false;
    std::wstring summary;
};

QuotaEstimateView RecordWeeklyEstimate(
    const std::wstring& historyPath,
    double usedPercent,
    bool hasCredits,
    double credits,
    bool enabled);
