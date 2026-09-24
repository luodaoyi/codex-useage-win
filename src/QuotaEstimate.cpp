#include "QuotaEstimate.h"

#include "JsonLite.h"
#include "TextUtil.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string ReadFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteFileBytes(const std::wstring& path, const std::string& content) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

std::string IsoWeekKey(std::time_t now) {
    std::tm local = {};
    localtime_s(&local, &now);
    std::tm thursday = local;
    const int delta = 4 - (local.tm_wday == 0 ? 7 : local.tm_wday);
    thursday.tm_mday += delta;
    std::mktime(&thursday);
    char buffer[16] = {};
    std::strftime(buffer, sizeof(buffer), "%G-W%V", &thursday);
    return buffer;
}

struct WeekRow {
    std::string week;
    double usedPercent = 0;
    bool hasCredits = false;
    double credits = 0;
};

std::vector<WeekRow> ReadWeeks(const std::string& jsonText) {
    std::vector<WeekRow> rows;
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return rows;
    }
    const jsonlite::Value* weeks = root->Find("weeks");
    const auto* array = weeks != nullptr ? weeks->AsArray() : nullptr;
    if (array == nullptr) {
        return rows;
    }
    for (const jsonlite::Value& item : *array) {
        WeekRow row;
        if (auto week = item.Find("week") != nullptr ? item.Find("week")->AsString() : std::nullopt; week.has_value()) {
            row.week = std::string(*week);
        }
        if (const jsonlite::Value* used = item.Find("usedPercent"); used != nullptr) {
            if (auto number = used->AsNumber(); number.has_value()) {
                row.usedPercent = *number;
            }
        }
        if (const jsonlite::Value* flag = item.Find("hasCredits"); flag != nullptr) {
            if (auto value = flag->AsBool(); value.has_value()) {
                row.hasCredits = *value;
            }
        }
        if (const jsonlite::Value* credits = item.Find("credits"); credits != nullptr) {
            if (auto number = credits->AsNumber(); number.has_value()) {
                row.credits = *number;
            }
        }
        if (!row.week.empty()) {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

}  // namespace

QuotaEstimateView RecordWeeklyEstimate(
    const std::wstring& historyPath,
    double usedPercent,
    bool hasCredits,
    double credits,
    bool enabled) {
    QuotaEstimateView view;
    if (!enabled) {
        view.summary = L"weekly estimate off";
        return view;
    }
    std::vector<WeekRow> rows = ReadWeeks(ReadFile(historyPath));
    const std::string week = IsoWeekKey(std::time(nullptr));
    WeekRow* current = nullptr;
    const WeekRow* previous = nullptr;
    for (WeekRow& row : rows) {
        if (row.week == week) {
            current = &row;
        } else if (previous == nullptr || row.week > previous->week) {
            previous = &row;
        }
    }
    if (current == nullptr) {
        rows.push_back(WeekRow{});
        current = &rows.back();
        current->week = week;
    }
    current->usedPercent = usedPercent;
    current->hasCredits = hasCredits;
    current->credits = hasCredits ? credits : 0;
    std::string json = "{\"weeks\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i != 0) {
            json += ',';
        }
        json += "{\"week\":\"" + rows[i].week + "\",\"usedPercent\":" + std::to_string(rows[i].usedPercent)
            + ",\"hasCredits\":" + (rows[i].hasCredits ? "true" : "false")
            + ",\"credits\":" + std::to_string(rows[i].hasCredits ? rows[i].credits : 0) + "}";
    }
    json += "]}";
    view.wroteHistory = WriteFileBytes(historyPath, json);
    std::wstring summary = L"Week used " + std::to_wstring(static_cast<int>(usedPercent)) + L"%";
    if (hasCredits) {
        summary += L" · credits " + std::to_wstring(static_cast<int>(credits));
    } else {
        summary += L" · credits unavailable";
    }
    if (previous != nullptr) {
        const int delta = static_cast<int>(usedPercent - previous->usedPercent);
        summary += L" · vs last " + std::to_wstring(delta) + L"%";
    }
    view.summary = summary;
    view.shown = true;
    return view;
}
