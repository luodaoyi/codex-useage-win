#include "CodexUsageFetcher.h"

#include "JsonLite.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

std::wstring FormatRadarToken(const std::string& token) {
    if (token == "gpt") {
        return L"GPT";
    }

    std::wstring wide = Utf8ToWide(token);
    if (!wide.empty() && wide[0] >= L'a' && wide[0] <= L'z') {
        wide[0] = static_cast<wchar_t>(wide[0] - (L'a' - L'A'));
    }
    return wide;
}

bool TokenLooksLikeVersion(const std::string& token) {
    for (char ch : token) {
        if (ch >= '0' && ch <= '9') {
            return true;
        }
    }
    return false;
}

std::wstring FormatRadarModelLabel(const std::string& model, const std::string& effort) {
    std::wstring result;
    size_t start = 0;
    while (start <= model.size()) {
        const size_t end = std::min(model.find('-', start), model.size());
        const std::string token = model.substr(start, end - start);
        if (!token.empty()) {
            const std::wstring part = FormatRadarToken(token);
            if (result.empty()) {
                result = part;
            } else if (TokenLooksLikeVersion(token)) {
                result += L'-';
                result += part;
            } else {
                result += L' ';
                result += part;
            }
        }
        if (end == model.size()) {
            break;
        }
        start = end + 1;
    }
    if (!effort.empty()) {
        if (!result.empty()) {
            result += L' ';
        }
        result += Utf8ToWide(effort);
    }
    return result;
}

std::string ExtractRadarFamilyKey(const std::string& model) {
    std::string lower = model;
    for (char& ch : lower) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    const size_t dash = lower.find('-');
    const std::string token = (dash == std::string::npos) ? lower : lower.substr(0, dash);
    return token.empty() ? lower : token;
}

bool FillModelIqScoreFromPoint(const jsonlite::Value& pointNode, ModelIqScore* score) {
    if (score == nullptr) {
        return false;
    }

    const jsonlite::Value* scoreNode = pointNode.Find("iq");
    if (scoreNode == nullptr) {
        return false;
    }
    const auto scoreValue = scoreNode->AsNumber();
    if (!scoreValue.has_value()) {
        return false;
    }

    score->score = *scoreValue;
    std::string modelUtf8;
    std::string effortUtf8;
    if (const jsonlite::Value* modelNode = pointNode.Find("model"); modelNode != nullptr) {
        if (auto model = modelNode->AsString(); model.has_value() && !model->empty()) {
            modelUtf8 = std::string(*model);
            score->model = Utf8ToWide(modelUtf8);
        }
    }
    if (const jsonlite::Value* effortNode = pointNode.Find("effort"); effortNode != nullptr) {
        if (auto effort = effortNode->AsString(); effort.has_value() && !effort->empty()) {
            effortUtf8 = std::string(*effort);
            score->effort = Utf8ToWide(effortUtf8);
        }
    }
    if (const jsonlite::Value* passedNode = pointNode.Find("passed"); passedNode != nullptr) {
        if (auto passed = passedNode->AsNumber(); passed.has_value()) {
            score->passed = static_cast<int>(*passed);
        }
    }
    const jsonlite::Value* totalNode = pointNode.Find("total");
    if (totalNode == nullptr) {
        totalNode = pointNode.Find("valid_tasks");
    }
    if (totalNode != nullptr) {
        if (auto total = totalNode->AsNumber(); total.has_value()) {
            score->tasks = static_cast<int>(*total);
        }
    }
    if (const jsonlite::Value* priceNode = pointNode.Find("average_price_usd");
        priceNode != nullptr && !priceNode->IsNull()) {
        if (auto price = priceNode->AsNumber(); price.has_value() && std::isfinite(*price)) {
            score->averagePriceUsd = *price;
            score->hasPrice = true;
        }
    }
    if (const jsonlite::Value* minutesNode = pointNode.Find("average_minutes");
        minutesNode != nullptr && !minutesNode->IsNull()) {
        if (auto minutes = minutesNode->AsNumber(); minutes.has_value() && std::isfinite(*minutes) && *minutes >= 0.0) {
            score->averageMinutes = *minutes;
            score->hasDuration = true;
        }
    }
    if (score->score >= 100.0) {
        score->status = L"green";
    } else if (score->score >= 85.0) {
        score->status = L"yellow";
    } else {
        score->status = L"red";
    }
    score->label = FormatRadarModelLabel(modelUtf8, effortUtf8);
    const std::string familyKey = ExtractRadarFamilyKey(modelUtf8);
    score->familyKey = Utf8ToWide(familyKey);
    score->familyLabel = FormatRadarModelLabel(familyKey, "");
    return !score->label.empty();
}

std::wstring ModelIqDedupKey(const ModelIqScore& score) {
    if (!score.model.empty() || !score.effort.empty()) {
        return score.model + L"|" + score.effort;
    }
    return score.label;
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& child) {
    std::wstring result = base;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    result += child;
    return result;
}

std::optional<std::wstring> ReadEnv(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) {
        return std::nullopt;
    }

    std::wstring value(size - 1, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    return value;
}

