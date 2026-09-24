#include "ResetStatus.h"

#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

namespace {

std::optional<std::string> StringField(const jsonlite::Value& node, const char* key) {
    const jsonlite::Value* child = node.Find(key);
    if (child == nullptr) {
        return std::nullopt;
    }
    auto text = child->AsString();
    if (!text.has_value() || text->empty()) {
        return std::nullopt;
    }
    return std::string(*text);
}

}  // namespace

ResetStatusInfo ParseResetStatusJson(const std::string& jsonText) {
    ResetStatusInfo info;
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        info.error = L"reset status JSON parse failed";
        return info;
    }
    const jsonlite::Value* events = root->Find("events");
    const auto* array = events != nullptr ? events->AsArray() : nullptr;
    if (array == nullptr || array->empty()) {
        info.error = L"reset status has no events";
        return info;
    }
    const jsonlite::Value* chosen = &array->front();
    for (const jsonlite::Value& event : *array) {
        if (auto kind = StringField(event, "kind"); kind.has_value() && *kind == "reset_completed") {
            chosen = &event;
            break;
        }
    }
    if (auto text = StringField(*chosen, "text"); text.has_value()) {
        info.summary = Utf8ToWide(*text);
    } else if (auto type = StringField(*chosen, "resetType"); type.has_value()) {
        info.summary = Utf8ToWide(*type);
    } else {
        info.summary = L"Codex reset";
    }
    if (auto when = StringField(*chosen, "announcedAt"); when.has_value()) {
        info.whenText = Utf8ToWide(*when);
    }
    if (const jsonlite::Value* source = chosen->Find("source"); source != nullptr) {
        if (auto url = StringField(*source, "url"); url.has_value()) {
            info.url = Utf8ToWide(*url);
        }
    }
    info.success = true;
    return info;
}

ResetStatusInfo FetchResetStatus() {
    std::wstring error;
    const std::vector<std::wstring> headers = {
        L"Accept: application/json",
        L"User-Agent: CodexUsageBar",
    };
    const auto body = NetHttps(L"www.codexrunway.com", L"/api/status.json", L"GET", headers, nullptr, &error);
    if (!body.has_value()) {
        ResetStatusInfo info;
        info.error = error.empty() ? L"reset status request failed" : error;
        return info;
    }
    ResetStatusInfo info = ParseResetStatusJson(*body);
    if (!info.success && info.error.empty()) {
        info.error = error;
    }
    return info;
}
