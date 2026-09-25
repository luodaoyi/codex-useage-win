#include "AccountLibrary.h"

#include "JsonLite.h"
#include "TextUtil.h"

#include <Windows.h>
#include <wincrypt.h>

#include <optional>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::wstring JoinPath(const std::wstring& base, const std::wstring& child) {
    std::wstring result = base;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    result += child;
    return result;
}

bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::string ReadFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteFileBytes(const std::wstring& path, const std::string& content, std::wstring* error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error != nullptr) {
            *error = L"cannot write " + path;
        }
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        if (error != nullptr) {
            *error = L"failed writing " + path;
        }
        return false;
    }
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(static_cast<char>(ch)); break;
        }
    }
    return out;
}

std::optional<std::string> JsonString(const jsonlite::Value* node) {
    if (node == nullptr) {
        return std::nullopt;
    }
    auto text = node->AsString();
    if (!text.has_value() || text->empty()) {
        return std::nullopt;
    }
    return std::string(*text);
}

std::string Field(const jsonlite::Value* tokens, const jsonlite::Value& root, const char* key) {
    if (auto nested = JsonString(tokens != nullptr ? tokens->Find(key) : nullptr); nested.has_value()) {
        return *nested;
    }
    if (auto top = JsonString(root.Find(key)); top.has_value()) {
        return *top;
    }
    return {};
}

struct ParsedAuth {
    bool ok = false;
    std::string accountId;
    std::string email;
    std::wstring error;
};

std::string EmailFromJwt(const std::string& jwt) {
    const size_t first = jwt.find('.');
    const size_t second = first == std::string::npos ? std::string::npos : jwt.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return {};
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
        return {};
    }
    std::string decoded(size, '\0');
    if (!CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(decoded.data()), &size, nullptr, nullptr)) {
        return {};
    }
    decoded.resize(size);
    jsonlite::Parser parser(decoded);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return {};
    }
    if (auto email = JsonString(root->Find("email")); email.has_value()) {
        return *email;
    }
    if (const jsonlite::Value* profile = root->Find("https://api.openai.com/profile"); profile != nullptr) {
        if (auto email = JsonString(profile->Find("email")); email.has_value()) {
            return *email;
        }
    }
    return {};
}

ParsedAuth ParseAuth(const std::string& jsonText, const std::wstring& provider) {
    ParsedAuth parsed;
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    if (!root.has_value() || !root->IsObject()) {
        parsed.error = L"auth JSON parse failed";
        return parsed;
    }
    const jsonlite::Value* tokens = root->Find("tokens");
    if (tokens != nullptr && !tokens->IsObject()) {
        tokens = nullptr;
    }
    if (EqualsIgnoreCase(provider, L"codex") && tokens == nullptr) {
        if (auto type = JsonString(root->Find("type")); type.has_value() && *type != "codex") {
            parsed.error = L"auth JSON type is not codex";
            return parsed;
        }
    }
    const std::string access = Field(tokens, *root, "access_token");
    const std::string token = Field(tokens, *root, "token");
    if (access.empty() && token.empty()) {
        parsed.error = L"auth JSON missing access_token";
        return parsed;
    }
    parsed.accountId = Field(tokens, *root, "account_id");
    if (parsed.accountId.empty()) {
        parsed.accountId = Field(tokens, *root, "sub");
    }
    parsed.email = EmailFromJwt(Field(tokens, *root, "id_token"));
    if (parsed.email.empty()) {
        if (auto email = JsonString(root->Find("email")); email.has_value()) {
            parsed.email = *email;
        }
    }
    parsed.ok = true;
    return parsed;
}