// Parses ISO-8601 timestamps:
// - 2026-07-18T00:39:53Z
// - 2026-07-18T00:39:53.868059Z
// - 2026-06-14T09:54:55+08:00
std::optional<long long> ParseIso8601UnixSeconds(const std::string& text) {
    if (text.size() < 19) {
        return std::nullopt;
    }

    try {
        std::tm tm = {};
        tm.tm_year = std::stoi(text.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(text.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(text.substr(8, 2));
        tm.tm_hour = std::stoi(text.substr(11, 2));
        tm.tm_min = std::stoi(text.substr(14, 2));
        tm.tm_sec = std::stoi(text.substr(17, 2));
        tm.tm_isdst = 0;

        long long offsetSeconds = 0;
        size_t pos = 19;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                ++pos;
            }
        }
        if (pos < text.size()) {
            if (text[pos] == 'Z' || text[pos] == 'z') {
                // UTC
            } else if ((text[pos] == '+' || text[pos] == '-') && pos + 5 < text.size()) {
                const int sign = text[pos] == '+' ? 1 : -1;
                const int oh = std::stoi(text.substr(pos + 1, 2));
                int om = 0;
                if (text[pos + 3] == ':' && pos + 5 < text.size()) {
                    om = std::stoi(text.substr(pos + 4, 2));
                } else {
                    om = std::stoi(text.substr(pos + 3, 2));
                }
                // Value is local wall time in that offset; convert to UTC.
                offsetSeconds = -static_cast<long long>(sign) * (oh * 3600LL + om * 60LL);
            }
        }

        const time_t utc = _mkgmtime(&tm);
        if (utc == static_cast<time_t>(-1)) {
            return std::nullopt;
        }
        return static_cast<long long>(utc) + offsetSeconds;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> HttpExchange(
    const std::wstring& userAgent,
    const std::wstring& host,
    const std::wstring& path,
    const std::wstring& method,
    const std::vector<std::wstring>& headers,
    const std::string* body,
    std::wstring* errorMessage) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    HINTERNET session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = L"WinHttpOpen failed";
        }
        return std::nullopt;
    }

    std::optional<std::string> responseBody;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    do {
        connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = L"WinHttpConnect failed";
            }
            break;
        }

        request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = L"WinHttpOpenRequest failed";
            }
            break;
        }

        for (const std::wstring& header : headers) {
            if (!WinHttpAddRequestHeaders(request, header.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD)) {
                if (errorMessage != nullptr) {
                    *errorMessage = L"WinHttpAddRequestHeaders failed";
                }
                break;
            }
        }
        if (errorMessage != nullptr && !errorMessage->empty()) {
            break;
        }

        DWORD timeout = 15000;
        WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);

        LPVOID bodyPtr = body != nullptr ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA;
        DWORD bodySize = body != nullptr ? static_cast<DWORD>(body->size()) : 0;
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPtr, bodySize, bodySize, 0)) {
            if (errorMessage != nullptr) {
                *errorMessage = L"WinHttpSendRequest failed";
            }
            break;
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            if (errorMessage != nullptr) {
                *errorMessage = L"WinHttpReceiveResponse failed";
            }
            break;
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        if (statusCode < 200 || statusCode > 299) {
            if (errorMessage != nullptr) {
                *errorMessage = host + path + L" returned HTTP " + std::to_wstring(statusCode);
                if (statusCode == 401) {
                    *errorMessage += L"; auth.json access_token may be expired (auto-refresh runs within 1 day of JWT exp or on 401/403)";
                }
            }
            break;
        }

        std::string response;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                if (errorMessage != nullptr) {
                    *errorMessage = L"WinHttpQueryDataAvailable failed";
                }
                break;
            }
            if (available == 0) {
                responseBody = std::move(response);
                break;
            }

            std::string chunk(static_cast<size_t>(available), '\0');
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &downloaded)) {
                if (errorMessage != nullptr) {
                    *errorMessage = L"WinHttpReadData failed";
                }
                break;
            }

            chunk.resize(downloaded);
            response.append(chunk);
        }
    } while (false);

    if (request != nullptr) {
        WinHttpCloseHandle(request);
    }
    if (connect != nullptr) {
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);

    return responseBody;
}

std::optional<std::string> HttpGetJson(
    const std::wstring& userAgent,
    const std::wstring& host,
    const std::wstring& path,
    const std::vector<std::wstring>& headers,
    std::wstring* errorMessage) {
    return HttpExchange(userAgent, host, path, L"GET", headers, nullptr, errorMessage);
}

std::vector<std::wstring> BuildCodexAuthHeaders(
    const std::string& accessToken,
    const std::string& accountId,
    bool forResetCredits) {
    std::vector<std::wstring> headers = {
        L"Authorization: Bearer " + Utf8ToWide(accessToken),
        L"Accept: application/json",
    };
    if (forResetCredits) {
        headers.push_back(L"OpenAI-Beta: codex-1");
        headers.push_back(L"originator: Codex Desktop");
    }
    if (!accountId.empty()) {
        headers.push_back(L"ChatGPT-Account-Id: " + Utf8ToWide(accountId));
    }
    return headers;
}

bool ExtractWindow(const jsonlite::Value* windowNode, UsageWindow* output) {
    if (windowNode == nullptr || output == nullptr || windowNode->IsNull()) {
        return false;
    }

    const jsonlite::Value* usedPercent = windowNode->Find("used_percent");
    const jsonlite::Value* limitWindowSeconds = windowNode->Find("limit_window_seconds");
    const jsonlite::Value* resetAfterSeconds = windowNode->Find("reset_after_seconds");
    const jsonlite::Value* resetAt = windowNode->Find("reset_at");
    if (usedPercent == nullptr || limitWindowSeconds == nullptr || resetAfterSeconds == nullptr || resetAt == nullptr) {
        return false;
    }

    auto used = usedPercent->AsInt();
    auto limit = limitWindowSeconds->AsInt();
    auto resetAfter = resetAfterSeconds->AsInt();
    auto resetAtValue = resetAt->AsNumber();
    if (!used.has_value() || !limit.has_value() || !resetAfter.has_value() || !resetAtValue.has_value()) {
        return false;
    }

    output->usedPercent = std::clamp(*used, 0, 100);
    output->remainingPercent = 100 - output->usedPercent;
    output->windowSeconds = std::max(*limit, 0);
    output->resetAfterSeconds = std::max(*resetAfter, 0);
    output->resetAtUnixSeconds = static_cast<long long>(*resetAtValue);
    if (output->windowSeconds > 0 && output->resetAtUnixSeconds > 0) {
        output->startAtUnixSeconds = output->resetAtUnixSeconds - output->windowSeconds;
        output->hasStartAt = true;
    }
    output->available = true;
    return true;
}

