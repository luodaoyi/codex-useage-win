#include "AppBarWindow.h"

#include "AppUpdate.h"
#include "BrowserSignIn.h"
#include "JsonLite.h"
#include "Net.h"
#include "TextUtil.h"

#include <ShlObj.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

std::wstring ModuleDirectory() {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::wstring directory = std::filesystem::path(modulePath).parent_path().wstring();
    return directory.empty() ? L"." : directory;
}

std::wstring DataDirectory() {
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData != nullptr) {
        const std::wstring path = std::filesystem::path(appData) / L"CodexUsageBar";
        CoTaskMemFree(appData);
        return path;
    }
    return ModuleDirectory();
}

struct PromptState {
    HWND edit = nullptr;
    std::wstring text;
    bool accepted = false;
};

LRESULT CALLBACK PromptWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (message == WM_COMMAND && state != nullptr) {
        if (LOWORD(wParam) == IDOK) {
            const int length = GetWindowTextLengthW(state->edit);
            state->text.assign(static_cast<size_t>(length) + 1, L'\0');
            GetWindowTextW(state->edit, state->text.data(), length + 1);
            state->text.resize(static_cast<size_t>(length));
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::wstring LogsRoot(const std::wstring& provider) {
    wchar_t buffer[MAX_PATH] = {};
    const wchar_t* envName = provider == L"grok" ? L"GROK_HOME" : L"CODEX_HOME";
    const DWORD size = GetEnvironmentVariableW(envName, buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        return buffer;
    }
    const DWORD profile = GetEnvironmentVariableW(L"USERPROFILE", buffer, MAX_PATH);
    if (profile == 0 || profile >= MAX_PATH) {
        return L".";
    }
    return std::filesystem::path(buffer) / (provider == L"grok" ? L".grok" : L".codex");
}

}  // namespace

const wchar_t* AppBarWindow::Tr(
    const wchar_t* english,
    const wchar_t* simplified,
    const wchar_t* traditional,
    const wchar_t* korean,
    const wchar_t* japanese,
    const wchar_t* russian,
    const wchar_t* french) const {
    switch (language_) {
        case Language::Chinese: return simplified;
        case Language::Traditional: return traditional;
        case Language::Korean: return korean;
        case Language::Japanese: return japanese;
        case Language::Russian: return russian;
        case Language::French: return french;
        case Language::English: break;
    }
    return english;
}

std::optional<std::wstring> AppBarWindow::PromptText(const wchar_t* title, bool multiline) const {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = PromptWindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = L"CodexUsageBarPrompt";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&windowClass);
        registered = true;
    }
    PromptState state;
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"CodexUsageBarPrompt",
        title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        multiline ? 360 : 140,
        hwnd_,
        nullptr,
        instance_,
        &state);
    if (dialog == nullptr) {
        return std::nullopt;
    }
    state.edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0),
        12,
        12,
        480,
        multiline ? 250 : 28,
        dialog,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)),
        instance_,
        nullptr);
    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 300, multiline ? 274 : 52, 90, 28, dialog, reinterpret_cast<HMENU>(IDOK), instance_, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 400, multiline ? 274 : 52, 90, 28, dialog, reinterpret_cast<HMENU>(IDCANCEL), instance_, nullptr);
    EnableWindow(hwnd_, FALSE);
    ShowWindow(dialog, SW_SHOW);
    MSG message;
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    if (!state.accepted) {
        return std::nullopt;
    }
    return state.text;
}

int AppBarWindow::ExtraFeatureHeight() const {
    if (simpleMode_ || taskbarMode_ || hwnd_ == nullptr) {
        return 0;
    }
    const int dpi = static_cast<int>(GetDpiForWindow(hwnd_));
    auto scale = [&](int value) { return MulDiv(value, dpi > 0 ? dpi : 96, 96); };
    int height = 0;
    if (resetStatusEnabled_) {
        height += scale(18);
    }
    if (estimateEnabled_ && provider_ == L"codex") {
        height += scale(18);
    }
    if (featurePage_ != FeaturePage::None) {
        height += scale(170);
    }
    return height;
}