std::wstring DetectProvider(const std::string& jsonText, const std::wstring& filename) {
    jsonlite::Parser parser(jsonText);
    const auto root = parser.Parse();
    std::string type;
    if (root.has_value()) {
        if (auto value = JsonString(root->Find("type")); value.has_value()) {
            type = *value;
            for (char& ch : type) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
        }
    }
    if (type == "xai" || type == "grok") {
        return L"grok";
    }
    if (type == "codex") {
        return L"codex";
    }
    auto jwtIss = [](const std::string& jwt) -> std::string {
        const size_t first = jwt.find('.');
        const size_t second = first == std::string::npos ? std::string::npos : jwt.find('.', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            return {};
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
            return {};
        }
        std::string decoded(size, '\0');
        if (!CryptStringToBinaryA(payload.c_str(), static_cast<DWORD>(payload.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(decoded.data()), &size, nullptr, nullptr)) {
            return {};
        }
        decoded.resize(size);
        const size_t iss = decoded.find("\"iss\"");
        return iss == std::string::npos ? std::string() : decoded.substr(iss);
    };
    std::string iss;
    if (root.has_value()) {
        const jsonlite::Value* tokens = root->Find("tokens");
        if (tokens != nullptr && !tokens->IsObject()) {
            tokens = nullptr;
        }
        iss = jwtIss(Field(tokens, *root, "access_token"));
        if (iss.find("auth.x.ai") == std::string::npos) {
            const std::string idIss = jwtIss(Field(tokens, *root, "id_token"));
            if (idIss.find("auth.x.ai") != std::string::npos) {
                iss = idIss;
            }
        }
    }
    if (iss.find("auth.x.ai") != std::string::npos || jsonText.find("auth.x.ai") != std::string::npos
        || jsonText.find("cli-chat-proxy.grok.com") != std::string::npos) {
        return L"grok";
    }
    if (iss.find("auth.openai.com") != std::string::npos) {
        return L"codex";
    }
    if (filename.size() >= 4 && _wcsnicmp(filename.c_str(), L"xai-", 4) == 0) {
        return L"grok";
    }
    return {};
}

std::wstring SanitizeStem(const std::wstring& label) {
    std::wstring stem;
    for (wchar_t ch : label) {
        const bool keep = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')
            || ch == L'.' || ch == L'-' || ch == L'_';
        if (keep) {
            stem.push_back(ch);
        } else if (ch == L'@' || ch == L' ') {
            stem.push_back(L'_');
        }
    }
    if (stem.empty()) {
        stem = L"account";
    }
    if (stem.size() > 80) {
        stem.resize(80);
    }
    return stem;
}

struct IndexRow {
    std::wstring id;
    std::wstring alias;
    std::wstring provider;
    int order = 0;
};

std::vector<IndexRow> ReadIndex(const std::wstring& path) {
    std::vector<IndexRow> rows;
    const std::string text = ReadFile(path);
    if (text.empty()) {
        return rows;
    }
    jsonlite::Parser parser(text);
    const auto root = parser.Parse();
    if (!root.has_value()) {
        return rows;
    }
    const jsonlite::Value* accounts = root->Find("accounts");
    const auto* array = accounts != nullptr ? accounts->AsArray() : nullptr;
    if (array == nullptr) {
        return rows;
    }
    for (const jsonlite::Value& item : *array) {
        IndexRow row;
        if (auto id = JsonString(item.Find("id")); id.has_value()) {
            row.id = Utf8ToWide(*id);
        }
        if (auto alias = JsonString(item.Find("alias")); alias.has_value()) {
            row.alias = Utf8ToWide(*alias);
        }
        if (auto provider = JsonString(item.Find("provider")); provider.has_value()) {
            row.provider = Utf8ToWide(*provider);
        }
        if (const jsonlite::Value* order = item.Find("order"); order != nullptr) {
            if (auto asInt = order->AsInt(); asInt.has_value()) {
                row.order = *asInt;
            }
        }
        if (!row.id.empty()) {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

bool WriteIndex(const std::wstring& path, const std::vector<IndexRow>& rows, std::wstring* error) {
    std::string json = "{\"accounts\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i != 0) {
            json += ',';
        }
        json += "{\"id\":\"" + JsonEscape(WideToUtf8(rows[i].id))
            + "\",\"alias\":\"" + JsonEscape(WideToUtf8(rows[i].alias))
            + "\",\"provider\":\"" + JsonEscape(WideToUtf8(rows[i].provider))
            + "\",\"order\":" + std::to_string(rows[i].order) + "}";
    }
    json += "]}";
    return WriteFileBytes(path, json, error);
}

}  // namespace

AccountLibrary::AccountLibrary() {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
        root_ = std::filesystem::path(modulePath).parent_path().wstring();
    }
    if (root_.empty()) {
        root_ = L".";
    }
}

AccountLibrary::AccountLibrary(std::wstring rootDirectory) : root_(std::move(rootDirectory)) {}

std::wstring AccountLibrary::RootDirectory() const {
    return root_;
}

