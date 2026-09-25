#pragma once

#include "AccountLibrary.h"
#include "CodexUsageFetcher.h"
#include "GrokBilling.h"
#include "ProxyConfig.h"
#include "QuotaEstimate.h"
#include "ResetStatus.h"
#include "SessionIndex.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct MenuEntry {
    UINT command = 0;
    std::wstring text;
    bool checked = false;
    bool header = false;
    bool enabled = true;
};

struct TokenMaintenanceReport {
    int refreshed = 0;
    int failed = 0;
    std::wstring error;
    std::vector<std::wstring> revokedIds;
    std::vector<std::wstring> failedIds;
};

class AppBarWindow {
public:
    explicit AppBarWindow(HINSTANCE instance);
    ~AppBarWindow();

    bool Create();
    int Run();
    void SetStickyMenu(HWND hwnd) { stickyMenu_ = hwnd; }
    std::vector<MenuEntry> BuildMenuEntries();
    void HandleMenuCommand(UINT command);

private:
    static constexpr UINT kUsageUpdatedMessage = WM_APP + 1;
    static constexpr UINT kReleaseVersionUpdatedMessage = WM_APP + 2;
    static constexpr UINT kResetCreditConsumedMessage = WM_APP + 3;
    static constexpr UINT kTokenRefreshedMessage = WM_APP + 4;
    static constexpr UINT kModelScoresUpdatedMessage = WM_APP + 5;
    static constexpr UINT_PTR kCountdownTimerId = 1;
    static constexpr UINT_PTR kRefreshTimerId = 2;
    static constexpr UINT_PTR kResetConfirmTimerId = 3;
    static constexpr UINT_PTR kModelScoresTimerId = 4;
    static constexpr UINT_PTR kResetStatusTimerId = 5;
    static constexpr UINT_PTR kTokenMaintenanceTimerId = 6;
    // Check every configured account, shown or not. Independent of the usage interval.
    static constexpr int kTokenMaintenanceIntervalSeconds = 300;
    static constexpr UINT kBrowserDoneMessage = WM_APP + 6;
    static constexpr UINT kSessionScanMessage = WM_APP + 7;
    static constexpr UINT kResetStatusMessage = WM_APP + 8;
    static constexpr UINT kGrokUpdatedMessage = WM_APP + 9;
    static constexpr UINT kSummaryRowMessage = WM_APP + 10;
    static constexpr UINT kSummaryDoneMessage = WM_APP + 11;
    static constexpr UINT kTokenMaintenanceMessage = WM_APP + 12;
    static constexpr int kModelScoresRefreshIntervalSeconds = 300;

    enum class Language {
        English = 0,
        Chinese = 1,
        Traditional = 2,
        Korean = 3,
        Japanese = 4,
        Russian = 5,
        French = 6,
    };

    enum class FeaturePage {
        None = 0,
        Chart = 1,
        Sessions = 2,
    };

    enum class Surface {
        Usage = 0,
        Accounts = 1,
        Settings = 2,
        Summary = 3,
    };

    struct UiHit {
        RECT rect = {};
        UINT command = 0;
        std::wstring accountId;
    };

    struct AccountQuotaRow {
        std::wstring id;
        std::wstring label;
        std::wstring alias;
        std::wstring email;
        std::wstring provider;
        bool loading = true;
        bool success = false;
        std::wstring error;
        bool hasQuota = false;
        int remainingPercent = 100;
        long long resetAtUnixSeconds = 0;
        std::wstring resetIso;
    };

    enum class ChartKind {
        Heat = 0,
        Line = 1,
        Bar = 2,
    };

    enum class DragMode {
        None,
        Move,
        ResizeRight,
        ResizeBottom,
        ResizeCorner,
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void RegisterWindowClass();
    RECT GetDesktopClientRect() const;
    bool GetCurrentMonitorInfo(MONITORINFO& monitorInfo) const;
    RECT GetCurrentMonitorWorkRect() const;
    RECT BuildDefaultRect(const RECT& desktopRect) const;
    RECT BuildTaskbarDockRect() const;
    RECT ClampRectToDesktop(RECT rect) const;
    void UpdateWindowBounds(bool useSavedPosition);
    // Recompute height from current snapshot (e.g. hide 5h bar) while keeping position.
    void FitWindowToContent();
    void SetDisplayMode(bool simpleMode, bool taskbarMode);

