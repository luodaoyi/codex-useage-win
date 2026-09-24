#include "AppUpdate.h"

#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <fstream>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

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

bool WriteText(const std::wstring& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(output);
}

std::wstring Hex(const unsigned char* data, size_t size) {
    static const wchar_t digits[] = L"0123456789abcdef";
    std::wstring out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    return out;
}

}  // namespace

bool Sha256OfFile(const std::wstring& path, std::wstring* hex) {
    const std::string bytes = ReadFile(path);
    if (bytes.empty() && path.empty()) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hashLength = 0;
    DWORD written = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }
    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &written, 0);
    std::string digest(hashLength, '\0');
    const bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0
        && BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) == 0
        && BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), hashLength, 0) == 0;
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok || hex == nullptr) {
        return ok;
    }
    *hex = Hex(reinterpret_cast<const unsigned char*>(digest.data()), digest.size());
    return true;
}

bool DigestMatches(const std::wstring& expectedSha256, const std::wstring& actualHex) {
    std::wstring expected = expectedSha256;
    const std::wstring prefix = L"sha256:";
    if (expected.size() >= prefix.size() && _wcsnicmp(expected.c_str(), prefix.c_str(), static_cast<int>(prefix.size())) == 0) {
        expected.erase(0, prefix.size());
    }
    return !expected.empty() && _wcsicmp(expected.c_str(), actualHex.c_str()) == 0;
}

StagedUpdate DownloadLatestRelease(const std::wstring& destinationExe) {
    StagedUpdate update;
    std::wstring error;
    const auto release = NetHttps(
        L"api.github.com",
        L"/repos/luodaoyi/codex-useage-win/releases/latest",
        L"GET",
        {L"Accept: application/vnd.github+json", L"User-Agent: CodexUsageBar", L"X-GitHub-Api-Version: 2022-11-28"},
        nullptr,
        &error);
    if (!release.has_value()) {
        update.error = error.empty() ? L"update lookup failed" : error;
        return update;
    }
    jsonlite::Parser parser(*release);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        update.error = L"update JSON parse failed";
        return update;
    }
    const jsonlite::Value* assets = root->Find("assets");
    const auto* array = assets != nullptr ? assets->AsArray() : nullptr;
    if (array == nullptr) {
        update.error = L"update release has no assets";
        return update;
    }
    const wchar_t* wanted = sizeof(void*) == 8 ? L"CodexUsageBar-x64.exe" : L"CodexUsageBar-x64.exe";
    SYSTEM_INFO info = {};
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        wanted = L"CodexUsageBar-ARM64.exe";
    }
    std::string downloadUrl;
    std::wstring digest;
    for (const jsonlite::Value& asset : *array) {
        auto name = asset.Find("name") != nullptr ? asset.Find("name")->AsString() : std::nullopt;
        if (!name.has_value() || Utf8ToWide(std::string(*name)) != wanted) {
            continue;
        }
        if (auto url = asset.Find("browser_download_url") != nullptr ? asset.Find("browser_download_url")->AsString() : std::nullopt; url.has_value()) {
            downloadUrl = std::string(*url);
        }
        if (auto value = asset.Find("digest") != nullptr ? asset.Find("digest")->AsString() : std::nullopt; value.has_value()) {
            digest = Utf8ToWide(std::string(*value));
        }
        break;
    }
    if (downloadUrl.empty()) {
        update.error = L"update asset was not found";
        return update;
    }
    const std::wstring wideUrl = Utf8ToWide(downloadUrl);
    const size_t scheme = wideUrl.find(L"://");
    const size_t hostStart = scheme == std::wstring::npos ? 0 : scheme + 3;
    const size_t pathStart = wideUrl.find(L'/', hostStart);
    if (pathStart == std::wstring::npos) {
        update.error = L"update URL is invalid";
        return update;
    }
    const auto file = NetHttps(wideUrl.substr(hostStart, pathStart - hostStart), wideUrl.substr(pathStart), L"GET",
        {L"User-Agent: CodexUsageBar", L"Accept: application/octet-stream"}, nullptr, &error);
    if (!file.has_value()) {
        update.error = error.empty() ? L"update download failed" : error;
        return update;
    }
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    update.downloadedPath = std::wstring(tempDir) + wanted;
    if (!WriteText(update.downloadedPath, *file)) {
        update.error = L"cannot store the downloaded update";
        return update;
    }
    if (!Sha256OfFile(update.downloadedPath, &update.sha256)) {
        update.error = L"cannot hash the downloaded update";
        return update;
    }
    if (!digest.empty() && !DigestMatches(digest, update.sha256)) {
        update.error = L"update SHA256 did not match";
        return update;
    }
    wchar_t scriptDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, scriptDir);
    update.scriptPath = std::wstring(scriptDir) + L"codex-usage-replace.cmd";
    const std::string script = "@echo off\r\ntimeout /t 2 /nobreak >nul\r\nmove /y \""
        + WideToUtf8(update.downloadedPath) + "\" \"" + WideToUtf8(destinationExe) + "\"\r\nstart \"\" \""
        + WideToUtf8(destinationExe) + "\"\r\n";
    if (!WriteText(update.scriptPath, script)) {
        update.error = L"cannot write the replace script";
        return update;
    }
    (void)destinationExe;
    update.success = true;
    return update;
}

bool LaunchReplaceScript(const StagedUpdate& update, const std::wstring& destinationExe) {
    (void)destinationExe;
    if (!update.success || update.scriptPath.empty()) {
        return false;
    }
    const HINSTANCE launched = ShellExecuteW(nullptr, L"open", update.scriptPath.c_str(), nullptr, nullptr, SW_HIDE);
    return reinterpret_cast<INT_PTR>(launched) > 32;
}