std::wstring AccountLibrary::AccountsDirectory() const {
    return JoinPath(root_, L"accounts");
}

std::wstring AccountLibrary::IndexPath() const {
    return JoinPath(AccountsDirectory(), L"index.json");
}

std::vector<AccountEntry> AccountLibrary::List(const std::wstring& provider) const {
    std::vector<AccountEntry> entries;
    const std::wstring dir = AccountsDirectory();
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error) || error) {
        return entries;
    }
    const std::vector<IndexRow> index = ReadIndex(IndexPath());
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        if (_wcsicmp(entry.path().extension().c_str(), L".json") != 0) {
            continue;
        }
        if (EqualsIgnoreCase(entry.path().filename().wstring(), L"index.json")) {
            continue;
        }
        files.push_back(entry.path());
    }
    for (const std::filesystem::path& file : files) {
        AccountEntry item;
        item.path = file.wstring();
        item.id = L"accounts\\" + file.filename().wstring();
        item.provider = L"codex";
        item.order = 1000;
        item.label = file.filename().wstring();
        for (const IndexRow& row : index) {
            if (EqualsIgnoreCase(row.id, item.id)) {
                item.alias = row.alias;
                if (!row.provider.empty()) {
                    item.provider = row.provider;
                }
                item.order = row.order;
                break;
            }
        }
        const std::string fileText = ReadFile(item.path);
        if (const std::wstring detected = DetectProvider(fileText, file.filename().wstring()); !detected.empty()) {
            item.provider = detected;
        }
        const ParsedAuth parsed = ParseAuth(fileText, item.provider);
        if (!parsed.email.empty()) {
            item.email = Utf8ToWide(parsed.email);
        }
        if (!item.alias.empty()) {
            item.label = item.alias;
        } else if (!item.email.empty()) {
            item.label = item.email;
        }
        if (!provider.empty() && !EqualsIgnoreCase(item.provider, provider)) {
            continue;
        }
        entries.push_back(std::move(item));
    }
    std::sort(entries.begin(), entries.end(), [](const AccountEntry& left, const AccountEntry& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return _wcsicmp(left.label.c_str(), right.label.c_str()) < 0;
    });
    return entries;
}

AccountOpResult AccountLibrary::ImportText(const std::string& jsonText, const std::wstring& provider) {
    AccountOpResult result;
    std::wstring resolved = DetectProvider(jsonText, L"");
    if (resolved.empty()) {
        resolved = provider.empty() ? L"codex" : provider;
    }
    const ParsedAuth parsed = ParseAuth(jsonText, resolved);
    const std::wstring& storedProvider = resolved;
    if (!parsed.ok) {
        result.error = parsed.error;
        return result;
    }
    std::error_code mkdirError;
    std::filesystem::create_directories(AccountsDirectory(), mkdirError);
    if (mkdirError) {
        result.error = L"cannot create accounts directory";
        return result;
    }
    std::wstring destId;
    std::wstring destPath;
    if (!parsed.accountId.empty()) {
        for (const AccountEntry& existing : List(L"")) {
            const ParsedAuth other = ParseAuth(ReadFile(existing.path), existing.provider);
            if (other.accountId == parsed.accountId && EqualsIgnoreCase(existing.provider, storedProvider)) {
                destId = existing.id;
                destPath = existing.path;
                break;
            }
        }
    }
    if (destPath.empty()) {
        const std::wstring stem = SanitizeStem(parsed.email.empty() ? L"account" : Utf8ToWide(parsed.email));
        std::wstring name = stem + L".json";
        destPath = JoinPath(AccountsDirectory(), name);
        for (int suffix = 2; suffix < 1000; ++suffix) {
            std::error_code existsError;
            if (!std::filesystem::exists(destPath, existsError) || existsError) {
                break;
            }
            name = stem + L"-" + std::to_wstring(suffix) + L".json";
            destPath = JoinPath(AccountsDirectory(), name);
        }
        destId = L"accounts\\" + std::filesystem::path(destPath).filename().wstring();
    }
    if (!WriteFileBytes(destPath, jsonText, &result.error)) {
        return result;
    }
    std::vector<IndexRow> rows = ReadIndex(IndexPath());
    bool found = false;
    for (IndexRow& row : rows) {
        if (EqualsIgnoreCase(row.id, destId)) {
            row.provider = storedProvider;
            found = true;
            break;
        }
    }
    if (!found) {
        IndexRow row;
        row.id = destId;
        row.provider = storedProvider;
        row.order = static_cast<int>(rows.size());
        rows.push_back(std::move(row));
    }
    if (!WriteIndex(IndexPath(), rows, &result.error)) {
        return result;
    }
    result.success = true;
    result.id = destId;
    return result;
}

