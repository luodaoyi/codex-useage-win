#pragma once

#include <string>
#include <vector>

struct AccountEntry {
    std::wstring id;
    std::wstring path;
    std::wstring alias;
    std::wstring email;
    std::wstring provider;
    int order = 0;
    std::wstring label;
};

struct AccountOpResult {
    bool success = false;
    std::wstring id;
    std::wstring error;
};

class AccountLibrary {
public:
    AccountLibrary();
    explicit AccountLibrary(std::wstring rootDirectory);

    std::wstring RootDirectory() const;
    std::wstring AccountsDirectory() const;
    std::wstring IndexPath() const;
    std::vector<AccountEntry> List(const std::wstring& provider) const;
    AccountOpResult ImportText(const std::string& jsonText, const std::wstring& provider);
    AccountOpResult ImportFile(const std::wstring& sourcePath, const std::wstring& provider);
    AccountOpResult SetAlias(const std::wstring& id, const std::wstring& alias);
    AccountOpResult Move(const std::wstring& id, int delta);
    AccountOpResult Delete(const std::wstring& id);
    // Copy exe-directory auth JSON into accounts\ without modifying the source.
    int ImportSiblingAuthFiles();

private:
    std::wstring root_;
};
