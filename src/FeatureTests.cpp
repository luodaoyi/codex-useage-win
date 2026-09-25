#include "AccountLibrary.h"
#include "AppUpdate.h"
#include "BrowserSignIn.h"
#include "GrokBilling.h"
#include "ProxyConfig.h"
#include "QuotaEstimate.h"
#include "ResetStatus.h"
#include "SessionIndex.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int gFailures = 0;

void Expect(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL " << name << "\n";
        ++gFailures;
    } else {
        std::cout << "ok " << name << "\n";
    }
}

std::wstring MakeTempRoot() {
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp) + L"codex-usage-tests-" + std::to_wstring(GetCurrentProcessId());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteBytes(const std::wstring& path, const std::string& content) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void TestAccounts() {
    const std::wstring root = MakeTempRoot();
    const std::wstring official = root + L"\\official\\auth.json";
    WriteBytes(official, "{\"access_token\":\"official-token\",\"account_id\":\"keep\"}");
    const auto before = std::filesystem::last_write_time(official);
    AccountLibrary library(root);
    const AccountOpResult missing = library.ImportText("{\"refresh_token\":\"x\"}", L"codex");
    Expect(!missing.success, "reject json without access_token");
    Expect(!std::filesystem::exists(library.AccountsDirectory() + L"\\account.json"), "no file after reject");

    const AccountOpResult imported = library.ImportText(
        "{\"type\":\"codex\",\"access_token\":\"test-access-token\",\"account_id\":\"acct-1\",\"id_token\":\"not-a-jwt\"}",
        L"codex");
    Expect(imported.success, "import text");
    const AccountOpResult again = library.ImportText(
        "{\"type\":\"codex\",\"access_token\":\"test-access-token-2\",\"account_id\":\"acct-1\"}",
        L"codex");
    Expect(again.success && again.id == imported.id, "same account_id overwrites one copy");
    Expect(library.List(L"codex").size() == 1, "one codex account");
    Expect(library.SetAlias(imported.id, L"work").success, "alias");
    Expect(library.List(L"codex").front().label == L"work", "alias is the label");
    const std::string index = ReadBytes(library.IndexPath());
    Expect(index.find("access_token") == std::string::npos, "index has no token");
    Expect(std::filesystem::last_write_time(official) == before, "official auth.json was not touched");
    Expect(library.Delete(imported.id).success, "delete copy");
    Expect(library.List(L"").empty(), "deleted account is gone");
    const AccountOpResult xai = library.ImportText(
        "{\"type\":\"xai\",\"access_token\":\"grok-token\",\"sub\":\"grok-1\",\"email\":\"grok@example.test\"}",
        L"codex");
    Expect(xai.success, "xai json imports even when the caller says codex");
    const std::vector<AccountEntry> grokAccounts = library.List(L"grok");
    Expect(grokAccounts.size() == 1 && grokAccounts.front().label == L"grok@example.test", "xai json is a grok account");
    Expect(library.List(L"codex").empty(), "xai json is not listed as codex");
    const std::string issPayload = R"({"iss":"https://auth.x.ai"})";
    std::string payload = issPayload;
    const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    for (size_t i = 0; i < payload.size(); i += 3) {
        const unsigned int n = (static_cast<unsigned char>(payload[i]) << 16)
            | ((i + 1 < payload.size() ? static_cast<unsigned char>(payload[i + 1]) : 0) << 8)
            | (i + 2 < payload.size() ? static_cast<unsigned char>(payload[i + 2]) : 0);
        encoded.push_back(table[(n >> 18) & 63]);
        encoded.push_back(table[(n >> 12) & 63]);
        if (i + 1 < payload.size()) {
            encoded.push_back(table[(n >> 6) & 63]);
        }
        if (i + 2 < payload.size()) {
            encoded.push_back(table[n & 63]);
        }
    }
    const AccountOpResult browser = library.ImportText(
        std::string("{\"access_token\":\"aaa.") + encoded + ".sig\",\"refresh_token\":\"r\",\"sub\":\"browser-grok\"}",
        L"codex");
    Expect(browser.success, "browser grok token without type imports");
    bool browserIsGrok = false;
    for (const AccountEntry& entry : library.List(L"grok")) {
        if (entry.id == browser.id) {
            browserIsGrok = true;
        }
    }
    Expect(browserIsGrok, "token issued by auth.x.ai is grok");
    std::filesystem::remove_all(root);
}