// Windows shorter than this are treated as the 5-hour session lane.
constexpr int kShortRateLimitWindowMaxSeconds = 12 * 60 * 60;

bool IsShortRateLimitWindow(const UsageWindow& window) {
    return window.available && window.windowSeconds > 0
        && window.windowSeconds <= kShortRateLimitWindowMaxSeconds;
}

void AssignRateLimitWindows(UsageWindow primary, UsageWindow secondary, UsageSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return;
    }

    snapshot->fiveHour = {};
    snapshot->weekly = {};

    if (primary.available && secondary.available) {
        // Prefer duration classification; fall back to classic primary=session, secondary=weekly.
        if (IsShortRateLimitWindow(primary) && !IsShortRateLimitWindow(secondary)) {
            snapshot->fiveHour = primary;
            snapshot->weekly = secondary;
        } else if (!IsShortRateLimitWindow(primary) && IsShortRateLimitWindow(secondary)) {
            snapshot->weekly = primary;
            snapshot->fiveHour = secondary;
        } else if (primary.windowSeconds <= secondary.windowSeconds) {
            snapshot->fiveHour = primary;
            snapshot->weekly = secondary;
        } else {
            snapshot->weekly = primary;
            snapshot->fiveHour = secondary;
        }
        return;
    }

    if (primary.available) {
        if (IsShortRateLimitWindow(primary)) {
            snapshot->fiveHour = primary;
        } else {
            // Current Codex API: only weekly remains, returned as primary_window.
            snapshot->weekly = primary;
        }
        return;
    }

    if (secondary.available) {
        if (IsShortRateLimitWindow(secondary)) {
            snapshot->fiveHour = secondary;
        } else {
            snapshot->weekly = secondary;
        }
    }
}

std::optional<std::string> Base64UrlDecode(const std::string& input) {
    std::string normalized;
    normalized.reserve(input.size() + 4);
    for (char ch : input) {
        if (ch == '-') {
            normalized.push_back('+');
        } else if (ch == '_') {
            normalized.push_back('/');
        } else {
            normalized.push_back(ch);
        }
    }
    while (normalized.size() % 4 != 0) {
        normalized.push_back('=');
    }

    DWORD required = 0;
    if (!CryptStringToBinaryA(
            normalized.c_str(),
            static_cast<DWORD>(normalized.size()),
            CRYPT_STRING_BASE64,
            nullptr,
            &required,
            nullptr,
            nullptr) || required == 0) {
        return std::nullopt;
    }

    std::string output(required, '\0');
    if (!CryptStringToBinaryA(
            normalized.c_str(),
            static_cast<DWORD>(normalized.size()),
            CRYPT_STRING_BASE64,
            reinterpret_cast<BYTE*>(output.data()),
            &required,
            nullptr,
            nullptr)) {
        return std::nullopt;
    }
    output.resize(required);
    return output;
}

std::optional<std::string> DecodeJwtPayloadJson(const std::string& jwt) {
    const size_t firstDot = jwt.find('.');
    if (firstDot == std::string::npos) {
        return std::nullopt;
    }
    const size_t secondDot = jwt.find('.', firstDot + 1);
    if (secondDot == std::string::npos) {
        return std::nullopt;
    }
    return Base64UrlDecode(jwt.substr(firstDot + 1, secondDot - firstDot - 1));
}

std::optional<long long> JwtExpUnixSeconds(const std::string& jwt) {
    std::optional<std::string> payloadJson = DecodeJwtPayloadJson(jwt);
    if (!payloadJson.has_value()) {
        return std::nullopt;
    }
    jsonlite::Parser parser(*payloadJson);
    std::optional<jsonlite::Value> root = parser.Parse();
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

bool ReplaceJsonStringField(std::string* json, const std::string& key, const std::string& newValue) {
    if (json == nullptr || key.empty()) {
        return false;
    }
    const std::string needle = "\"" + key + "\"";
    size_t pos = json->find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json->find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < json->size() && ((*json)[pos] == ' ' || (*json)[pos] == '\t' || (*json)[pos] == '\r' || (*json)[pos] == '\n')) {
        ++pos;
    }
    if (pos >= json->size() || (*json)[pos] != '"') {
        return false;
    }
    const size_t valueStart = pos + 1;
    size_t valueEnd = valueStart;
    while (valueEnd < json->size()) {
        if ((*json)[valueEnd] == '\\' && valueEnd + 1 < json->size()) {
            valueEnd += 2;
            continue;
        }
        if ((*json)[valueEnd] == '"') {
            break;
        }
        ++valueEnd;
    }
    if (valueEnd >= json->size()) {
        return false;
    }

    std::string escaped;
    escaped.reserve(newValue.size() + 8);
    for (char ch : newValue) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    json->replace(valueStart, valueEnd - valueStart, escaped);
    return true;
}

