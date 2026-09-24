#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <wincred.h>

#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>

#include "ProxyConfig.h"
#include "TextUtil.h"

#include <mutex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")

namespace {

std::mutex gProxyMutex;
ProxyConfig gProxy;

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

bool RecvAll(SOCKET socket, char* data, int size) {
    int got = 0;
    while (got < size) {
        const int n = recv(socket, data + got, size - got, 0);
        if (n <= 0) {
            return false;
        }
        got += n;
    }
    return true;
}

}  // namespace

ProxyConfig GetProcessProxy() {
    std::lock_guard<std::mutex> lock(gProxyMutex);
    return gProxy;
}

void SetProcessProxy(const ProxyConfig& config) {
    std::lock_guard<std::mutex> lock(gProxyMutex);
    gProxy = config;
}

bool SaveProxyPassword(const ProxyConfig& config) {
    if (config.password.empty()) {
        return true;
    }
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(L"CodexUsageBar/proxy");
    credential.CredentialBlobSize = static_cast<DWORD>(config.password.size() * sizeof(wchar_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(config.password.c_str()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(config.user.c_str());
    return CredWriteW(&credential, 0) == TRUE;
}

bool LoadProxyPassword(ProxyConfig* config) {
    if (config == nullptr) {
        return false;
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(L"CodexUsageBar/proxy", CRED_TYPE_GENERIC, 0, &credential) || credential == nullptr) {
        return false;
    }
    if (credential->CredentialBlob != nullptr && credential->CredentialBlobSize >= sizeof(wchar_t)) {
        config->password.assign(
            reinterpret_cast<wchar_t*>(credential->CredentialBlob),
            credential->CredentialBlobSize / sizeof(wchar_t));
    }
    if (config->user.empty() && credential->UserName != nullptr) {
        config->user = credential->UserName;
    }
    CredFree(credential);
    return true;
}

std::string BuildSocksGreeting(bool withUserPass) {
    if (withUserPass) {
        return std::string("\x05\x02\x00\x02", 4);
    }
    return std::string("\x05\x01\x00", 3);
}

bool ProxyIniContainsSecret(const std::string& iniText) {
    return iniText.find("password") != std::string::npos || iniText.find("Password") != std::string::npos;
}

std::wstring DescribeProxy(const ProxyConfig& config) {
    if (config.mode == ProxyConfig::Mode::Http) {
        return L"HTTP " + config.server;
    }
    if (config.mode == ProxyConfig::Mode::Socks5) {
        return L"SOCKS5 " + config.server;
    }
    return L"system";
}

std::optional<std::string> Socks5Https(
    const std::wstring& targetHost,
    const std::wstring& pathAndQuery,
    const std::wstring& method,
    const std::vector<std::wstring>& headers,
    const std::string* body,
    const ProxyConfig& proxy,
    std::wstring* errorMessage) {
    auto fail = [&](const wchar_t* message) -> std::optional<std::string> {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return std::nullopt;
    };
    if (proxy.server.empty()) {
        return fail(L"SOCKS5 proxy server is empty");
    }
    const std::wstring server = proxy.server;
    const size_t colon = server.rfind(L':');
    if (colon == std::wstring::npos) {
        return fail(L"SOCKS5 proxy must be host:port");
    }
    const std::string proxyHost = WideToUtf8(server.substr(0, colon));
    const int proxyPort = _wtoi(server.substr(colon + 1).c_str());
    if (proxyPort <= 0) {
        return fail(L"SOCKS5 proxy port is invalid");
    }

    WSADATA wsa = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return fail(L"winsock startup failed");
    }
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(proxyHost.c_str(), std::to_string(proxyPort).c_str(), &hints, &resolved) != 0) {
        WSACleanup();
        return fail(L"cannot resolve SOCKS5 proxy");
    }
    SOCKET socket = INVALID_SOCKET;
    for (addrinfo* item = resolved; item != nullptr; item = item->ai_next) {
        socket = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (socket == INVALID_SOCKET) {
            continue;
        }
        if (connect(socket, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            break;
        }
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    freeaddrinfo(resolved);
    if (socket == INVALID_SOCKET) {
        WSACleanup();
        return fail(L"cannot connect to SOCKS5 proxy");
    }

    const bool auth = !proxy.user.empty();
    const std::string greeting = BuildSocksGreeting(auth);
    char methodReply[2] = {};
    if (!SendAll(socket, greeting.data(), static_cast<int>(greeting.size())) || !RecvAll(socket, methodReply, 2) || methodReply[0] != 5) {
        closesocket(socket);
        WSACleanup();
        return fail(L"SOCKS5 handshake failed");
    }
    if (auth) {
        if (methodReply[1] != 2) {
            closesocket(socket);
            WSACleanup();
            return fail(L"SOCKS5 proxy rejected username auth");
        }
        const std::string user = WideToUtf8(proxy.user);
        const std::string password = WideToUtf8(proxy.password);
        std::string authMsg;
        authMsg.push_back(1);
        authMsg.push_back(static_cast<char>(user.size()));
        authMsg += user;
        authMsg.push_back(static_cast<char>(password.size()));
        authMsg += password;
        char authReply[2] = {};
        if (!SendAll(socket, authMsg.data(), static_cast<int>(authMsg.size())) || !RecvAll(socket, authReply, 2) || authReply[1] != 0) {
            closesocket(socket);
            WSACleanup();
            return fail(L"SOCKS5 authentication failed");
        }
    } else if (methodReply[1] != 0) {
        closesocket(socket);
        WSACleanup();
        return fail(L"SOCKS5 proxy requires authentication");
    }

    const std::string host = WideToUtf8(targetHost);
    std::string request;
    request.push_back(5);
    request.push_back(1);
    request.push_back(0);
    request.push_back(3);
    request.push_back(static_cast<char>(host.size()));
    request += host;
    request.push_back(0x01);
    request.push_back(0xbb);
    char connectReply[4] = {};
    if (!SendAll(socket, request.data(), static_cast<int>(request.size())) || !RecvAll(socket, connectReply, 4) || connectReply[1] != 0) {
        closesocket(socket);
        WSACleanup();
        return fail(L"SOCKS5 connect failed");
    }
    int skip = 0;
    if (connectReply[3] == 1) {
        skip = 4 + 2;
    } else if (connectReply[3] == 3) {
        char length = 0;
        if (!RecvAll(socket, &length, 1)) {
            closesocket(socket);
            WSACleanup();
            return fail(L"SOCKS5 reply was truncated");
        }
        skip = static_cast<unsigned char>(length) + 2;
    } else if (connectReply[3] == 4) {
        skip = 16 + 2;
    }
    std::string junk(static_cast<size_t>(skip), '\0');
    if (skip > 0 && !RecvAll(socket, junk.data(), skip)) {
        closesocket(socket);
        WSACleanup();
        return fail(L"SOCKS5 reply was truncated");
    }

    SCHANNEL_CRED credentials = {};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION;
    CredHandle credentialHandle = {};
    if (AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr, &credentials, nullptr, nullptr, &credentialHandle, nullptr) != SEC_E_OK) {
        closesocket(socket);
        WSACleanup();
        return fail(L"TLS credential setup failed");
    }

    CtxtHandle context = {};
    SecBuffer outBuffers[1] = {};
    outBuffers[0].BufferType = SECBUFFER_TOKEN;
    SecBufferDesc outDesc = {SECBUFFER_VERSION, 1, outBuffers};
    DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_CONFIDENTIALITY | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT;
    DWORD outFlags = 0;
    SECURITY_STATUS status = InitializeSecurityContextW(
        &credentialHandle, nullptr, const_cast<wchar_t*>(targetHost.c_str()), flags, 0, 0, nullptr, 0,
        &context, &outDesc, &outFlags, nullptr);
    if (outBuffers[0].pvBuffer != nullptr && outBuffers[0].cbBuffer > 0) {
        SendAll(socket, static_cast<char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer));
        FreeContextBuffer(outBuffers[0].pvBuffer);
    }
    std::string incoming;
    while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
        char buffer[16 * 1024];
        const int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            status = SEC_E_INTERNAL_ERROR;
            break;
        }
        incoming.append(buffer, buffer + n);
        SecBuffer inBuffers[2] = {};
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].pvBuffer = incoming.data();
        inBuffers[0].cbBuffer = static_cast<unsigned long>(incoming.size());
        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc inDesc = {SECBUFFER_VERSION, 2, inBuffers};
        outBuffers[0] = {};
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        status = InitializeSecurityContextW(
            &credentialHandle, &context, const_cast<wchar_t*>(targetHost.c_str()), flags, 0, 0, &inDesc, 0,
            nullptr, &outDesc, &outFlags, nullptr);
        if (outBuffers[0].pvBuffer != nullptr && outBuffers[0].cbBuffer > 0) {
            SendAll(socket, static_cast<char*>(outBuffers[0].pvBuffer), static_cast<int>(outBuffers[0].cbBuffer));
            FreeContextBuffer(outBuffers[0].pvBuffer);
        }
        if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
            incoming.erase(0, incoming.size() - inBuffers[1].cbBuffer);
        } else {
            incoming.clear();
        }
    }
    if (status != SEC_E_OK) {
        DeleteSecurityContext(&context);
        FreeCredentialsHandle(&credentialHandle);
        closesocket(socket);
        WSACleanup();
        return fail(L"TLS handshake through SOCKS5 failed");
    }

    std::string http = WideToUtf8(method) + " " + WideToUtf8(pathAndQuery) + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n";
    for (const std::wstring& header : headers) {
        http += WideToUtf8(header) + "\r\n";
    }
    if (body != nullptr) {
        http += "Content-Length: " + std::to_string(body->size()) + "\r\n\r\n";
        http += *body;
    } else {
        http += "\r\n";
    }

    SecPkgContext_StreamSizes sizes = {};
    QueryContextAttributesW(&context, SECPKG_ATTR_STREAM_SIZES, &sizes);
    std::string encrypted(sizes.cbHeader + http.size() + sizes.cbTrailer, '\0');
    memcpy(encrypted.data() + sizes.cbHeader, http.data(), http.size());
    SecBuffer encryptBuffers[4] = {};
    encryptBuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    encryptBuffers[0].pvBuffer = encrypted.data();
    encryptBuffers[0].cbBuffer = sizes.cbHeader;
    encryptBuffers[1].BufferType = SECBUFFER_DATA;
    encryptBuffers[1].pvBuffer = encrypted.data() + sizes.cbHeader;
    encryptBuffers[1].cbBuffer = static_cast<unsigned long>(http.size());
    encryptBuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    encryptBuffers[2].pvBuffer = encrypted.data() + sizes.cbHeader + http.size();
    encryptBuffers[2].cbBuffer = sizes.cbTrailer;
    encryptBuffers[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc encryptDesc = {SECBUFFER_VERSION, 4, encryptBuffers};
    if (EncryptMessage(&context, 0, &encryptDesc, 0) != SEC_E_OK
        || !SendAll(socket, encrypted.data(), static_cast<int>(encryptBuffers[0].cbBuffer + encryptBuffers[1].cbBuffer + encryptBuffers[2].cbBuffer))) {
        DeleteSecurityContext(&context);
        FreeCredentialsHandle(&credentialHandle);
        closesocket(socket);
        WSACleanup();
        return fail(L"failed to send through SOCKS5");
    }

    std::string cipher;
    std::string plain;
    for (;;) {
        char buffer[16 * 1024];
        const int n = recv(socket, buffer, sizeof(buffer), 0);
        if (n < 0) {
            break;
        }
        if (n > 0) {
            cipher.append(buffer, buffer + n);
        }
        if (cipher.empty()) {
            break;
        }
        SecBuffer decryptBuffers[4] = {};
        decryptBuffers[0].BufferType = SECBUFFER_DATA;
        decryptBuffers[0].pvBuffer = cipher.data();
        decryptBuffers[0].cbBuffer = static_cast<unsigned long>(cipher.size());
        decryptBuffers[1].BufferType = SECBUFFER_EMPTY;
        decryptBuffers[2].BufferType = SECBUFFER_EMPTY;
        decryptBuffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc decryptDesc = {SECBUFFER_VERSION, 4, decryptBuffers};
        const SECURITY_STATUS decrypted = DecryptMessage(&context, &decryptDesc, 0, nullptr);
        if (decrypted == SEC_E_INCOMPLETE_MESSAGE) {
            if (n == 0) {
                break;
            }
            continue;
        }
        if (decrypted != SEC_E_OK) {
            break;
        }
        for (SecBuffer& item : decryptBuffers) {
            if (item.BufferType == SECBUFFER_DATA && item.pvBuffer != nullptr && item.cbBuffer > 0) {
                plain.append(static_cast<char*>(item.pvBuffer), item.cbBuffer);
            }
        }
        size_t extra = 0;
        for (const SecBuffer& item : decryptBuffers) {
            if (item.BufferType == SECBUFFER_EXTRA) {
                extra = item.cbBuffer;
            }
        }
        if (extra > 0 && extra <= cipher.size()) {
            cipher = cipher.substr(cipher.size() - extra);
        } else {
            cipher.clear();
        }
        if (n == 0 && cipher.empty()) {
            break;
        }
    }
    DeleteSecurityContext(&context);
    FreeCredentialsHandle(&credentialHandle);
    closesocket(socket);
    WSACleanup();
    const size_t split = plain.find("\r\n\r\n");
    if (split == std::string::npos) {
        return fail(L"SOCKS5 response had no HTTP body");
    }
    const std::string header = plain.substr(0, split);
    if (header.find(" 200 ") == std::string::npos && header.find(" 201 ") == std::string::npos && header.find(" 204 ") == std::string::npos) {
        return fail(L"SOCKS5 request was not successful");
    }
    return plain.substr(split + 4);
}