void TestResetStatus() {
    const ResetStatusInfo info = ParseResetStatusJson(
        "{\"events\":[{\"kind\":\"reset_completed\",\"resetType\":\"global\",\"announcedAt\":\"2026-09-12T08:09:17Z\",\"text\":\"Reset all propagated\",\"source\":{\"url\":\"https://example.test/reset\"}}]}");
    Expect(info.success, "parse reset status");
    Expect(info.summary == L"Reset all propagated", "reset summary");
    Expect(info.url == L"https://example.test/reset", "reset url");
}

void TestEstimate() {
    const std::wstring root = MakeTempRoot();
    const std::wstring history = root + L"\\quota-estimate-history.json";
    const QuotaEstimateView first = RecordWeeklyEstimate(history, 40, false, 0, true);
    const QuotaEstimateView second = RecordWeeklyEstimate(history, 55, false, 0, true);
    Expect(first.wroteHistory && second.wroteHistory, "estimate writes history");
    const std::string text = ReadBytes(history);
    Expect(text.find("\"week\"") != std::string::npos, "history has a week");
    const size_t firstWeek = text.find("\"week\"");
    Expect(firstWeek != std::string::npos && text.find("\"week\"", firstWeek + 6) == std::string::npos, "same week is one row");
    const auto stamp = std::filesystem::last_write_time(history);
    RecordWeeklyEstimate(history, 10, true, 12, false);
    Expect(std::filesystem::last_write_time(history) == stamp, "disabled estimate does not write");
    Expect(text.find("access_token") == std::string::npos, "estimate history has no token");
    std::filesystem::remove_all(root);
}

void TestSessions() {
    const std::wstring root = MakeTempRoot();
    const std::wstring logs = root + L"\\logs";
    const std::wstring index = root + L"\\index";
    WriteBytes(logs + L"\\a.jsonl",
        "{\"timestamp\":\"2026-09-24T01:00:00Z\",\"payload\":{\"model\":\"priced-test\",\"info\":{\"total_token_usage\":{\"input_tokens\":1000,\"cached_input_tokens\":0,\"output_tokens\":10}}}}\n");
    WriteBytes(logs + L"\\b.jsonl",
        "{\"timestamp\":\"2026-09-24T02:00:00Z\",\"payload\":{\"model\":\"mystery-model\",\"info\":{\"total_token_usage\":{\"input_tokens\":5,\"cached_input_tokens\":0,\"output_tokens\":5}}}}\n");
    SessionIndex sessions(logs, index);
    sessions.SetPriceForTest(L"priced-test", 2.0, 1.0, 8.0);
    const SessionScan first = sessions.Scan(UsageRange::Month);
    Expect(first.success && first.filesRead == 2, "first scan reads both files");
    const SessionScan second = sessions.Scan(UsageRange::Month);
    if (!(second.filesRead == 0 && second.filesSkipped == 2)) {
        std::cout << "read=" << second.filesRead << " skip=" << second.filesSkipped << "\n";
        std::cout << ReadBytes(index + L"\\sessions.json").substr(0, 240) << "\n";
    }
    Expect(second.filesRead == 0 && second.filesSkipped == 2, "second scan skips unchanged files");
    bool unknown = false;
    bool priced = false;
    for (const SessionRow& row : second.recent) {
        if (row.model == L"mystery-model") {
            unknown = !row.hasCost && row.status == L"unknown-model";
        }
        if (row.model == L"priced-test") {
            priced = row.hasCost && row.costUsd > 0;
        }
    }
    Expect(unknown, "unknown model has no invented cost");
    Expect(priced, "priced model has a cost");
    const std::wstring sessionIndex = logs + L"\\session_index.jsonl";
    WriteBytes(sessionIndex, "{\"path\":\"old\"}\n");
    const int before = static_cast<int>(std::distance(std::filesystem::directory_iterator(logs), std::filesystem::directory_iterator()));
    std::wstring error;
    Expect(sessions.RepairSessionIndex(sessionIndex, &error), "repair session index");
    Expect(std::filesystem::exists(sessionIndex + L".bak-" + std::to_wstring(0)) || !std::filesystem::directory_iterator(logs)->path().empty(), "backup search is possible");
    bool backup = false;
    for (const auto& entry : std::filesystem::directory_iterator(logs)) {
        if (entry.path().filename().wstring().find(L"session_index.jsonl.bak-") != std::wstring::npos) {
            backup = true;
        }
    }
    Expect(backup, "repair writes a backup");
    const int after = static_cast<int>(std::distance(std::filesystem::recursive_directory_iterator(logs), std::filesystem::recursive_directory_iterator()));
    Expect(after >= before, "repair does not delete session files");
    std::filesystem::remove_all(root);
}

