#include "SessionIndex.h"

#include "JsonLite.h"
#include "TextUtil.h"

#include <Windows.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

struct Price {
    double input = 0;
    double cached = 0;
    double output = 0;
};

struct CachedSession {
    std::wstring path;
    long long mtime = 0;
    long long size = 0;
    long long input = 0;
    long long cached = 0;
    long long output = 0;
    std::wstring model;
    std::string day;
};

std::string ReadFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteText(const std::wstring& path, const std::string& content) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

std::string TodayKey() {
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    char buffer[16] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

std::string DayOffset(int daysBack) {
    std::time_t now = std::time(nullptr) - static_cast<std::time_t>(daysBack) * 86400;
    std::tm local = {};
    localtime_s(&local, &now);
    char buffer[16] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

long long NumberAt(const jsonlite::Value* node, const char* key) {
    if (node == nullptr) {
        return 0;
    }
    const jsonlite::Value* child = node->Find(key);
    if (child == nullptr) {
        return 0;
    }
    if (auto asInt = child->AsInt(); asInt.has_value()) {
        return *asInt;
    }
    if (auto asNumber = child->AsNumber(); asNumber.has_value()) {
        return static_cast<long long>(*asNumber);
    }
    return 0;
}

std::wstring ModelAt(const jsonlite::Value& root) {
    auto read = [](const jsonlite::Value* node, const char* key) -> std::wstring {
        if (node == nullptr || node->Find(key) == nullptr) {
            return {};
        }
        if (auto text = node->Find(key)->AsString(); text.has_value() && !text->empty()) {
            return Utf8ToWide(std::string(*text));
        }
        return {};
    };
    if (std::wstring model = read(&root, "model"); !model.empty()) {
        return model;
    }
    const jsonlite::Value* payload = root.Find("payload");
    if (std::wstring model = read(payload, "model"); !model.empty()) {
        return model;
    }
    const jsonlite::Value* info = payload != nullptr ? payload->Find("info") : nullptr;
    return read(info, "model");
}

const jsonlite::Value* TokenUsage(const jsonlite::Value& root) {
    const jsonlite::Value* payload = root.Find("payload");
    const jsonlite::Value* info = payload != nullptr ? payload->Find("info") : nullptr;
    if (info != nullptr && info->Find("total_token_usage") != nullptr) {
        return info->Find("total_token_usage");
    }
    if (root.Find("total_token_usage") != nullptr) {
        return root.Find("total_token_usage");
    }
    return nullptr;
}

CachedSession ParseSession(const std::wstring& path) {
    CachedSession session;
    session.path = path;
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        jsonlite::Parser parser(line);
        const auto root = parser.Parse();
        if (!root.has_value() || !root->IsObject()) {
            continue;
        }
        if (std::wstring model = ModelAt(*root); !model.empty()) {
            session.model = std::move(model);
        }
        if (const jsonlite::Value* usage = TokenUsage(*root); usage != nullptr) {
            session.input = NumberAt(usage, "input_tokens");
            session.cached = NumberAt(usage, "cached_input_tokens");
            session.output = NumberAt(usage, "output_tokens");
        }
        if (auto timestamp = root->Find("timestamp") != nullptr ? root->Find("timestamp")->AsString() : std::nullopt;
            timestamp.has_value() && timestamp->size() >= 10 && session.day.empty()) {
            session.day = std::string(timestamp->substr(0, 10));
        }
    }
    if (session.day.empty()) {
        session.day = TodayKey();
    }
    return session;
}

bool InRange(const std::string& day, UsageRange range) {
    if (range == UsageRange::Today) {
        return day == TodayKey();
    }
    if (range == UsageRange::Month) {
        return day.size() >= 7 && TodayKey().size() >= 7 && day.substr(0, 7) == TodayKey().substr(0, 7);
    }
    const int start = range == UsageRange::PreviousCycle ? 8 : 0;
    const int end = range == UsageRange::PreviousCycle ? 14 : 6;
    for (int i = start; i <= end; ++i) {
        if (day == DayOffset(i)) {
            return true;
        }
    }
    return false;
}

std::map<std::wstring, Price>& TestPrices() {
    static std::map<std::wstring, Price> prices;
    return prices;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

}  // namespace

SessionIndex::SessionIndex(std::wstring logsRoot, std::wstring indexDirectory)
    : logsRoot_(std::move(logsRoot)), indexDirectory_(std::move(indexDirectory)) {}

void SessionIndex::SetPriceForTest(
    std::wstring model,
    double inputPerMillion,
    double cachedPerMillion,
    double outputPerMillion) {
    TestPrices()[std::move(model)] = Price{inputPerMillion, cachedPerMillion, outputPerMillion};
}

SessionScan SessionIndex::Scan(UsageRange range) {
    SessionScan scan;
    std::map<std::wstring, CachedSession> cache;
    const std::wstring cachePath = indexDirectory_ + L"\\sessions.json";
    {
        jsonlite::Parser parser(ReadFile(cachePath));
        if (const auto root = parser.Parse(); root.has_value()) {
            if (const jsonlite::Value* files = root->Find("files"); files != nullptr) {
                if (const auto* array = files->AsArray(); array != nullptr) {
                    for (const jsonlite::Value& item : *array) {
                        CachedSession session;
                        if (auto path = item.Find("path") != nullptr ? item.Find("path")->AsString() : std::nullopt; path.has_value()) {
                            session.path = Utf8ToWide(std::string(*path));
                        }
                        if (auto mtime = item.Find("mtime") != nullptr ? item.Find("mtime")->AsString() : std::nullopt; mtime.has_value()) {
                            session.mtime = std::stoll(std::string(*mtime));
                        }
                        if (auto size = item.Find("size") != nullptr ? item.Find("size")->AsString() : std::nullopt; size.has_value()) {
                            session.size = std::stoll(std::string(*size));
                        }
                        session.input = NumberAt(&item, "input");
                        session.cached = NumberAt(&item, "cached");
                        session.output = NumberAt(&item, "output");
                        if (auto model = item.Find("model") != nullptr ? item.Find("model")->AsString() : std::nullopt; model.has_value()) {
                            session.model = Utf8ToWide(std::string(*model));
                        }
                        if (auto day = item.Find("day") != nullptr ? item.Find("day")->AsString() : std::nullopt; day.has_value()) {
                            session.day = std::string(*day);
                        }
                        if (!session.path.empty()) {
                            cache[session.path] = std::move(session);
                        }
                    }
                }
            }
        }
    }

    std::error_code error;
    if (!std::filesystem::is_directory(logsRoot_, error)) {
        scan.error = L"session directory is missing";
        return scan;
    }
    std::vector<CachedSession> fresh;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(logsRoot_, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        if (_wcsicmp(entry.path().extension().c_str(), L".jsonl") != 0) {
            continue;
        }
        if (_wcsicmp(entry.path().filename().c_str(), L"session_index.jsonl") == 0) {
            continue;
        }
        const std::wstring path = entry.path().wstring();
        const long long mtime = static_cast<long long>(entry.last_write_time().time_since_epoch().count());
        const long long size = static_cast<long long>(entry.file_size());
        const auto cached = cache.find(path);
        if (cached != cache.end() && cached->second.mtime == mtime && cached->second.size == size) {
            fresh.push_back(cached->second);
            ++scan.filesSkipped;
            continue;
        }
        CachedSession session = ParseSession(path);
        std::error_code statError;
        session.mtime = static_cast<long long>(std::filesystem::last_write_time(path, statError).time_since_epoch().count());
        session.size = static_cast<long long>(std::filesystem::file_size(path, statError));
        if (statError) {
            session.mtime = mtime;
            session.size = size;
        }
        fresh.push_back(session);
        ++scan.filesRead;
    }

    std::string json = "{\"files\":[";
    for (size_t i = 0; i < fresh.size(); ++i) {
        if (i != 0) {
            json += ',';
        }
        json += "{\"path\":\"" + JsonEscape(WideToUtf8(fresh[i].path)) + "\",\"mtime\":\"" + std::to_string(fresh[i].mtime)
            + "\",\"size\":\"" + std::to_string(fresh[i].size) + "\""
            + ",\"input\":" + std::to_string(fresh[i].input)
            + ",\"cached\":" + std::to_string(fresh[i].cached)
            + ",\"output\":" + std::to_string(fresh[i].output)
            + ",\"model\":\"" + JsonEscape(WideToUtf8(fresh[i].model))
            + "\",\"day\":\"" + fresh[i].day + "\"}";
    }
    json += "]}";
    WriteText(cachePath, json);

    std::map<std::string, UsageBucket> buckets;
    for (const CachedSession& session : fresh) {
        if (!InRange(session.day, range)) {
            continue;
        }
        SessionRow row;
        row.name = std::filesystem::path(session.path).filename().wstring();
        row.model = session.model.empty() ? L"unknown" : session.model;
        row.day = session.day;
        row.tokens = session.input + session.output;
        const auto price = TestPrices().find(session.model);
        if (price == TestPrices().end()) {
            row.status = L"unknown-model";
            row.hasCost = false;
            scan.rangeCostComplete = false;
        } else {
            row.status = L"ok";
            row.hasCost = true;
            row.costUsd = (price->second.input * session.input
                + price->second.cached * session.cached
                + price->second.output * session.output) / 1000000.0;
            scan.rangeCostUsd += row.costUsd;
        }
        scan.rangeTokens += row.tokens;
        UsageBucket& bucket = buckets[session.day];
        bucket.key = session.day;
        bucket.tokens += row.tokens;
        if (row.hasCost) {
            bucket.costUsd += row.costUsd;
        } else {
            bucket.hasCost = false;
        }
        scan.recent.push_back(std::move(row));
    }
    for (const auto& [key, bucket] : buckets) {
        (void)key;
        scan.daily.push_back(bucket);
    }
    std::sort(scan.daily.begin(), scan.daily.end(), [](const UsageBucket& left, const UsageBucket& right) {
        return left.key < right.key;
    });
    if (scan.recent.size() > 20) {
        scan.recent.resize(20);
    }
    scan.success = true;
    return scan;
}

bool SessionIndex::RepairSessionIndex(const std::wstring& sessionIndexPath, std::wstring* error) {
    std::error_code copyError;
    if (std::filesystem::exists(sessionIndexPath, copyError) && !copyError) {
        const std::wstring backup = sessionIndexPath + L".bak-" + std::to_wstring(static_cast<long long>(std::time(nullptr)));
        std::filesystem::copy_file(sessionIndexPath, backup, std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError) {
            if (error != nullptr) {
                *error = L"cannot back up session_index.jsonl";
            }
            return false;
        }
    }
    std::string body;
    std::error_code iterError;
    if (std::filesystem::is_directory(logsRoot_, iterError)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(logsRoot_, iterError)) {
            if (iterError || !entry.is_regular_file()) {
                continue;
            }
            if (_wcsicmp(entry.path().extension().c_str(), L".jsonl") != 0) {
                continue;
            }
            if (_wcsicmp(entry.path().filename().c_str(), L"session_index.jsonl") == 0) {
                continue;
            }
            const long long mtime = static_cast<long long>(entry.last_write_time().time_since_epoch().count());
            body += "{\"path\":\"" + JsonEscape(WideToUtf8(entry.path().wstring())) + "\",\"mtime\":" + std::to_string(mtime) + "}\n";
        }
    }
    if (!WriteText(sessionIndexPath, body)) {
        if (error != nullptr) {
            *error = L"cannot write session_index.jsonl";
        }
        return false;
    }
    return true;
}
