#include "BrowserSignIn.h"

#include "AccountLibrary.h"
#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

#include <Windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

std::string RandomToken(size_t size) {
    std::string bytes(size, '\0');
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return bytes;
}

std::string Base64Url(const unsigned char* data, size_t size) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16)
            | ((i + 1 < size ? data[i + 1] : 0) << 8)
            | (i + 2 < size ? data[i + 2] : 0);
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        if (i + 1 < size) {
            out.push_back(table[(n >> 6) & 63]);
        }
        if (i + 2 < size) {
            out.push_back(table[n & 63]);
        }
    }
    return out;
}

std::string Sha256Base64Url(const std::string& value) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hashLength = 0;
    DWORD written = 0;
    std::string digest;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &written, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    digest.resize(hashLength);
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0
        || BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) != 0
        || BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), hashLength, 0) != 0) {
        digest.clear();
    }
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return Base64Url(reinterpret_cast<const unsigned char*>(digest.data()), digest.size());
}

std::string UrlEncode(const std::string& value) {
    std::string out;
    char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 15]);
        }
    }
    return out;
}

bool SendAll(SOCKET socket, const char* data, int size) {
    int sent = 0;
    while (sent < size) {
        const int n = send(socket, data + sent, size - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

std::string ReceiveSome(SOCKET socket) {
    std::string data;
    char buffer[2048];
    const int n = recv(socket, buffer, sizeof(buffer), 0);
    if (n > 0) {
        data.assign(buffer, buffer + n);
    }
    return data;
}

std::string QueryValue(const std::string& request, const std::string& key) {
    const std::string needle = key + "=";
    const size_t start = request.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    size_t end = request.find_first_of(" &\r\n", start + needle.size());
    if (end == std::string::npos) {
        end = request.size();
    }
    return request.substr(start + needle.size(), end - start - needle.size());
}

}  // namespace

PkceMaterial MakePkceMaterial() {
    PkceMaterial material;
    material.verifier = Base64Url(reinterpret_cast<const unsigned char*>(RandomToken(32).data()), 32);
    material.challenge = Sha256Base64Url(material.verifier);
    material.state = Base64Url(reinterpret_cast<const unsigned char*>(RandomToken(16).data()), 16);
    return material;
}

bool ParseOAuthTokenJson(const std::string& jsonText, std::string* access, std::string* refresh, std::string* idToken) {
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return false;
    }
    auto read = [&](const char* key) {
        if (root->Find(key) == nullptr) {
            return std::string();
        }
        if (auto text = root->Find(key)->AsString(); text.has_value()) {
            return std::string(*text);
        }
        return std::string();
    };
    const std::string accessToken = read("access_token");
    if (accessToken.empty()) {
        return false;
    }
    if (access != nullptr) {
        *access = accessToken;
    }
    if (refresh != nullptr) {
        *refresh = read("refresh_token");
    }
    if (idToken != nullptr) {
        *idToken = read("id_token");
    }
    return true;
}

BrowserSignInResult RunBrowserSignIn(SignInProvider provider, const std::wstring& accountsRoot) {
    BrowserSignInResult result;
    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        result.error = L"winsock startup failed";
        return result;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        result.error = L"cannot open the login callback socket";
        return result;
    }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(provider == SignInProvider::Codex ? 1455 : 56121);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(listener);
        WSACleanup();
        result.error = L"login callback port is already in use";
        return result;
    }
    listen(listener, 1);
    const PkceMaterial pkce = MakePkceMaterial();
    const std::string redirect = provider == SignInProvider::Codex
        ? "http://localhost:1455/auth/callback"
        : "http://127.0.0.1:56121/callback";
    std::string url = provider == SignInProvider::Codex
        ? "https://auth.openai.com/oauth/authorize?response_type=code&client_id=app_EMoamEEZ73f0CkXaXp7hrann"
        : "https://auth.x.ai/oauth2/authorize?response_type=code&client_id=b1a00492-073a-47ea-816f-4c329264a828";
    url += "&redirect_uri=" + UrlEncode(redirect);
    url += "&code_challenge=" + UrlEncode(pkce.challenge);
    url += "&code_challenge_method=S256&state=" + UrlEncode(pkce.state);
    url += provider == SignInProvider::Codex
        ? "&scope=" + UrlEncode("openid profile email offline_access")
        : "&scope=" + UrlEncode("openid profile email offline_access grok-cli:access api:access");
    ShellExecuteW(nullptr, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listener, &readSet);
    timeval timeout = {};
    timeout.tv_sec = 180;
    if (select(0, &readSet, nullptr, nullptr, &timeout) <= 0) {
        closesocket(listener);
        WSACleanup();
        result.error = L"browser login timed out";
        return result;
    }
    SOCKET client = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (client == INVALID_SOCKET) {
        WSACleanup();
        result.error = L"login callback failed";
        return result;
    }
    const std::string request = ReceiveSome(client);
    const char* page = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nYou can close this tab.";
    SendAll(client, page, static_cast<int>(strlen(page)));
    closesocket(client);
    WSACleanup();

    const std::string code = QueryValue(request, "code");
    const std::string state = QueryValue(request, "state");
    if (code.empty() || state != pkce.state) {
        result.error = L"login callback did not include a matching code";
        return result;
    }
    const std::string body = provider == SignInProvider::Codex
        ? "{\"grant_type\":\"authorization_code\",\"client_id\":\"app_EMoamEEZ73f0CkXaXp7hrann\",\"code\":\"" + code
            + "\",\"code_verifier\":\"" + pkce.verifier + "\",\"redirect_uri\":\"" + redirect + "\"}"
        : "grant_type=authorization_code&client_id=b1a00492-073a-47ea-816f-4c329264a828&code=" + UrlEncode(code)
            + "&code_verifier=" + UrlEncode(pkce.verifier) + "&redirect_uri=" + UrlEncode(redirect);
    std::wstring error;
    const std::vector<std::wstring> headers = provider == SignInProvider::Codex
        ? std::vector<std::wstring>{L"Content-Type: application/json", L"Accept: application/json"}
        : std::vector<std::wstring>{L"Content-Type: application/x-www-form-urlencoded", L"Accept: application/json"};
    const auto tokenJson = NetHttps(
        provider == SignInProvider::Codex ? L"auth.openai.com" : L"auth.x.ai",
        provider == SignInProvider::Codex ? L"/oauth/token" : L"/oauth2/token",
        L"POST",
        headers,
        &body,
        &error);
    if (!tokenJson.has_value()) {
        result.error = error.empty() ? L"token exchange failed" : error;
        return result;
    }
    std::string access;
    std::string refresh;
    std::string idToken;
    if (!ParseOAuthTokenJson(*tokenJson, &access, &refresh, &idToken)) {
        result.error = L"token response missing access_token";
        return result;
    }
    std::string stored = provider == SignInProvider::Codex
        ? "{\"type\":\"codex\",\"access_token\":\"" + access + "\",\"refresh_token\":\"" + refresh + "\",\"id_token\":\"" + idToken + "\"}"
        : "{\"access_token\":\"" + access + "\",\"refresh_token\":\"" + refresh + "\",\"id_token\":\"" + idToken + "\"}";
    AccountLibrary library(accountsRoot);
    const AccountOpResult imported = library.ImportText(stored, provider == SignInProvider::Codex ? L"codex" : L"grok");
    if (!imported.success) {
        result.error = imported.error;
        return result;
    }
    result.success = true;
    result.accountId = imported.id;
    return result;
}