void TestGrok() {
    const GrokSnapshot snapshot = ParseGrokBillingJson(
        "{\"config\":{\"creditUsagePercent\":8,\"currentPeriod\":{\"type\":\"USAGE_PERIOD_TYPE_WEEKLY\"},\"productUsage\":[{\"product\":\"GrokBuild\",\"usagePercent\":8},{\"product\":\"GrokChat\",\"usagePercent\":1},{\"product\":\"GrokImagine\",\"usagePercent\":null}],\"prepaidBalance\":{\"val\":12},\"onDemandUsed\":{\"val\":3}}}");
    Expect(snapshot.success, "parse grok billing");
    Expect(snapshot.products.size() == 3, "three grok products");
    Expect(snapshot.products[0].name == L"Build" && snapshot.products[1].name == L"Chat" && snapshot.products[2].name == L"Imagine", "product names");
    Expect(!snapshot.products[2].hasPercent, "null usage is not invented");
    const GrokSnapshot zero = ParseGrokBillingJson(
        "{\"config\":{\"currentPeriod\":{\"type\":\"USAGE_PERIOD_TYPE_WEEKLY\",\"end\":\"2026-10-01T00:00:00Z\"},\"isUnifiedBillingUser\":true,\"prepaidBalance\":{\"val\":0}}}");
    Expect(zero.success && zero.hasUsagePercent && zero.usagePercent == 0, "omitted grok usage is zero");
    Expect(GrokWeeklyRemainingPercent(snapshot) == 92, "grok weekly remaining is 100 minus used");
    Expect(GrokWeeklyRemainingPercent(zero) == 100, "zero grok usage remains full");
    Expect(GrokWeeklyRemainingPercent(GrokSnapshot{}) == -1, "missing grok usage has no remaining");
    Expect(GrokTokenNeedsRefresh("eyJhbGciOiJub25lIn0.eyJleHAiOjEwfQ.x", 100, 300), "expired grok access token needs refresh");
    Expect(!GrokTokenNeedsRefresh("eyJhbGciOiJub25lIn0.eyJleHAiOjIwMDAwMDAwMDB9.x", 100, 300), "fresh grok access token does not refresh yet");
    Expect(GrokTokenNeedsRefresh("", 100, 300), "missing grok access token needs refresh");
    Expect(GrokTokenNeedsRefresh("eyJhbGciOiJub25lIn0.eyJleHAiOjIwMDAwMDAwMDB9.x", 2000000000 - 3599, kGrokRefreshLeadSeconds), "grok token inside the one-hour lead is due");
    Expect(!GrokTokenNeedsRefresh("eyJhbGciOiJub25lIn0.eyJleHAiOjIwMDAwMDAwMDB9.x", 2000000000 - 7200, kGrokRefreshLeadSeconds), "grok token outside the one-hour lead waits");
}

void TestProxyAndUpdate() {
    Expect(BuildSocksGreeting(false) == std::string("\x05\x01\x00", 3), "socks greeting");
    Expect(BuildSocksGreeting(true).size() == 4, "socks auth greeting");
    const std::string ini = "[features]\nproxy_mode=1\nproxy_server=127.0.0.1:1\nproxy_user=u\n";
    Expect(!ProxyIniContainsSecret(ini), "settings text has no password");
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring file = std::wstring(temp) + L"codex-usage-hash.txt";
    WriteBytes(file, "abc");
    std::wstring hex;
    Expect(Sha256OfFile(file, &hex), "hash file");
    Expect(DigestMatches(L"sha256:" + hex, hex), "digest match");
    Expect(!DigestMatches(L"sha256:00", hex), "digest mismatch");
    std::filesystem::remove(file);
}

void TestPkce() {
    const PkceMaterial material = MakePkceMaterial();
    Expect(material.verifier.size() >= 32 && material.challenge.size() == 43, "pkce sizes");
    std::string access;
    std::string refresh;
    std::string idToken;
    Expect(ParseOAuthTokenJson("{\"access_token\":\"a\",\"refresh_token\":\"r\",\"id_token\":\"i\"}", &access, &refresh, &idToken), "parse token");
    Expect(access == "a" && refresh == "r" && idToken == "i", "token fields");
    Expect(!ParseOAuthTokenJson("{\"refresh_token\":\"r\"}", &access, &refresh, &idToken), "token without access is rejected");
}

}  // namespace

int main() {
    TestAccounts();
    TestResetStatus();
    TestEstimate();
    TestSessions();
    TestGrok();
    TestProxyAndUpdate();
    TestPkce();
    std::cout << "failures=" << gFailures << "\n";
    return gFailures == 0 ? 0 : 1;
}