std::string EscapeJsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string CurrentUtcIso8601() {
    const std::time_t now = std::time(nullptr);
    std::tm utc = {};
    gmtime_s(&utc, &now);
    char buffer[40] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// Refresh one day before JWT exp (also covers already-expired tokens).
constexpr long long kAuthRefreshLeadSeconds = 24LL * 60 * 60;

bool TokenNeedsProactiveRefresh(const std::string& jwt, long long nowUnix) {
    const auto exp = JwtExpUnixSeconds(jwt);
    if (!exp.has_value()) {
        // Unknown expiry: do not thrash refresh every poll; rely on 401 retry.
        return false;
    }
    return *exp <= nowUnix + kAuthRefreshLeadSeconds;
}

bool CredentialsNeedProactiveRefresh(const CodexUsageFetcher::AuthCredentials& credentials) {
    if (credentials.refreshToken.empty()) {
        return false;
    }
    const long long nowUnix = static_cast<long long>(std::time(nullptr));
    if (!credentials.accessToken.empty() && TokenNeedsProactiveRefresh(credentials.accessToken, nowUnix)) {
        return true;
    }
    if (!credentials.idToken.empty() && TokenNeedsProactiveRefresh(credentials.idToken, nowUnix)) {
        return true;
    }
    return false;
}

}  // namespace

UsageSnapshot CodexUsageFetcher::Fetch() const {
    UsageSnapshot snapshot;

    std::wstring errorMessage;
    std::optional<AuthCredentials> credentials = ReadAuthCredentials(&errorMessage);
    if (!credentials.has_value()) {
        snapshot.errorMessage = errorMessage;
        return snapshot;
    }

    // Proactive OAuth refresh only when access/id token is within 1 day of exp.
    // Failures fall back to existing tokens so usage still loads.
    if (CredentialsNeedProactiveRefresh(*credentials)) {
        std::wstring refreshError;
        RefreshAuthCredentials(&*credentials, &refreshError);
    }

    std::optional<std::string> usageJson = HttpGetUsageJson(*credentials, &errorMessage);
    if (!usageJson.has_value()) {
        // Reactive refresh on auth failures even if not yet inside the 1-day window.
        if (!credentials->refreshToken.empty()
            && (errorMessage.find(L"HTTP 401") != std::wstring::npos
                || errorMessage.find(L"HTTP 403") != std::wstring::npos)) {
            std::wstring refreshError;
            if (RefreshAuthCredentials(&*credentials, &refreshError)) {
                usageJson = HttpGetUsageJson(*credentials, &errorMessage);
            }
        }
        if (!usageJson.has_value()) {
            snapshot.errorMessage = errorMessage;
            return snapshot;
        }
    }

    // Full replace from live usage every refresh.
    snapshot = ParseUsageJson(*usageJson, &errorMessage);
    if (!snapshot.success) {
        snapshot.errorMessage = errorMessage;
        return snapshot;
    }

    // Always re-apply plan dates/type from the freshest id_token (overwrites stale values).
    if (!credentials->idToken.empty()) {
        EnrichSubscriptionFromIdToken(&snapshot, credentials->idToken);
    }

    // Best-effort inventory; usage success is preserved even if this fails.
    std::wstring resetError;
    std::optional<std::string> resetJson = HttpGetRateLimitResetCreditsJson(*credentials, &resetError);
    if (resetJson.has_value()) {
        snapshot.resetCredits = ParseRateLimitResetCreditsJson(*resetJson, &resetError);
        if (!snapshot.resetCredits.fetched) {
            snapshot.resetCredits.errorMessage = resetError;
        }
    } else {
        snapshot.resetCredits = {};
        snapshot.resetCredits.fetched = false;
        snapshot.resetCredits.errorMessage = resetError;
    }

    return snapshot;
}

TokenRefreshResult CodexUsageFetcher::ForceRefreshAuthTokens() const {
    TokenRefreshResult result;

    std::wstring errorMessage;
    std::optional<AuthCredentials> credentials = ReadAuthCredentials(&errorMessage);
    if (!credentials.has_value()) {
        result.errorMessage = errorMessage;
        return result;
    }
    if (credentials->refreshToken.empty()) {
        result.errorMessage = L"auth.json missing tokens.refresh_token";
        return result;
    }

    if (!RefreshAuthCredentials(&*credentials, &errorMessage)) {
        result.errorMessage = errorMessage.empty() ? L"token refresh failed" : errorMessage;
        return result;
    }

    result.success = true;
    result.wroteAuthFile = !credentials->authPath.empty();
    return result;
}

ConsumeResetCreditResult CodexUsageFetcher::ConsumeRateLimitResetCredit(const std::wstring& redeemRequestId) const {
    ConsumeResetCreditResult result;
    if (redeemRequestId.empty()) {
        result.errorMessage = L"redeem_request_id is required";
        return result;
    }

    std::wstring errorMessage;
    std::optional<AuthCredentials> credentials = ReadAuthCredentials(&errorMessage);
    if (!credentials.has_value()) {
        result.errorMessage = errorMessage;
        return result;
    }

    // Prefer a fresh access token for write paths (spend must not fail on near-expiry).
    if (!credentials->refreshToken.empty()) {
        std::wstring refreshError;
        RefreshAuthCredentials(&*credentials, &refreshError);
    }

    if (!HttpPostConsumeRateLimitResetCredit(*credentials, redeemRequestId, &errorMessage)) {
        if (!credentials->refreshToken.empty()
            && (errorMessage.find(L"HTTP 401") != std::wstring::npos
                || errorMessage.find(L"HTTP 403") != std::wstring::npos)) {
            std::wstring refreshError;
            if (RefreshAuthCredentials(&*credentials, &refreshError)) {
                errorMessage.clear();
                if (HttpPostConsumeRateLimitResetCredit(*credentials, redeemRequestId, &errorMessage)) {
                    result.success = true;
                    return result;
                }
            }
        }
        result.errorMessage = errorMessage;
        return result;
    }

    result.success = true;
    return result;
}