void AppBarWindow::LoadFeatureSettings() {
    const std::wstring path = GetSettingsPath();
    const int language = GetPrivateProfileIntW(L"features", L"language", -1, path.c_str());
    if (language >= 0 && language <= static_cast<int>(Language::French)) {
        language_ = static_cast<Language>(language);
    }
    resetStatusEnabled_ = GetPrivateProfileIntW(L"features", L"reset_status_enabled", 1, path.c_str()) != 0;
    resetStatusIntervalSeconds_ = GetPrivateProfileIntW(L"features", L"reset_status_interval_seconds", 300, path.c_str());
    if (resetStatusIntervalSeconds_ < 60) {
        resetStatusIntervalSeconds_ = 60;
    }
    estimateEnabled_ = GetPrivateProfileIntW(L"features", L"estimate_enabled", 1, path.c_str()) != 0;
    featurePage_ = static_cast<FeaturePage>(GetPrivateProfileIntW(L"features", L"detail_page", 0, path.c_str()));
    chartKind_ = static_cast<ChartKind>(GetPrivateProfileIntW(L"features", L"chart_kind", 0, path.c_str()));
    usageRange_ = static_cast<UsageRange>(GetPrivateProfileIntW(L"features", L"usage_range", 1, path.c_str()));
    wchar_t provider[32] = {};
    GetPrivateProfileStringW(L"features", L"provider", L"codex", provider, 32, path.c_str());
    provider_ = provider;
    if (provider_ != L"grok") {
        provider_ = L"codex";
    }
    const int proxyMode = GetPrivateProfileIntW(L"features", L"proxy_mode", 0, path.c_str());
    proxy_.mode = proxyMode == 1 ? ProxyConfig::Mode::Http : proxyMode == 2 ? ProxyConfig::Mode::Socks5 : ProxyConfig::Mode::System;
    wchar_t server[256] = {};
    wchar_t user[128] = {};
    GetPrivateProfileStringW(L"features", L"proxy_server", L"", server, 256, path.c_str());
    GetPrivateProfileStringW(L"features", L"proxy_user", L"", user, 128, path.c_str());
    proxy_.server = server;
    proxy_.user = user;
    LoadProxyPassword(&proxy_);
    SetProcessProxy(proxy_);
}

