#include "GrokBilling.h"

#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

#include <fstream>
#include <sstream>

namespace {

std::wstring ProductName(const std::string& raw) {
    if (raw == "GrokBuild" || raw == "PRODUCT_GROK_BUILD") {
        return L"Build";
    }
    if (raw == "GrokImagine" || raw == "PRODUCT_GROK_IMAGINE") {
        return L"Imagine";
    }
    if (raw == "GrokChat" || raw == "PRODUCT_GROK_CHAT") {
        return L"Chat";
    }
    return Utf8ToWide(raw);
}

double Cents(const jsonlite::Value* node) {
    if (node == nullptr) {
        return 0;
    }
    const jsonlite::Value* val = node->Find("val");
    const jsonlite::Value* amount = val != nullptr ? val : node;
    if (auto number = amount->AsNumber(); number.has_value()) {
        return *number;
    }
    return 0;
}

}  // namespace

GrokSnapshot ParseGrokBillingJson(const std::string& jsonText) {
    GrokSnapshot snapshot;
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        snapshot.error = L"grok billing JSON parse failed";
        return snapshot;
    }
    const jsonlite::Value* config = root->Find("config");
    const jsonlite::Value* body = config != nullptr ? config : &*root;
    bool recognized = false;
    if (const jsonlite::Value* percent = body->Find("creditUsagePercent"); percent != nullptr) {
        if (auto number = percent->AsNumber(); number.has_value()) {
            snapshot.usagePercent = *number;
            snapshot.hasUsagePercent = true;
            recognized = true;
        }
    }
    if (const jsonlite::Value* period = body->Find("currentPeriod"); period != nullptr) {
        recognized = true;
        if (auto type = period->Find("type") != nullptr ? period->Find("type")->AsString() : std::nullopt; type.has_value()) {
            snapshot.period = Utf8ToWide(std::string(*type));
        }
        if (auto end = period->Find("end") != nullptr ? period->Find("end")->AsString() : std::nullopt; end.has_value()) {
            snapshot.periodEnd = Utf8ToWide(std::string(*end));
        }
    }
    if (const jsonlite::Value* unified = body->Find("isUnifiedBillingUser"); unified != nullptr && unified->AsBool().value_or(false)) {
        recognized = true;
    }
    if (const jsonlite::Value* prepaid = body->Find("prepaidBalance"); prepaid != nullptr) {
        snapshot.prepaidCents = Cents(prepaid);
        snapshot.hasPrepaid = true;
    }
    if (const jsonlite::Value* onDemand = body->Find("onDemandUsed"); onDemand != nullptr) {
        snapshot.onDemandCents = Cents(onDemand);
        snapshot.hasOnDemand = true;
    }
    if (const jsonlite::Value* products = body->Find("productUsage"); products != nullptr) {
        if (const auto* array = products->AsArray(); array != nullptr) {
            for (const jsonlite::Value& item : *array) {
                GrokProduct product;
                if (auto name = item.Find("product") != nullptr ? item.Find("product")->AsString() : std::nullopt; name.has_value()) {
                    product.name = ProductName(std::string(*name));
                }
                if (const jsonlite::Value* usage = item.Find("usagePercent"); usage != nullptr && !usage->IsNull()) {
                    if (auto number = usage->AsNumber(); number.has_value()) {
                        product.usagePercent = *number;
                        product.hasPercent = true;
                    }
                }
                if (!product.name.empty()) {
                    snapshot.products.push_back(std::move(product));
                }
            }
        }
    }
    if (!recognized && snapshot.products.empty() && !snapshot.hasPrepaid) {
        snapshot.error = L"grok billing fields were not recognized";
        return snapshot;
    }
    if (!snapshot.hasUsagePercent) {
        snapshot.usagePercent = 0;
        snapshot.hasUsagePercent = true;
    }
    snapshot.success = true;
    return snapshot;
}