ReleaseVersionInfo CodexUsageFetcher::FetchLatestRelease() const {
    ReleaseVersionInfo info;

    std::wstring errorMessage;
    std::optional<std::string> releaseJson = HttpGetLatestReleaseJson(&errorMessage);
    if (!releaseJson.has_value()) {
        info.errorMessage = errorMessage;
        return info;
    }

    info = ParseLatestReleaseJson(*releaseJson, &errorMessage);
    if (!info.success) {
        info.errorMessage = errorMessage;
    }
    return info;
}

ModelIqSnapshot CodexUsageFetcher::FetchModelIq(RadarMetricKind kind) const {
    ModelIqSnapshot snapshot;
    snapshot.kind = kind;

    const wchar_t* path = kind == RadarMetricKind::VisualSpatial
        ? L"/api/visual-spatial-reasoning?refresh=1"
        : L"/api/intelligence-efficiency-metrics?refresh=1";

    std::wstring errorMessage;
    std::optional<std::string> radarJson = HttpGetCodexRadarMetricsJson(path, &errorMessage);
    if (!radarJson.has_value()) {
        snapshot.errorMessage = errorMessage.empty() ? L"CodexRadar request failed" : errorMessage;
        return snapshot;
    }

    snapshot = ParseModelIqJson(*radarJson, &errorMessage);
    snapshot.kind = kind;
    if (!snapshot.success) {
        snapshot.errorMessage = errorMessage;
    }
    return snapshot;
}

std::wstring CodexUsageFetcher::ResolveAuthJsonPath() const {
    // Prefer auth.json next to the executable; fall back to CODEX_HOME / ~/.codex.
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
        const std::wstring localAuth =
            JoinPath(std::filesystem::path(modulePath).parent_path().wstring(), L"auth.json");
        if (std::filesystem::exists(localAuth)) {
            return localAuth;
        }
    }

    if (auto codexHome = ReadEnv(L"CODEX_HOME"); codexHome.has_value() && !codexHome->empty()) {
        return JoinPath(*codexHome, L"auth.json");
    }

    if (auto userProfile = ReadEnv(L"USERPROFILE"); userProfile.has_value() && !userProfile->empty()) {
        return JoinPath(JoinPath(*userProfile, L".codex"), L"auth.json");
    }

    return L".codex\\auth.json";
}

std::optional<CodexUsageFetcher::AuthCredentials> CodexUsageFetcher::ReadAuthCredentials(
    std::wstring* errorMessage) const {
    const std::wstring authPath = ResolveAuthJsonPath();
    std::optional<std::string> jsonText = LoadFileUtf8(authPath, errorMessage);
    if (!jsonText.has_value()) {
        return std::nullopt;
    }

    jsonlite::Parser parser(*jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"auth.json parse failed: " + Utf8ToWide(parser.Error());
        }
        return std::nullopt;
    }

    const jsonlite::Value* tokens = root->Find("tokens");
    const jsonlite::Value* accessToken = tokens != nullptr ? tokens->Find("access_token") : nullptr;
    auto token = accessToken != nullptr ? accessToken->AsString() : std::nullopt;
    if (!token.has_value() || token->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"auth.json missing tokens.access_token";
        }
        return std::nullopt;
    }

    AuthCredentials credentials;
    credentials.authPath = authPath;
    credentials.accessToken = std::string(*token);

    const jsonlite::Value* accountIdNode = tokens != nullptr ? tokens->Find("account_id") : nullptr;
    if (accountIdNode == nullptr) {
        accountIdNode = root->Find("account_id");
    }
    if (auto accountId = accountIdNode != nullptr ? accountIdNode->AsString() : std::nullopt;
        accountId.has_value()) {
        credentials.accountId = std::string(*accountId);
    }

    const jsonlite::Value* idTokenNode = tokens != nullptr ? tokens->Find("id_token") : nullptr;
    if (auto idToken = idTokenNode != nullptr ? idTokenNode->AsString() : std::nullopt;
        idToken.has_value()) {
        credentials.idToken = std::string(*idToken);
    }

    const jsonlite::Value* refreshTokenNode = tokens != nullptr ? tokens->Find("refresh_token") : nullptr;
    if (auto refreshToken = refreshTokenNode != nullptr ? refreshTokenNode->AsString() : std::nullopt;
        refreshToken.has_value()) {
        credentials.refreshToken = std::string(*refreshToken);
    }

    return credentials;
}

bool CodexUsageFetcher::RefreshAuthCredentials(AuthCredentials* credentials, std::wstring* errorMessage) const {
    if (credentials == nullptr || credentials->refreshToken.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"missing refresh_token";
        }
        return false;
    }

    const std::string body =
        std::string("{\"client_id\":\"app_EMoamEEZ73f0CkXaXp7hrann\",")
        + "\"grant_type\":\"refresh_token\","
        + "\"refresh_token\":\"" + EscapeJsonString(credentials->refreshToken) + "\","
        + "\"scope\":\"openid profile email offline_access\"}";

    std::vector<std::wstring> headers = {
        L"Content-Type: application/json",
        L"Accept: application/json",
    };

    std::optional<std::string> response = HttpExchange(
        L"CodexUsageBar/0.1",
        L"auth.openai.com",
        L"/oauth/token",
        L"POST",
        headers,
        &body,
        errorMessage);
    if (!response.has_value()) {
        return false;
    }

    jsonlite::Parser parser(*response);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"token refresh JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return false;
    }

    const jsonlite::Value* accessToken = root->Find("access_token");
    auto access = accessToken != nullptr ? accessToken->AsString() : std::nullopt;
    if (!access.has_value() || access->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"token refresh missing access_token";
        }
        return false;
    }

    credentials->accessToken = std::string(*access);

    if (const jsonlite::Value* idToken = root->Find("id_token"); idToken != nullptr) {
        if (auto id = idToken->AsString(); id.has_value() && !id->empty()) {
            credentials->idToken = std::string(*id);
        }
    }
    if (const jsonlite::Value* refreshToken = root->Find("refresh_token"); refreshToken != nullptr) {
        if (auto refresh = refreshToken->AsString(); refresh.has_value() && !refresh->empty()) {
            credentials->refreshToken = std::string(*refresh);
        }
    }

    // Best-effort persist so the next refresh and external tools see the new claims.
    PersistAuthCredentials(*credentials, errorMessage);
    return true;
}

