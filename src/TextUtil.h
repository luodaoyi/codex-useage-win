#pragma once

#include <Windows.h>

#include <ctime>
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

// Label without the trailing effort word. The depth column shows that word.
inline std::wstring ModelScoreDisplayName(const std::wstring& label, const std::wstring& effort) {
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
    return name;
}

// "$1.99", never "$  1.99". Width lives in the column, not inside the text.
inline std::wstring FormatRadarCostText(bool hasPrice, double usd) {
    if (!hasPrice) {
        return L"--";
    }
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"$%.2f", usd);
    return std::wstring(buffer);
}

// Client width that keeps the measured model name inside the name cell.
// nameTextWidth is the glyph width. The other inputs are full column widths.
inline int RadarScoreTableMinimumClientWidth(
    int nameTextWidth,
    int effortCol,
    int scoreCol,
    int timeCol,
    int costCol,
    int updatedCol,
    int accentWidth,
    int cellPad,
    int nameGap,
    int horizontalPadding) {
    return nameTextWidth + accentWidth + cellPad + nameGap
        + effortCol + scoreCol + timeCol + costCol + updatedCol
        + 2 + horizontalPadding * 2;
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

    const std::wstring name = ModelScoreDisplayName(label, effort);
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
    // Use distinct hues for the eight benchmark models; formula colors can look alike.
    static const COLORREF lightColors[] = {
        RGB(0, 70, 200), RGB(195, 105, 0), RGB(105, 45, 200), RGB(0, 115, 35),
        RGB(180, 20, 135), RGB(0, 125, 130), RGB(105, 130, 0), RGB(190, 25, 40),
    };
    static const COLORREF darkColors[] = {
        RGB(90, 140, 255), RGB(255, 170, 40), RGB(174, 140, 255), RGB(90, 220, 120),
        RGB(255, 125, 200), RGB(60, 220, 240), RGB(220, 235, 110), RGB(255, 95, 95),
    };
    if (n < 8) {
        return lightTheme ? lightColors[n] : darkColors[n];
    }
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
    if (dayOnly || iso.size() == 10) {
        return iso.substr(5, 5);
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (swscanf_s(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return L"--";
    }
    std::tm parts = {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    time_t utc = _mkgmtime(&parts);
    if (utc == static_cast<time_t>(-1)) {
        return L"--";
    }
    const size_t sign = iso.find_first_of(L"+-", 19);
    if (sign != std::wstring::npos) {
        int offsetHour = 0, offsetMinute = 0;
        if (swscanf_s(iso.c_str() + sign + 1, L"%2d:%2d", &offsetHour, &offsetMinute) != 2
            || offsetHour > 23 || offsetMinute > 59) {
            return L"--";
        }
        const int offsetSeconds = (offsetHour * 60 + offsetMinute) * 60;
        utc -= iso[sign] == L'-' ? -offsetSeconds : offsetSeconds;
    }
    std::tm local = {};
    if (localtime_s(&local, &utc) != 0) {
        return L"--";
    }
    wchar_t text[32] = {};
    wcsftime(text, _countof(text), L"%m-%d %H:%M", &local);
    return text;
}