namespace {

std::string ReadFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteFileBytes(const std::wstring& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

bool ReplaceJsonString(std::string* json, const std::string& key, const std::string& value) {
    const std::string needle = "\"" + key + "\"";
    const size_t keyPos = json->find(needle);
    if (keyPos == std::string::npos) {
        return false;
    }
    const size_t colon = json->find(':', keyPos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    const size_t open = json->find('"', colon + 1);
    const size_t close = open == std::string::npos ? std::string::npos : json->find('"', open + 1);
    if (open == std::string::npos || close == std::string::npos) {
        return false;
    }
    json->replace(open + 1, close - open - 1, value);
    return true;
}

std::string JsonString(const jsonlite::Value& root, const char* key) {
    const jsonlite::Value* node = root.Find(key);
    if (node == nullptr) {
        return {};
    }
    if (auto text = node->AsString(); text.has_value()) {
        return std::string(*text);
    }
    return {};
}

std::string UrlEncode(const std::string& value) {
    std::string out;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 15]);
        }
    }
    return out;
}

bool RefreshGrokFile(const std::wstring& authPath, std::string* accessToken) {
    const std::string original = ReadFile(authPath);
    jsonlite::Parser parser(original);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return false;
    }
    const std::string refresh = JsonString(*root, "refresh_token");
    if (refresh.empty()) {
        return false;
    }
    const std::string body = "grant_type=refresh_token&client_id=b1a00492-073a-47ea-816f-4c329264a828&refresh_token="
        + UrlEncode(refresh);
    std::wstring error;
    const auto response = NetHttps(
        L"auth.x.ai",
        L"/oauth2/token",
        L"POST",
        {L"Content-Type: application/x-www-form-urlencoded", L"Accept: application/json"},
        &body,
        &error);
    if (!response.has_value()) {
        return false;
    }
    jsonlite::Parser tokenParser(*response);
    const auto tokenRoot = tokenParser.Parse();
    if (!tokenRoot.has_value()) {
        return false;
    }
    const std::string access = JsonString(*tokenRoot, "access_token");
    if (access.empty()) {
        return false;
    }
    std::string updated = original;
    ReplaceJsonString(&updated, "access_token", access);
    if (const std::string nextRefresh = JsonString(*tokenRoot, "refresh_token"); !nextRefresh.empty()) {
        ReplaceJsonString(&updated, "refresh_token", nextRefresh);
    }
    if (const std::string idToken = JsonString(*tokenRoot, "id_token"); !idToken.empty()) {
        ReplaceJsonString(&updated, "id_token", idToken);
    }
    WriteFileBytes(authPath, updated);
    if (accessToken != nullptr) {
        *accessToken = access;
    }
    return true;
}

}  // namespace

GrokSnapshot FetchGrokBilling(const std::string& accessToken) {
    if (accessToken.empty()) {
        GrokSnapshot snapshot;
        snapshot.error = L"grok access_token is missing";
        return snapshot;
    }
    std::wstring error;
    const std::vector<std::wstring> headers = {
        L"Authorization: Bearer " + Utf8ToWide(accessToken),
        L"X-XAI-Token-Auth: xai-grok-cli",
        L"Accept: application/json",
        L"User-Agent: CodexUsageBar",
    };
    const auto body = NetHttps(
        L"cli-chat-proxy.grok.com",
        L"/v1/billing?format=credits",
        L"GET",
        headers,
        nullptr,
        &error);
    if (!body.has_value()) {
        GrokSnapshot snapshot;
        snapshot.error = error.empty() ? L"grok billing request failed" : error;
        return snapshot;
    }
    return ParseGrokBillingJson(*body);
}

GrokSnapshot FetchGrokBillingFile(const std::wstring& authPath) {
    const std::string original = ReadFile(authPath);
    jsonlite::Parser parser(original);
    const auto root = parser.Parse();
    std::string access;
    if (root.has_value()) {
        access = JsonString(*root, "access_token");
    }
    GrokSnapshot snapshot = FetchGrokBilling(access);
    const bool unauthorized = snapshot.error.find(L"HTTP 401") != std::wstring::npos
        || snapshot.error.find(L"HTTP 403") != std::wstring::npos;
    if (!unauthorized) {
        return snapshot;
    }
    if (!RefreshGrokFile(authPath, &access)) {
        return snapshot;
    }
    return FetchGrokBilling(access);
}