bool CodexUsageFetcher::PersistAuthCredentials(
    const AuthCredentials& credentials,
    std::wstring* errorMessage) const {
    if (credentials.authPath.empty()) {
        return false;
    }

    std::optional<std::string> jsonText = LoadFileUtf8(credentials.authPath, errorMessage);
    if (!jsonText.has_value()) {
        return false;
    }

    std::string updated = *jsonText;
    bool changed = false;
    if (!credentials.accessToken.empty()) {
        changed = ReplaceJsonStringField(&updated, "access_token", credentials.accessToken) || changed;
    }
    if (!credentials.idToken.empty()) {
        changed = ReplaceJsonStringField(&updated, "id_token", credentials.idToken) || changed;
    }
    if (!credentials.refreshToken.empty()) {
        changed = ReplaceJsonStringField(&updated, "refresh_token", credentials.refreshToken) || changed;
    }
    // last_refresh may be ISO string at root.
    if (!ReplaceJsonStringField(&updated, "last_refresh", CurrentUtcIso8601())) {
        // optional field
    }

    if (!changed) {
        return false;
    }
    return WriteFileUtf8(credentials.authPath, updated, errorMessage);
}

std::optional<std::string> CodexUsageFetcher::LoadFileUtf8(const std::wstring& path, std::wstring* errorMessage) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (errorMessage != nullptr) {
            *errorMessage = L"cannot open " + path;
        }
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool CodexUsageFetcher::WriteFileUtf8(
    const std::wstring& path,
    const std::string& content,
    std::wstring* errorMessage) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = L"cannot write " + path;
        }
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = L"failed writing " + path;
        }
        return false;
    }
    return true;
}

std::optional<std::string> CodexUsageFetcher::HttpGetUsageJson(
    const AuthCredentials& credentials,
    std::wstring* errorMessage) const {
    auto headers = BuildCodexAuthHeaders(credentials.accessToken, credentials.accountId, false);
    headers.push_back(L"Cache-Control: no-cache");
    headers.push_back(L"Pragma: no-cache");
    return HttpGetJson(
        L"CodexUsageBar/0.1",
        L"chatgpt.com",
        L"/backend-api/wham/usage",
        headers,
        errorMessage);
}

std::optional<std::string> CodexUsageFetcher::HttpGetRateLimitResetCreditsJson(
    const AuthCredentials& credentials,
    std::wstring* errorMessage) const {
    auto headers = BuildCodexAuthHeaders(credentials.accessToken, credentials.accountId, true);
    headers.push_back(L"Cache-Control: no-cache");
    headers.push_back(L"Pragma: no-cache");
    return HttpGetJson(
        L"CodexUsageBar/0.1",
        L"chatgpt.com",
        L"/backend-api/wham/rate-limit-reset-credits",
        headers,
        errorMessage);
}

bool CodexUsageFetcher::HttpPostConsumeRateLimitResetCredit(
    const AuthCredentials& credentials,
    const std::wstring& redeemRequestId,
    std::wstring* errorMessage) const {
    // Body must be UTF-8 JSON. redeemRequestId is expected to be ASCII UUID text.
    std::string body = "{\"redeem_request_id\":\"";
    for (wchar_t ch : redeemRequestId) {
        if (ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\') {
            body.push_back(static_cast<char>(ch));
        }
    }
    body += "\"}";

    std::vector<std::wstring> headers = BuildCodexAuthHeaders(
        credentials.accessToken, credentials.accountId, true);
    headers.push_back(L"Content-Type: application/json");
    // Write path mirrors the official CLI user agent used by Quotio Desktop.
    headers.push_back(L"User-Agent: codex_cli_rs/0.76.0 (Windows; x86_64) CodexUsageBar");

    std::optional<std::string> response = HttpExchange(
        L"CodexUsageBar/0.1",
        L"chatgpt.com",
        L"/backend-api/wham/rate-limit-reset-credits/consume",
        L"POST",
        headers,
        &body,
        errorMessage);
    return response.has_value();
}

std::optional<std::string> CodexUsageFetcher::HttpGetLatestReleaseJson(std::wstring* errorMessage) const {
    return HttpGetJson(
        L"CodexUsageBar/0.1",
        L"api.github.com",
        L"/repos/luodaoyi/codex-useage-win/releases/latest",
        {
            L"Accept: application/vnd.github+json",
            L"X-GitHub-Api-Version: 2022-11-28",
            L"User-Agent: CodexUsageBar"
        },
        errorMessage);
}

