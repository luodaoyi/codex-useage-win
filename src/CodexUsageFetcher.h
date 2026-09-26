#pragma once

#include <optional>
#include <string>
#include <vector>

struct UsageWindow {
    // False when the API omits this window (e.g. 5h primary removed).
    bool available = false;
    int usedPercent = 0;
    int remainingPercent = 100;
    int windowSeconds = 0;
    int resetAfterSeconds = 0;
    long long resetAtUnixSeconds = 0;
    // Derived: resetAt - windowSeconds when both are valid.
    long long startAtUnixSeconds = 0;
    bool hasStartAt = false;
};

struct RateLimitResetCredit {
    std::wstring id;
    std::wstring resetType;
    std::wstring status;
    long long grantedAtUnixSeconds = 0;
    // 0 means no expiry / unknown.
    long long expiresAtUnixSeconds = 0;
    bool hasExpiry = false;
    std::wstring title;
    std::wstring description;
};

struct RateLimitResetCreditsInfo {
    bool fetched = false;
    int availableCount = 0;
    std::wstring errorMessage;
    // Still-available inventory after local filtering (status + expiry).
    std::vector<RateLimitResetCredit> availableCredits;
    // Earliest expiring available credit; 0 / false if none or no expiry.
    long long nextExpiresAtUnixSeconds = 0;
    bool hasNextExpiry = false;
};

struct UsageSnapshot {
    bool success = false;
    std::wstring email;
    std::wstring planType;
    // From id_token chatgpt_subscription_active_start / until when present.
    long long planStartUnixSeconds = 0;
    long long planUntilUnixSeconds = 0;
    bool hasPlanStart = false;
    bool hasPlanUntil = false;
    std::wstring errorMessage;
    UsageWindow fiveHour;
    UsageWindow weekly;
    RateLimitResetCreditsInfo resetCredits;
};

struct ReleaseVersionInfo {
    bool success = false;
    std::wstring latestTag;
    std::wstring errorMessage;
};

struct ConsumeResetCreditResult {
    bool success = false;
    std::wstring errorMessage;
};

struct TokenRefreshResult {
    bool success = false;
    bool wroteAuthFile = false;
    bool attempted = false;
    std::wstring errorMessage;
};

enum class RadarMetricKind {
    SoftwareEngineering = 0,
    VisualSpatial = 1,
};

struct ModelIqScore {
    std::wstring label;
    std::wstring model;
    std::wstring effort;
    std::wstring status;
    std::wstring familyKey;
    std::wstring familyLabel;
    double score = 0.0;
    double averagePriceUsd = 0.0;
    bool hasPrice = false;
    double averageMinutes = 0.0;
    bool hasDuration = false;
    int passed = 0;
    int tasks = 0;
};

struct ModelIqSnapshot {
    bool success = false;
    RadarMetricKind kind = RadarMetricKind::SoftwareEngineering;
    std::wstring errorMessage;
    std::wstring updatedAt;
    std::wstring benchmarkId;
    std::vector<ModelIqScore> scores;
};

class CodexUsageFetcher {
public:
    struct AuthCredentials {
        std::string accessToken;
        std::string accountId;
        std::string idToken;
        std::string refreshToken;
        std::wstring authPath;
    };

    // One copied credential file per account. id is "accounts\\<file>.json".
    // Usage never reads the source auth.json; import copies it first.
    struct AuthAccount {
        std::wstring id;
        std::wstring label;
        std::wstring path;
    };

    struct AuthImportResult {
        bool success = false;
        std::wstring authId;
        std::wstring errorMessage;
    };

    // Imported copies beside the executable only, sorted by filename.
    std::vector<AuthAccount> ListAuthAccounts() const;
    // Empty or unknown id selects the first imported copy. Empty if none exist.
    std::wstring ResolveActiveAuthPath(const std::wstring& activeAuthId) const;
    // Copy sourcePath into accounts\. Does not modify the source file.
    AuthImportResult ImportAuthFile(const std::wstring& sourcePath) const;

    UsageSnapshot Fetch(const std::wstring& authPath) const;
    ReleaseVersionInfo FetchLatestRelease() const;
    ModelIqSnapshot FetchModelIq(RadarMetricKind kind) const;

    // Force OAuth refresh and write tokens back to this account's file.
    TokenRefreshResult ForceRefreshAuthTokens(const std::wstring& authPath) const;
    // Refresh only when the access_token is inside the 1-day lead. Otherwise skipped.
    TokenRefreshResult RefreshAuthIfNeeded(const std::wstring& authPath) const;
    UsageSnapshot ParseUsageJson(const std::string& jsonText, std::wstring* errorMessage) const;

    // Spends one real rate-limit reset credit. Do not call casually.
    ConsumeResetCreditResult ConsumeRateLimitResetCredit(
        const std::wstring& redeemRequestId,
        const std::wstring& authPath) const;

private:
    std::optional<AuthCredentials> ReadAuthCredentials(
        const std::wstring& authPath,
        std::wstring* errorMessage) const;
    // Refresh OAuth tokens and persist updated tokens/id_token back to auth.json.
    bool RefreshAuthCredentials(AuthCredentials* credentials, std::wstring* errorMessage) const;
    bool PersistAuthCredentials(const AuthCredentials& credentials, std::wstring* errorMessage) const;
    std::optional<std::string> LoadFileUtf8(const std::wstring& path, std::wstring* errorMessage) const;
    bool WriteFileUtf8(const std::wstring& path, const std::string& content, std::wstring* errorMessage) const;
    std::optional<std::string> HttpGetUsageJson(const AuthCredentials& credentials, std::wstring* errorMessage) const;
    std::optional<std::string> HttpGetRateLimitResetCreditsJson(
        const AuthCredentials& credentials,
        std::wstring* errorMessage) const;
    std::optional<std::string> HttpGetLatestReleaseJson(std::wstring* errorMessage) const;
    std::optional<std::string> HttpGetCodexRadarMetricsJson(
        const std::wstring& host,
        const wchar_t* path,
        std::wstring* errorMessage) const;
    bool HttpPostConsumeRateLimitResetCredit(
        const AuthCredentials& credentials,
        const std::wstring& redeemRequestId,
        std::wstring* errorMessage) const;
    void EnrichSubscriptionFromIdToken(UsageSnapshot* snapshot, const std::string& idToken) const;
    RateLimitResetCreditsInfo ParseRateLimitResetCreditsJson(const std::string& jsonText, std::wstring* errorMessage) const;
    ReleaseVersionInfo ParseLatestReleaseJson(const std::string& jsonText, std::wstring* errorMessage) const;
    ModelIqSnapshot ParseModelIqJson(const std::string& jsonText, std::wstring* errorMessage) const;
};