    void LoadSettings();
    void SaveSettings() const;
    void SaveActiveAuth() const;
    void LoadFeatureSettings();
    void SaveFeatureSettings() const;
    std::wstring ActiveAuthPath() const;
    bool IsActiveAuth(const AccountEntry& account) const;
    int ExtraFeatureHeight() const;
    void ReloadAccounts();
    void PasteImport(const std::wstring& provider);
    void RenameActiveAccount();
    void RenameAccountById(const std::wstring& id);
    void MoveActiveAccount(int delta);
    void DeleteActiveAccount();
    void DeleteAccountById(const std::wstring& id);
    void StartBrowserSignIn();
    void RequestResetStatus();
    void RequestSessionScan();
    void RepairSessions();
    void ApplyProxyMode(ProxyConfig::Mode mode);
    void ConfigureProxyServer();
    void TestProxy();
    void DownloadAndStageUpdate();
    void RequestGrokRefresh();
    void RequestTokenMaintenance();
    void OnTokenMaintenance(TokenMaintenanceReport* report);
    void SeedSummaryRows();
    void RequestSummaryRefresh();
    void OnSummaryRowUpdated(int generation, AccountQuotaRow* row);
    void OnSummaryRefreshDone(int generation);
    int SummaryContentHeight() const;
    const wchar_t* Tr(
        const wchar_t* english,
        const wchar_t* simplified,
        const wchar_t* traditional,
        const wchar_t* korean,
        const wchar_t* japanese,
        const wchar_t* russian,
        const wchar_t* french) const;
    std::optional<std::wstring> PromptText(const wchar_t* title, bool multiline) const;
    std::wstring GetSettingsPath() const;
    std::wstring GetExecutablePath() const;
    void RefreshTheme();
    bool IsDesktopLightTheme() const;
    bool IsLaunchAtStartupEnabled() const;
    bool SetLaunchAtStartupEnabled(bool enabled) const;

    DragMode HitTestDragMode(POINT clientPoint) const;
    void BeginDrag(DragMode mode, POINT screenPoint);
    void UpdateDrag(POINT screenPoint);
    void EndDrag(bool saveSettings);

    void RequestRefresh(bool force);
    void OnUsageUpdated(UsageSnapshot* snapshot);
    void RequestLatestReleaseCheck(bool force);
    void OnLatestReleaseChecked(ReleaseVersionInfo* info);
    void OnResetCreditConsumed(ConsumeResetCreditResult* result);
    void ArmOrConsumeResetCredit();
    void RequestConsumeResetCredit();
    void RequestRefreshToken();
    void ImportAccount();
    void OnTokenRefreshed(TokenRefreshResult* result);
    void RequestModelScoresRefresh(bool force);
    void OnModelScoresUpdated(ModelIqSnapshot* snapshot);
    void SetModelScoreMode(bool enabled, RadarMetricKind kind);
    void ToggleModelScoreFamily(const std::wstring& familyKey);
    void SetModelScoresPage(int page);
    void ClampModelScoresPage();
    bool IsModelScoreFamilySelected(const std::wstring& familyKey) const;
    bool MatchesModelScoreFamily(const ModelIqScore& score) const;
    std::vector<std::pair<std::wstring, std::wstring>> ListModelScoreFamilies() const;
    int CountFilteredModelScores() const;
    int GetModelScoresPageCount() const;
    int GetModelScoresVisibleRowCount() const;
    int GetModelScoreFilterBandHeight(int innerWidth) const;

    struct ModelScoreFilterChip {
        RECT rect = {};
        std::wstring key;
        std::wstring label;
        bool selected = false;
    };
    std::vector<ModelScoreFilterChip> BuildModelScoreFilterChips(int left, int top, int right) const;
    void RestartModelScoresTimer();
    int GetModelScoresPanelHeight() const;
    bool TryHandleActionButtonClick(POINT clientPoint);
    std::wstring BuildResetCreditsSummaryText() const;
    std::wstring BuildResetCreditsExpiryText() const;
    std::wstring CreateRedeemRequestId() const;

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    HRESULT EnsureBrandIcons();
    void DiscardDeviceResources();
    void DiscardTextFormats();
    HRESULT EnsureTextFormats();
    HRESULT CreateTextFormat(float sizePixels, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format);

    void Paint(HDC hdc);
    void PaintContent(const RECT& clientRect);
    void DrawAccountDropdown();
    void ShowContextMenu(POINT screenPoint);
    HMENU CreateContextMenuHandle();
    int GetMinimumWidgetWidth() const;
    int GetMinimumWidgetHeight(int width) const;
    void SetLanguage(Language language);
    void SetRefreshIntervalSeconds(int seconds);
    void RestartRefreshTimer();
    const wchar_t* LocalizeText(const wchar_t* english, const wchar_t* chinese) const;
    std::wstring GetVersionStatusText(bool compact) const;