std::optional<std::string> CodexUsageFetcher::HttpGetCodexRadarMetricsJson(
    const wchar_t* path,
    std::wstring* errorMessage) const {
    return HttpGetJson(
        L"CodexUsageBar/0.1",
        L"codexradar.com",
        path != nullptr ? path : L"/api/intelligence-efficiency-metrics?refresh=1",
        {
            L"Accept: application/json",
            L"Cache-Control: no-cache",
            L"User-Agent: CodexUsageBar"
        },
        errorMessage);
}

UsageSnapshot CodexUsageFetcher::ParseUsageJson(const std::string& jsonText, std::wstring* errorMessage) const {
    UsageSnapshot snapshot;

    jsonlite::Parser parser(jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"usage JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return snapshot;
    }

    const jsonlite::Value* email = root->Find("email");
    const jsonlite::Value* planType = root->Find("plan_type");
    const jsonlite::Value* rateLimit = root->Find("rate_limit");
    const jsonlite::Value* primaryWindow = rateLimit != nullptr ? rateLimit->Find("primary_window") : nullptr;
    const jsonlite::Value* secondaryWindow = rateLimit != nullptr ? rateLimit->Find("secondary_window") : nullptr;

    UsageWindow primary;
    UsageWindow secondary;
    ExtractWindow(primaryWindow, &primary);
    ExtractWindow(secondaryWindow, &secondary);
    AssignRateLimitWindows(primary, secondary, &snapshot);
    if (!snapshot.fiveHour.available && !snapshot.weekly.available) {
        if (errorMessage != nullptr) {
            *errorMessage = L"usage payload missing rate_limit windows";
        }
        return snapshot;
    }

    if (auto emailString = email != nullptr ? email->AsString() : std::nullopt; emailString.has_value()) {
        snapshot.email = Utf8ToWide(std::string(*emailString));
    }
    if (auto planTypeString = planType != nullptr ? planType->AsString() : std::nullopt; planTypeString.has_value()) {
        snapshot.planType = Utf8ToWide(std::string(*planTypeString));
    }

    snapshot.success = true;
    return snapshot;
}

void CodexUsageFetcher::EnrichSubscriptionFromIdToken(
    UsageSnapshot* snapshot,
    const std::string& idToken) const {
    if (snapshot == nullptr || idToken.empty()) {
        return;
    }

    // Always clear previous plan period so a failed re-parse cannot keep stale dates.
    snapshot->hasPlanStart = false;
    snapshot->hasPlanUntil = false;
    snapshot->planStartUnixSeconds = 0;
    snapshot->planUntilUnixSeconds = 0;

    std::optional<std::string> payloadJson = DecodeJwtPayloadJson(idToken);
    if (!payloadJson.has_value()) {
        return;
    }

    jsonlite::Parser parser(*payloadJson);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        return;
    }

    // Prefer nested OpenAI auth claim; also accept root-level aliases if present.
    const jsonlite::Value* auth = root->Find("https://api.openai.com/auth");
    const jsonlite::Value* claimRoot = auth != nullptr ? auth : &*root;

    if (auto plan = claimRoot->Find("chatgpt_plan_type") != nullptr
            ? claimRoot->Find("chatgpt_plan_type")->AsString()
            : std::nullopt;
        plan.has_value() && !plan->empty()) {
        // Live claim wins over usage payload for plan label when present.
        snapshot->planType = Utf8ToWide(std::string(*plan));
    }

    if (auto email = root->Find("email") != nullptr ? root->Find("email")->AsString() : std::nullopt;
        email.has_value() && !email->empty()) {
        snapshot->email = Utf8ToWide(std::string(*email));
    }

    if (auto start = claimRoot->Find("chatgpt_subscription_active_start") != nullptr
            ? claimRoot->Find("chatgpt_subscription_active_start")->AsString()
            : std::nullopt;
        start.has_value()) {
        if (auto unix = ParseIso8601UnixSeconds(std::string(*start)); unix.has_value()) {
            snapshot->planStartUnixSeconds = *unix;
            snapshot->hasPlanStart = true;
        }
    }

    if (auto until = claimRoot->Find("chatgpt_subscription_active_until") != nullptr
            ? claimRoot->Find("chatgpt_subscription_active_until")->AsString()
            : std::nullopt;
        until.has_value()) {
        if (auto unix = ParseIso8601UnixSeconds(std::string(*until)); unix.has_value()) {
            snapshot->planUntilUnixSeconds = *unix;
            snapshot->hasPlanUntil = true;
        }
    }
}

