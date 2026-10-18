#pragma once

#include <Windows.h>

#include <string>
#include <vector>

inline std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size);
    return output;
}

inline std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), size, nullptr, nullptr);
    return output;
}

inline wchar_t AsciiLower(wchar_t ch) {
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return ch;
}

inline bool ContainsAsciiInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool matched = true;
        for (size_t index = 0; index < needle.size(); ++index) {
            if (AsciiLower(haystack[start + index]) != AsciiLower(needle[index])) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

// model id plus the display name, without the effort suffix. Space-separated
// query words match if any one word is contained (flash luna => flash or luna).
inline bool ModelNameMatchesQuery(
    const std::wstring& model,
    const std::wstring& label,
    const std::wstring& effort,
    const std::wstring& query) {
    std::vector<std::wstring> tokens;
    std::wstring token;
    auto flush = [&]() {
        if (!token.empty()) {
            tokens.push_back(token);
            token.clear();
        }
    };
    for (wchar_t ch : query) {
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            flush();
        } else {
            token.push_back(ch);
        }
    }
    flush();
    if (tokens.empty()) {
        return true;
    }

    std::wstring name = label;
    if (!effort.empty()) {
        const std::wstring suffix = L" " + effort;
        if (name.size() > suffix.size()
            && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.resize(name.size() - suffix.size());
            while (!name.empty() && name.back() == L' ') {
                name.pop_back();
            }
        }
    }
    std::wstring haystack = model;
    if (!name.empty()) {
        if (!haystack.empty()) {
            haystack += L' ';
        }
        haystack += name;
    }
    for (const std::wstring& part : tokens) {
        if (ContainsAsciiInsensitive(haystack, part)) {
            return true;
        }
    }
    return false;
}

// Distinct RGB for each model index. The old palette wrapped every 12 models.
inline COLORREF SeriesModelColor(int index, bool lightTheme) {
    const int n = index < 0 ? 0 : index;
    if (lightTheme) {
        return RGB(20 + ((n * 67) % 200), 25 + ((n * 41) % 170), 30 + ((n * 97) % 190));
    }
    return RGB(70 + ((n * 53) % 170), 50 + ((n * 89) % 180), 80 + ((n * 71) % 160));
}

// Precise stamps keep HH:mm from the original offset. Day buckets show MM-DD only.
inline std::wstring FormatScoreUpdatedAt(const std::wstring& iso, bool dayOnly) {
    if (iso.size() < 10 || iso[4] != L'-' || iso[7] != L'-') {
        return L"--";
    }
    std::wstring text = iso.substr(5, 5);
    if (!dayOnly && iso.size() >= 16 && (iso[10] == L'T' || iso[10] == L't')) {
        text += L' ';
        text += iso.substr(11, 5);
    }
    return text;
}