AccountOpResult AccountLibrary::ImportFile(const std::wstring& sourcePath, const std::wstring& provider) {
    return ImportText(ReadFile(sourcePath), provider);
}

AccountOpResult AccountLibrary::SetAlias(const std::wstring& id, const std::wstring& alias) {
    AccountOpResult result;
    std::vector<IndexRow> rows = ReadIndex(IndexPath());
    bool found = false;
    for (IndexRow& row : rows) {
        if (EqualsIgnoreCase(row.id, id)) {
            row.alias = alias;
            found = true;
            break;
        }
    }
    if (!found) {
        result.error = L"account is not in the index";
        return result;
    }
    if (!WriteIndex(IndexPath(), rows, &result.error)) {
        return result;
    }
    result.success = true;
    result.id = id;
    return result;
}

AccountOpResult AccountLibrary::Move(const std::wstring& id, int delta) {
    AccountOpResult result;
    if (delta == 0) {
        result.success = true;
        result.id = id;
        return result;
    }
    std::vector<AccountEntry> entries = List(L"");
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const AccountEntry& entry) {
        return EqualsIgnoreCase(entry.id, id);
    });
    if (it == entries.end()) {
        result.error = L"account was not found";
        return result;
    }
    const int index = static_cast<int>(std::distance(entries.begin(), it));
    const int target = index + (delta < 0 ? -1 : 1);
    if (target < 0 || target >= static_cast<int>(entries.size())) {
        result.success = true;
        result.id = id;
        return result;
    }
    if (!EqualsIgnoreCase(entries[static_cast<size_t>(target)].provider, it->provider)) {
        result.success = true;
        result.id = id;
        return result;
    }
    std::swap(entries[static_cast<size_t>(index)], entries[static_cast<size_t>(target)]);
    std::vector<IndexRow> rows;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        IndexRow row;
        row.id = entries[static_cast<size_t>(i)].id;
        row.alias = entries[static_cast<size_t>(i)].alias;
        row.provider = entries[static_cast<size_t>(i)].provider;
        row.order = i;
        rows.push_back(std::move(row));
    }
    if (!WriteIndex(IndexPath(), rows, &result.error)) {
        return result;
    }
    result.success = true;
    result.id = id;
    return result;
}

AccountOpResult AccountLibrary::Delete(const std::wstring& id) {
    AccountOpResult result;
    if (id.find(L"..") != std::wstring::npos) {
        result.error = L"refusing to delete outside accounts";
        return result;
    }
    const std::vector<AccountEntry> entries = List(L"");
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const AccountEntry& entry) {
        return EqualsIgnoreCase(entry.id, id);
    });
    if (it == entries.end()) {
        result.error = L"account was not found";
        return result;
    }
    std::error_code removeError;
    std::filesystem::remove(it->path, removeError);
    if (removeError) {
        result.error = L"cannot delete account file";
        return result;
    }
    std::vector<IndexRow> rows = ReadIndex(IndexPath());
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const IndexRow& row) {
        return EqualsIgnoreCase(row.id, id);
    }), rows.end());
    if (!WriteIndex(IndexPath(), rows, &result.error)) {
        return result;
    }
    result.success = true;
    result.id = id;
    return result;
}

int AccountLibrary::ImportSiblingAuthFiles() {
    int imported = 0;
    std::error_code error;
    if (!std::filesystem::is_directory(root_, error) || error) {
        return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        if (_wcsicmp(entry.path().extension().c_str(), L".json") != 0) {
            continue;
        }
        if (EqualsIgnoreCase(entry.path().filename().wstring(), L"index.json")) {
            continue;
        }
        const std::string text = ReadFile(entry.path().wstring());
        if (text.find("access_token") == std::string::npos) {
            continue;
        }
        const AccountOpResult result = ImportText(text, L"codex");
        if (result.success) {
            ++imported;
        }
    }
    return imported;
}