RateLimitResetCreditsInfo CodexUsageFetcher::ParseRateLimitResetCreditsJson(
    const std::string& jsonText,
    std::wstring* errorMessage) const {
    RateLimitResetCreditsInfo info;

    jsonlite::Parser parser(jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"reset credits JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return info;
    }

    const jsonlite::Value* availableCountNode = root->Find("available_count");
    auto availableCount = availableCountNode != nullptr ? availableCountNode->AsInt() : std::nullopt;
    if (!availableCount.has_value() || *availableCount < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = L"reset credits payload missing available_count";
        }
        return info;
    }
    info.availableCount = *availableCount;

    const long long nowUnix = static_cast<long long>(std::time(nullptr));
    const jsonlite::Value* creditsNode = root->Find("credits");
    if (const auto* credits = creditsNode != nullptr ? creditsNode->AsArray() : nullptr; credits != nullptr) {
        for (const jsonlite::Value& creditNode : *credits) {
            const jsonlite::Value* statusNode = creditNode.Find("status");
            auto status = statusNode != nullptr ? statusNode->AsString() : std::nullopt;
            if (!status.has_value() || *status != "available") {
                continue;
            }

            RateLimitResetCredit credit;
            credit.status = L"available";

            if (auto id = creditNode.Find("id") != nullptr ? creditNode.Find("id")->AsString() : std::nullopt;
                id.has_value()) {
                credit.id = Utf8ToWide(std::string(*id));
            }
            if (auto resetType = creditNode.Find("reset_type") != nullptr
                    ? creditNode.Find("reset_type")->AsString()
                    : std::nullopt;
                resetType.has_value()) {
                credit.resetType = Utf8ToWide(std::string(*resetType));
            }
            if (auto title = creditNode.Find("title") != nullptr ? creditNode.Find("title")->AsString() : std::nullopt;
                title.has_value()) {
                credit.title = Utf8ToWide(std::string(*title));
            }
            if (auto description = creditNode.Find("description") != nullptr
                    ? creditNode.Find("description")->AsString()
                    : std::nullopt;
                description.has_value()) {
                credit.description = Utf8ToWide(std::string(*description));
            }
            if (auto grantedAt = creditNode.Find("granted_at") != nullptr
                    ? creditNode.Find("granted_at")->AsString()
                    : std::nullopt;
                grantedAt.has_value()) {
                if (auto unix = ParseIso8601UnixSeconds(std::string(*grantedAt)); unix.has_value()) {
                    credit.grantedAtUnixSeconds = *unix;
                }
            }

            const jsonlite::Value* expiresAtNode = creditNode.Find("expires_at");
            if (expiresAtNode != nullptr && !expiresAtNode->IsNull()) {
                if (auto expiresAt = expiresAtNode->AsString(); expiresAt.has_value()) {
                    if (auto unix = ParseIso8601UnixSeconds(std::string(*expiresAt)); unix.has_value()) {
                        credit.hasExpiry = true;
                        credit.expiresAtUnixSeconds = *unix;
                    }
                }
            }

            if (credit.hasExpiry && credit.expiresAtUnixSeconds <= nowUnix) {
                continue;
            }

            info.availableCredits.push_back(std::move(credit));
        }
    }

    std::sort(info.availableCredits.begin(), info.availableCredits.end(),
        [](const RateLimitResetCredit& lhs, const RateLimitResetCredit& rhs) {
            if (lhs.hasExpiry != rhs.hasExpiry) {
                return lhs.hasExpiry && !rhs.hasExpiry;
            }
            if (lhs.hasExpiry && rhs.hasExpiry && lhs.expiresAtUnixSeconds != rhs.expiresAtUnixSeconds) {
                return lhs.expiresAtUnixSeconds < rhs.expiresAtUnixSeconds;
            }
            return lhs.id < rhs.id;
        });

    // Prefer locally filtered inventory count when the list is present.
    if (!info.availableCredits.empty() || creditsNode != nullptr) {
        info.availableCount = static_cast<int>(info.availableCredits.size());
    }

    if (!info.availableCredits.empty() && info.availableCredits.front().hasExpiry) {
        info.hasNextExpiry = true;
        info.nextExpiresAtUnixSeconds = info.availableCredits.front().expiresAtUnixSeconds;
    }

    info.fetched = true;
    return info;
}

ReleaseVersionInfo CodexUsageFetcher::ParseLatestReleaseJson(const std::string& jsonText, std::wstring* errorMessage) const {
    ReleaseVersionInfo info;

    jsonlite::Parser parser(jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"latest release JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return info;
    }

    const jsonlite::Value* tagName = root->Find("tag_name");
    auto tag = tagName != nullptr ? tagName->AsString() : std::nullopt;
    if (!tag.has_value() || tag->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"latest release payload missing tag_name";
        }
        return info;
    }

    info.latestTag = Utf8ToWide(std::string(*tag));
    info.success = true;
    return info;
}

ModelIqSnapshot CodexUsageFetcher::ParseModelIqJson(const std::string& jsonText, std::wstring* errorMessage) const {
    ModelIqSnapshot snapshot;

    jsonlite::Parser parser(jsonText);
    std::optional<jsonlite::Value> root = parser.Parse();
    if (!root.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"CodexRadar JSON parse failed: " + Utf8ToWide(parser.Error());
        }
        return snapshot;
    }

    if (const jsonlite::Value* updatedAt = root->Find("source_updated_at"); updatedAt != nullptr) {
        if (auto text = updatedAt->AsString(); text.has_value()) {
            snapshot.updatedAt = Utf8ToWide(std::string(*text));
        }
    }

    const jsonlite::Value* pointsNode = root->Find("points");
    const jsonlite::Value::Array* points = pointsNode != nullptr ? pointsNode->AsArray() : nullptr;
    if (points == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = L"CodexRadar payload missing points";
        }
        return snapshot;
    }

    std::map<std::wstring, ModelIqScore> uniqueScores;
    for (const jsonlite::Value& point : *points) {
        if (!point.IsObject()) {
            continue;
        }
        ModelIqScore score;
        if (!FillModelIqScoreFromPoint(point, &score)) {
            continue;
        }
        const std::wstring key = ModelIqDedupKey(score);
        if (key.empty() || uniqueScores.find(key) != uniqueScores.end()) {
            continue;
        }
        uniqueScores.emplace(key, std::move(score));
    }

    snapshot.scores.reserve(uniqueScores.size());
    for (auto& entry : uniqueScores) {
        snapshot.scores.push_back(std::move(entry.second));
    }
    std::sort(snapshot.scores.begin(), snapshot.scores.end(),
        [](const ModelIqScore& lhs, const ModelIqScore& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return lhs.label < rhs.label;
        });

    if (snapshot.scores.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = L"CodexRadar points has no scores";
        }
        return snapshot;
    }

    snapshot.success = true;
    return snapshot;
}