    std::wstring FormatDuration(int totalSeconds) const;
    std::wstring FormatRefreshCountdown(int totalSeconds) const;
    std::wstring FormatDateTime(long long unixSeconds) const;
    std::wstring FormatFullDateTime(long long unixSeconds) const;
    std::wstring FormatClockTime(long long unixSeconds) const;
    std::wstring FormatPercent(double value) const;
    std::wstring FormatPlanDisplayName() const;
    // remainingPercent: 100 = healthy green, 0 = critical red (soft, not pure).
    COLORREF ColorForRemainingPercent(int remainingPercent, bool forBackground) const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::atomic_bool refreshInFlight_ = false;
    std::atomic_bool releaseCheckInFlight_ = false;
    std::atomic_bool resetCreditInFlight_ = false;
    std::atomic_bool tokenRefreshInFlight_ = false;
    std::atomic_bool modelScoresInFlight_ = false;
    bool lightTheme_ = false;
    bool alwaysOnTop_ = false;
    bool lockPosition_ = false;
    bool simpleMode_ = false;
    bool taskbarMode_ = false;
    bool showModelScores_ = false;
    RadarMetricKind modelScoreKind_ = RadarMetricKind::SoftwareEngineering;
    std::vector<std::wstring> selectedModelFamilyKeys_;
    std::vector<ModelScoreFilterChip> modelScoreFilterChips_;
    int modelScoresPage_ = 0;
    bool hasReleaseCheckResult_ = false;
    bool updateAvailable_ = false;
    // 0 = idle, 1/2 = armed steps (menu), 3rd selection opens MessageBox before consume.
    int resetCreditConfirmStep_ = 0;
    Language language_ = Language::English;
    bool hasSavedRect_ = false;
    RECT savedRect_ = {};
    DragMode dragMode_ = DragMode::None;
    POINT dragStartPoint_ = {};
    RECT dragStartRect_ = {};
    UINT textFormatDpi_ = 0;
    long long lastSuccessfulRefreshUnixSeconds_ = 0;
    long long lastReleaseCheckUnixSeconds_ = 0;
    int refreshIntervalSeconds_ = 60;
    int refreshCountdownSeconds_ = 60;
    int releaseCheckCountdownSeconds_ = 6 * 60 * 60;
    std::wstring latestReleaseTag_;
    std::wstring releaseCheckErrorMessage_;
    std::wstring resetCreditActionMessage_;
    RECT refreshButtonRect_ = {};
    RECT modelScoresPrevRect_ = {};
    RECT modelScoresNextRect_ = {};
    RECT modelScoresSourceRect_ = {};

    UsageSnapshot snapshot_;
    ModelIqSnapshot modelScores_;
    CodexUsageFetcher fetcher_;
    AccountLibrary accounts_;
    std::wstring provider_ = L"codex";
    bool resetStatusEnabled_ = true;
    int resetStatusIntervalSeconds_ = 300;
    ResetStatusInfo resetStatus_;
    bool estimateEnabled_ = true;
    QuotaEstimateView estimate_;
    FeaturePage featurePage_ = FeaturePage::None;
    Surface surface_ = Surface::Usage;
    bool accountDropOpen_ = false;
    RECT accountDropRect_ = {};
    std::vector<UiHit> uiHits_;
    ChartKind chartKind_ = ChartKind::Heat;
    UsageRange usageRange_ = UsageRange::Cycle;
    SessionScan sessionScan_;
    GrokSnapshot grok_;
    ProxyConfig proxy_;
    std::atomic_bool sessionScanInFlight_ = false;
    std::atomic_bool resetStatusInFlight_ = false;
    std::atomic_bool grokInFlight_ = false;
    std::atomic_bool browserSignInInFlight_ = false;
    std::atomic_bool summaryInFlight_ = false;
    std::atomic_bool tokenMaintenanceInFlight_ = false;
    // Account id -> unix time before which a failed refresh is not retried.
    std::vector<std::pair<std::wstring, long long>> tokenRefreshNotBefore_;
    int summaryGeneration_ = 0;
    std::vector<AccountQuotaRow> summaryRows_;
    RECT resetLinkRect_ = {};
    HWND stickyMenu_ = nullptr;
    // Settings key of the selected credential file. Empty means the default slot.
    std::wstring activeAuthId_;
    // Account id captured when the in-flight usage refresh started.
    std::wstring inflightAuthId_;
    std::vector<AccountEntry> authMenuAccounts_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidBrush_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> codexIcon_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> grokIcon_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatKicker_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatTitle_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatDelta_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatMetricLabel_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatMetricValue_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatFoot_;
};
