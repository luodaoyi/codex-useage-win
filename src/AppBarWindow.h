#pragma once

#include "CodexUsageFetcher.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class AppBarWindow {
public:
    explicit AppBarWindow(HINSTANCE instance);
    ~AppBarWindow();

    bool Create();
    int Run();

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
    static constexpr int kModelScoresRefreshIntervalSeconds = 300;

    enum class Language {
        English = 0,
        Chinese = 1,
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
    void DiscardDeviceResources();
    void DiscardTextFormats();
    HRESULT EnsureTextFormats();
    HRESULT CreateTextFormat(float sizePixels, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format);

    void Paint(HDC hdc);
    void PaintContent(const RECT& clientRect);
    void ShowContextMenu(POINT screenPoint);
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

    UsageSnapshot snapshot_;
    ModelIqSnapshot modelScores_;
    CodexUsageFetcher fetcher_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidBrush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatKicker_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatTitle_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatDelta_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatMetricLabel_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatMetricValue_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormatFoot_;
};
