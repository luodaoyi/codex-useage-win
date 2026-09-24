#pragma once

#include <string>

struct StagedUpdate {
    bool success = false;
    std::wstring downloadedPath;
    std::wstring sha256;
    std::wstring scriptPath;
    std::wstring error;
};

bool Sha256OfFile(const std::wstring& path, std::wstring* hex);
bool DigestMatches(const std::wstring& expectedSha256, const std::wstring& actualHex);
StagedUpdate DownloadLatestRelease(const std::wstring& destinationExe);
bool LaunchReplaceScript(const StagedUpdate& update, const std::wstring& destinationExe);