void AppBarWindow::SaveFeatureSettings() const {
    const std::wstring path = GetSettingsPath();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    WritePrivateProfileStringW(L"features", L"language", std::to_wstring(static_cast<int>(language_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"reset_status_enabled", resetStatusEnabled_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"features", L"reset_status_interval_seconds", std::to_wstring(resetStatusIntervalSeconds_).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"estimate_enabled", estimateEnabled_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"features", L"detail_page", std::to_wstring(static_cast<int>(featurePage_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"chart_kind", std::to_wstring(static_cast<int>(chartKind_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"usage_range", std::to_wstring(static_cast<int>(usageRange_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"provider", provider_.c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"proxy_mode", std::to_wstring(static_cast<int>(proxy_.mode)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"proxy_server", proxy_.server.c_str(), path.c_str());
    WritePrivateProfileStringW(L"features", L"proxy_user", proxy_.user.c_str(), path.c_str());
    SaveProxyPassword(proxy_);
}

void AppBarWindow::ReloadAccounts() {
    authMenuAccounts_ = accounts_.List(L"");
}

void AppBarWindow::PasteImport(const std::wstring& provider) {
    const auto text = PromptText(Tr(L"Paste auth JSON", L"粘贴凭证 JSON", L"貼上憑證 JSON", L"자격 증명 JSON 붙여넣기", L"認証 JSON を貼り付け", L"Вставьте JSON", L"Coller le JSON"), true);
    if (!text.has_value()) {
        return;
    }
    const AccountOpResult imported = accounts_.ImportText(WideToUtf8(*text), provider);
    if (!imported.success) {
        resetCreditActionMessage_ = imported.error;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    provider_ = provider;
    activeAuthId_ = imported.id;
    SaveActiveAuth();
    SaveFeatureSettings();
    RequestRefresh(true);
}

void AppBarWindow::RenameActiveAccount() {
    if (activeAuthId_.empty() && !authMenuAccounts_.empty()) {
        activeAuthId_ = authMenuAccounts_.front().id;
    }
    const auto alias = PromptText(Tr(L"Account alias", L"账号别名", L"帳號別名", L"계정 별칭", L"アカウント名", L"Псевдоним", L"Alias du compte"), false);
    if (!alias.has_value()) {
        return;
    }
    const AccountOpResult renamed = accounts_.SetAlias(activeAuthId_, *alias);
    resetCreditActionMessage_ = renamed.success
        ? Tr(L"Alias saved", L"别名已保存", L"別名已儲存", L"별칭 저장됨", L"別名を保存しました", L"Псевдоним сохранён", L"Alias enregistré")
        : renamed.error;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::MoveActiveAccount(int delta) {
    if (activeAuthId_.empty() && !authMenuAccounts_.empty()) {
        activeAuthId_ = authMenuAccounts_.front().id;
    }
    accounts_.Move(activeAuthId_, delta);
}

void AppBarWindow::DeleteActiveAccount() {
    if (activeAuthId_.empty() && !authMenuAccounts_.empty()) {
        activeAuthId_ = authMenuAccounts_.front().id;
    }
    if (activeAuthId_.empty()) {
        return;
    }
    if (MessageBoxW(hwnd_,
            Tr(L"Delete this imported copy? The original auth file is not touched.",
                L"删除这份导入副本？不会改动原来的凭证文件。",
                L"刪除這份匯入副本？不會改動原來的憑證檔。",
                L"가져온 복사본을 삭제할까요? 원본 파일은 그대로입니다.",
                L"このコピーを削除しますか？元のファイルは変更しません。",
                L"Удалить эту копию? Исходный файл не изменится.",
                L"Supprimer cette copie ? Le fichier d'origine n'est pas modifié."),
            Tr(L"Delete account", L"删除账号", L"刪除帳號", L"계정 삭제", L"アカウントを削除", L"Удалить аккаунт", L"Supprimer le compte"),
            MB_ICONWARNING | MB_YESNO) != IDYES) {
        return;
    }
    const AccountOpResult deleted = accounts_.Delete(activeAuthId_);
    if (!deleted.success) {
        resetCreditActionMessage_ = deleted.error;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    activeAuthId_.clear();
    SaveActiveAuth();
    snapshot_ = {};
    grok_ = {};
    RequestRefresh(true);
}

void AppBarWindow::StartBrowserSignIn() {
    if (browserSignInInFlight_.exchange(true)) {
        return;
    }
    const HWND target = hwnd_;
    const SignInProvider provider = provider_ == L"grok" ? SignInProvider::Grok : SignInProvider::Codex;
    const std::wstring root = accounts_.RootDirectory();
    std::thread([this, target, provider, root]() {
        auto* result = new BrowserSignInResult(RunBrowserSignIn(provider, root));
        PostMessageW(target, kBrowserDoneMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::RequestResetStatus() {
    if (!resetStatusEnabled_) {
        return;
    }
    if (resetStatusInFlight_.exchange(true)) {
        return;
    }
    const HWND target = hwnd_;
    std::thread([this, target]() {
        auto* result = new ResetStatusInfo(FetchResetStatus());
        PostMessageW(target, kResetStatusMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::RequestGrokRefresh() {
    grokInFlight_ = true;
    const std::wstring path = ActiveAuthPath();
    const HWND target = hwnd_;
    std::thread([this, target, path]() {
        auto* result = new GrokSnapshot(FetchGrokBillingFile(path));
        PostMessageW(target, kGrokUpdatedMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::RequestSessionScan() {
    if (sessionScanInFlight_.exchange(true)) {
        return;
    }
    const HWND target = hwnd_;
    const UsageRange range = usageRange_;
    const std::wstring provider = provider_;
    std::thread([this, target, range, provider]() {
        SessionIndex index(LogsRoot(provider), DataDirectory() + L"\\session-index");
        auto* result = new SessionScan(index.Scan(range));
        PostMessageW(target, kSessionScanMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::RepairSessions() {
    SessionIndex index(LogsRoot(provider_), DataDirectory() + L"\\session-index");
    const std::wstring indexPath = LogsRoot(provider_) + L"\\session_index.jsonl";
    std::wstring error;
    const bool ok = index.RepairSessionIndex(indexPath, &error);
    resetCreditActionMessage_ = ok
        ? Tr(L"Session index repaired", L"会话索引已修复", L"工作階段索引已修復", L"세션 인덱스를 고쳤습니다", L"セッション索引を修復しました", L"Индекс сессий исправлен", L"Index des sessions réparé")
        : error;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::ApplyProxyMode(ProxyConfig::Mode mode) {
    proxy_.mode = mode;
    SetProcessProxy(proxy_);
    SaveFeatureSettings();
}

void AppBarWindow::ConfigureProxyServer() {
    const auto server = PromptText(Tr(L"Proxy host:port", L"代理 host:port", L"代理 host:port", L"프록시 host:port", L"プロキシ host:port", L"Прокси host:port", L"Proxy host:port"), false);
    if (!server.has_value()) {
        return;
    }
    proxy_.server = *server;
    const auto user = PromptText(Tr(L"Proxy username (empty to skip)", L"代理用户名，可留空", L"代理使用者名稱，可留空", L"프록시 사용자, 비워도 됩니다", L"プロキシユーザー。空でも可", L"Пользователь прокси, можно пусто", L"Utilisateur proxy, vide possible"), false);
    if (user.has_value()) {
        proxy_.user = *user;
    }
    const auto password = PromptText(Tr(L"Proxy password (stored in Credential Manager)", L"代理密码，存入凭据管理器", L"代理密碼，存入認證管理員", L"프록시 암호는 자격 증명 관리자에 저장", L"パスワードは資格情報マネージャーへ", L"Пароль сохранится в диспетчере учётных данных", L"Mot de passe dans le gestionnaire d'identifiants"), false);
    if (password.has_value()) {
        proxy_.password = *password;
    }
    SetProcessProxy(proxy_);
    SaveFeatureSettings();
}

void AppBarWindow::TestProxy() {
    std::wstring error;
    const auto body = NetHttps(L"www.codexrunway.com", L"/api/status.json", L"GET", {L"Accept: application/json", L"User-Agent: CodexUsageBar"}, nullptr, &error);
    resetCreditActionMessage_ = body.has_value()
        ? Tr(L"Proxy test succeeded", L"代理测试成功", L"代理測試成功", L"프록시 테스트 성공", L"プロキシ試験は成功", L"Проверка прокси успешна", L"Test du proxy réussi")
        : (error.empty() ? std::wstring(L"proxy test failed") : error);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::DownloadAndStageUpdate() {
    if (MessageBoxW(hwnd_,
            Tr(L"Download the latest GitHub build and replace this exe after it exits?",
                L"下载 GitHub 最新构建，并在退出后替换当前程序？",
                L"下載 GitHub 最新組建，並在結束後取代目前程式？",
                L"최신 GitHub 빌드를 받아 종료 후 이 프로그램을 바꿀까요?",
                L"最新の GitHub ビルドを受け取り、終了後に置き換えますか？",
                L"Скачать последнюю сборку GitHub и заменить программу после выхода?",
                L"Télécharger la build GitHub et remplacer ce programme après fermeture ?"),
            Tr(L"Update", L"更新", L"更新", L"업데이트", L"更新", L"Обновление", L"Mise à jour"),
            MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    const StagedUpdate update = DownloadLatestRelease(GetExecutablePath());
    if (!update.success) {
        resetCreditActionMessage_ = update.error;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!LaunchReplaceScript(update, GetExecutablePath())) {
        resetCreditActionMessage_ = L"cannot start the replace script";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    DestroyWindow(hwnd_);
}
