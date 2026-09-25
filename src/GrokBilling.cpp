#include "GrokBilling.h"

#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

#include <Windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cmath>
#include <ctime>
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

int GrokWeeklyRemainingPercent(const GrokSnapshot& snapshot) {
    if (!snapshot.hasUsagePercent) {
        return -1;
    }
    const int used = static_cast<int>(std::lround(snapshot.usagePercent));
    return std::max(0, std::min(100, 100 - used));
}

namespace {

std::optional<long long> JwtExpUnix(const std::string& jwt) {
    const size_t first = jwt.find('.');
    const size_t second = first == std::string::npos ? std::string::npos : jwt.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return std::nullopt;
    }
    std::string payload = jwt.substr(first + 1, second - first - 1);
    for (char& ch : payload) {
        if (ch == '-') {
            ch = '+';
        } else if (ch == '_') {
            ch = '/';
        }
    }
    while (payload.size() % 4 != 0) {
        payload.push_back('=');
    }
    DWORD size = 0;
    if (!CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr) || size == 0) {
        return std::nullopt;
    }
    std::string decoded(size, '\0');
    if (!CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(decoded.data()), &size, nullptr, nullptr)) {
        return std::nullopt;
    }
    decoded.resize(size);
    jsonlite::Parser parser(decoded);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return std::nullopt;
    }
    const jsonlite::Value* exp = root->Find("exp");
    if (exp == nullptr) {
        return std::nullopt;
    }
    if (auto asInt = exp->AsInt(); asInt.has_value()) {
        return static_cast<long long>(*asInt);
    }
    if (auto asNum = exp->AsNumber(); asNum.has_value()) {
        return static_cast<long long>(*asNum);
    }
    return std::nullopt;
}

std::string UnixToUtcIso8601(long long unixSeconds) {
    const std::time_t when = static_cast<std::time_t>(unixSeconds);
    std::tm utc = {};
    if (gmtime_s(&utc, &when) != 0) {
        return {};
    }
    char buffer[40] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return {};
    }
    return buffer;
}

}  // namespace

bool GrokTokenNeedsRefresh(const std::string& jwt, long long nowUnix, long long leadSeconds) {
    const auto exp = JwtExpUnix(jwt);
    if (!exp.has_value()) {
        return jwt.empty();
    }
    return *exp <= nowUnix + std::max(0LL, leadSeconds);
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

bool RefreshGrokFile(const std::wstring& authPath, std::string* accessToken, std::wstring* errorOut) {
    const std::string original = ReadFile(authPath);
    jsonlite::Parser parser(original);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        if (errorOut != nullptr) {
            *errorOut = L"grok auth JSON parse failed";
        }
        return false;
    }
    const std::string refresh = JsonString(*root, "refresh_token");
    if (refresh.empty()) {
        if (errorOut != nullptr) {
            *errorOut = L"grok auth missing refresh_token";
        }
        return false;
    }
    const std::string body = "grant_type=refresh_token&client_id=b1a00492-073a-47ea-816f-4c329264a828&refresh_token="
        + UrlEncode(refresh);
    std::wstring requestError;
    const auto response = NetHttps(
        L"auth.x.ai",
        L"/oauth2/token",
        L"POST",
        {L"Content-Type: application/x-www-form-urlencoded", L"Accept: application/json"},
        &body,
        &requestError);
    if (!response.has_value()) {
        if (errorOut != nullptr) {
            *errorOut = requestError.empty() ? L"grok token refresh failed" : requestError;
        }
        return false;
    }
    jsonlite::Parser tokenParser(*response);
    const auto tokenRoot = tokenParser.Parse();
    if (!tokenRoot.has_value()) {
        if (errorOut != nullptr) {
            *errorOut = L"grok token refresh JSON parse failed";
        }
        return false;
    }
    const std::string access = JsonString(*tokenRoot, "access_token");
    if (access.empty()) {
        if (errorOut != nullptr) {
            *errorOut = L"grok token refresh missing access_token";
        }
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
    if (const auto exp = JwtExpUnix(access); exp.has_value()) {
        const std::string expired = UnixToUtcIso8601(*exp);
        if (!expired.empty()) {
            ReplaceJsonString(&updated, "expired", expired);
        }
    }
    const std::string refreshedAt = UnixToUtcIso8601(static_cast<long long>(std::time(nullptr)));
    if (!refreshedAt.empty()) {
        ReplaceJsonString(&updated, "last_refresh", refreshedAt);
    }
    if (!WriteFileBytes(authPath, updated)) {
        if (errorOut != nullptr) {
            *errorOut = L"cannot write refreshed grok auth";
        }
        return false;
    }
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
    std::string refresh;
    if (root.has_value()) {
        access = JsonString(*root, "access_token");
        refresh = JsonString(*root, "refresh_token");
    }
    std::wstring refreshError;
    bool refreshed = false;
    const long long now = static_cast<long long>(std::time(nullptr));
    // Grok access tokens last about 6 hours. A 1-day lead would refresh every poll.
    if (!refresh.empty() && GrokTokenNeedsRefresh(access, now, kGrokRefreshLeadSeconds)) {
        refreshed = RefreshGrokFile(authPath, &access, &refreshError);
    }
    GrokSnapshot snapshot = FetchGrokBilling(access);
    const auto unauthorized = [&](const GrokSnapshot& item) {
        return item.error.find(L"HTTP 401") != std::wstring::npos
            || item.error.find(L"HTTP 403") != std::wstring::npos;
    };
    if (unauthorized(snapshot) && !refresh.empty() && !refreshed) {
        if (RefreshGrokFile(authPath, &access, &refreshError)) {
            snapshot = FetchGrokBilling(access);
        }
    }
    if (!snapshot.success && !refreshError.empty()) {
        snapshot.error = refreshError;
    }
    return snapshot;
}

GrokTokenRefreshResult RefreshGrokAuthIfNeeded(
    const std::wstring& authPath,
    long long leadSeconds,
    bool force) {
    GrokTokenRefreshResult result;
    const std::string original = ReadFile(authPath);
    jsonlite::Parser parser(original);
    const auto root = parser.Parse();
    std::string access;
    if (root.has_value()) {
        access = JsonString(*root, "access_token");
    }
    const long long now = static_cast<long long>(std::time(nullptr));
    if (!force && !GrokTokenNeedsRefresh(access, now, leadSeconds)) {
        result.success = true;
        return result;
    }
    result.attempted = true;
    std::wstring error;
    result.success = RefreshGrokFile(authPath, nullptr, &error);
    result.error = error;
    return result;
}
