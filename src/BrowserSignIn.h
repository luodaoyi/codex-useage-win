#pragma once

#include <string>

struct BrowserSignInResult {
    bool success = false;
    std::wstring accountId;
    std::wstring error;
};

struct PkceMaterial {
    std::string verifier;
    std::string challenge;
    std::string state;
};

enum class SignInProvider {
    Codex = 0,
    Grok = 1,
};

PkceMaterial MakePkceMaterial();
bool ParseOAuthTokenJson(const std::string& jsonText, std::string* access, std::string* refresh, std::string* idToken);
BrowserSignInResult RunBrowserSignIn(SignInProvider provider, const std::wstring& accountsRoot);
