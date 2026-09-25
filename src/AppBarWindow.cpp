#include "AppBarWindow.h"
#include "AppVersion.h"
#include "BrandIcons.h"
#include "BrowserSignIn.h"
#include "TextUtil.h"

#include <commdlg.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <wincodec.h>
#include <winreg.h>
#include <windowsx.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kWindowClassName[] = L"CodexUsageBarWindow";
constexpr const wchar_t* kCurrentVersion = APP_VERSION_W;
// Bumped when default geometry / full-mode layout changes.
constexpr int kLayoutVersion = 9;
constexpr UINT kCommandRefresh = 1;
constexpr UINT kCommandExit = 2;
constexpr UINT kCommandResetPosition = 3;
constexpr UINT kCommandLaunchAtStartup = 4;
constexpr UINT kCommandAlwaysOnTop = 5;
constexpr UINT kCommandLockPosition = 6;
constexpr UINT kCommandSimpleMode = 7;
constexpr UINT kCommandLanguageEnglish = 8;
constexpr UINT kCommandLanguageChinese = 9;
constexpr UINT kCommandRefreshInterval1Minute = 10;
constexpr UINT kCommandRefreshInterval3Minutes = 11;
constexpr UINT kCommandRefreshInterval5Minutes = 12;
constexpr UINT kCommandRefreshInterval10Minutes = 13;
constexpr UINT kCommandRefreshInterval30Minutes = 14;
constexpr UINT kCommandCheckVersion = 15;
constexpr UINT kCommandFullMode = 16;
constexpr UINT kCommandTaskbarMode = 17;
constexpr UINT kCommandRefreshToken = 18;
constexpr UINT kCommandModelScoresOff = 19;
constexpr UINT kCommandModelScoresSoftware = 20;
constexpr UINT kCommandModelScoresVisual = 21;
constexpr UINT kCommandResetCredit = 22;
constexpr UINT kCommandImportAccount = 23;
constexpr UINT kCommandPasteImport = 24;
constexpr UINT kCommandAlias = 25;
constexpr UINT kCommandMoveUp = 26;
constexpr UINT kCommandMoveDown = 27;
constexpr UINT kCommandDeleteAccount = 28;
constexpr UINT kCommandResetStatusToggle = 29;
constexpr UINT kCommandEstimateToggle = 30;
constexpr UINT kCommandDetailOff = 31;
constexpr UINT kCommandDetailChart = 32;
constexpr UINT kCommandDetailSessions = 33;
constexpr UINT kCommandRepairSessions = 34;
constexpr UINT kCommandChartHeat = 35;
constexpr UINT kCommandChartLine = 36;
constexpr UINT kCommandChartBar = 37;
constexpr UINT kCommandProviderCodex = 38;
constexpr UINT kCommandProviderGrok = 39;
constexpr UINT kCommandBrowserLogin = 40;
constexpr UINT kCommandProxySystem = 44;
constexpr UINT kCommandProxyHttp = 45;
constexpr UINT kCommandProxySocks = 46;
constexpr UINT kCommandProxyEdit = 47;
constexpr UINT kCommandProxyTest = 48;
constexpr UINT kCommandDownloadUpdate = 49;
constexpr UINT kCommandRangeToday = 50;
constexpr UINT kCommandRangeCycle = 51;
constexpr UINT kCommandRangePrev = 52;
constexpr UINT kCommandRangeMonth = 53;
constexpr UINT kCommandLanguageTraditional = 60;
constexpr UINT kCommandLanguageKorean = 61;
constexpr UINT kCommandLanguageJapanese = 62;
constexpr UINT kCommandLanguageRussian = 63;
constexpr UINT kCommandLanguageFrench = 64;
constexpr UINT kCommandCloseMenu = 70;
constexpr UINT kUiSettings = 80;
constexpr UINT kUiAccounts = 81;
constexpr UINT kUiCloseSheet = 82;
constexpr UINT kUiPrevAccount = 83;
constexpr UINT kUiNextAccount = 84;
constexpr UINT kUiSummary = 85;
constexpr UINT kCommandAccountBase = 1000;
constexpr UINT kMaxAuthMenuAccounts = 64;
constexpr int kModelScoresPageSize = 10;
constexpr int kDefaultWidgetWidth = 420;
constexpr int kMinimumWidgetWidth = 360;
constexpr int kSimpleDefaultWidgetWidth = 240;
constexpr int kSimpleMinimumWidgetWidth = 220;
constexpr int kTaskbarDefaultWidgetWidth = 184;
constexpr int kTaskbarMinimumWidgetWidth = 160;
constexpr int kTaskbarWidgetHeight = 46;
constexpr int kDesktopMargin = 18;
constexpr int kHorizontalPadding = 14;
constexpr int kVerticalPadding = 12;
constexpr int kResizeGrip = 12;
constexpr long long kDaySeconds = 24LL * 60 * 60;
constexpr long long kWeekSeconds = 7LL * kDaySeconds;
constexpr int kReleaseCheckIntervalSeconds = 6 * 60 * 60;

int SanitizeRefreshIntervalSeconds(int seconds) {
    switch (seconds) {
        case 60:
        case 180:
        case 300:
        case 600:
        case 1800:
            return seconds;
        default:
            return 60;
    }
}

std::vector<int> ParseVersionParts(const std::wstring& version) {
    std::vector<int> parts;
    int value = 0;
    bool inNumber = false;

    for (wchar_t ch : version) {
        if (ch >= L'0' && ch <= L'9') {
            value = value * 10 + (ch - L'0');
            inNumber = true;
        } else if (inNumber) {
            parts.push_back(value);
            value = 0;
            inNumber = false;
        }
    }
    if (inNumber) {
        parts.push_back(value);
    }

    return parts;
}

int CompareVersions(const std::wstring& left, const std::wstring& right) {
    const std::vector<int> leftParts = ParseVersionParts(left);
    const std::vector<int> rightParts = ParseVersionParts(right);
    const size_t count = std::max(leftParts.size(), rightParts.size());

    for (size_t i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts[i] : 0;
        const int rightValue = i < rightParts.size() ? rightParts[i] : 0;
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }

    return 0;
}

int ScaleForDpi(HWND hwnd, int value) {
    const UINT dpi = GetDpiForWindow(hwnd != nullptr ? hwnd : GetDesktopWindow());
    return MulDiv(value, static_cast<int>(dpi), 96);
}

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

int CalculateDetailedMinimumWidgetHeight(HWND hwnd, int width) {
    (void)width;
    // Weekly-only compact card (1 credit row), refresh sits on the footer row.
    return ScaleForDpi(hwnd, 184);
}

int CalculateSimpleMinimumWidgetHeight(HWND hwnd) {
    return ScaleForDpi(hwnd, 132);
}

int CalculateTaskbarWidgetHeight(HWND hwnd) {
    return ScaleForDpi(hwnd, kTaskbarWidgetHeight);
}

RECT ShrinkRect(const RECT& rect, int dx, int dy) {
    RECT output = rect;
    output.left += dx;
    output.right -= dx;
    output.top += dy;
    output.bottom -= dy;
    return output;
}

struct PaceInfo {
    bool valid = false;
    double dailyBudgetPercent = 0.0;
    double expectedUsedPercent = 0.0;
    double actualUsedPercent = 0.0;
    double fiveHourExpectedUsedPercent = 0.0;
    double fiveHourActualUsedPercent = 0.0;
    double weeklyRemainingPercent = 0.0;
    double deltaPercent = 0.0;
    int cycleDay = 0;
    int elapsedSeconds = 0;
    int remainingSeconds = 0;
    long long weekStartUnixSeconds = 0;
    bool isOver = false;
};

int ClampInt(int value, int minValue, int maxValue) {
    return std::min(maxValue, std::max(minValue, value));
}

double ClampDouble(double value, double minValue, double maxValue) {
    return std::min(maxValue, std::max(minValue, value));
}

RECT MakeRect(int left, int top, int right, int bottom) {
    RECT rect = { left, top, right, bottom };
    return rect;
}

D2D1_RECT_F ToRectF(const RECT& rect) {
    return D2D1::RectF(
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right),
        static_cast<float>(rect.bottom));
}

D2D1_COLOR_F ToColorF(COLORREF color, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(color)) / 255.0f,
        static_cast<float>(GetGValue(color)) / 255.0f,
        static_cast<float>(GetBValue(color)) / 255.0f,
        alpha);
}

void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void StrokeRect(HDC hdc, const RECT& rect, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

std::wstring FormatNumber(double value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%.1f", value);
    return buffer;
}

std::wstring FormatNumberNoUnit(double value) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%.1f", value);
    return buffer;
}

PaceInfo BuildPaceInfo(const UsageSnapshot& snapshot) {
    PaceInfo info;
    if (!snapshot.success) {
        return info;
    }

    if (snapshot.weekly.available && snapshot.weekly.windowSeconds > 0) {
        info.dailyBudgetPercent = 100.0 / 7.0;
        info.actualUsedPercent = static_cast<double>(snapshot.weekly.usedPercent);
        info.weeklyRemainingPercent = static_cast<double>(snapshot.weekly.remainingPercent);
        info.remainingSeconds = std::max(0, snapshot.weekly.resetAfterSeconds);
        info.elapsedSeconds = ClampInt(
            snapshot.weekly.windowSeconds - info.remainingSeconds, 0, snapshot.weekly.windowSeconds);
        info.weekStartUnixSeconds = snapshot.weekly.resetAtUnixSeconds - snapshot.weekly.windowSeconds;

        const int elapsedDays = info.elapsedSeconds <= 0 ? 0 : (info.elapsedSeconds / static_cast<int>(kDaySeconds));
        info.cycleDay = ClampInt(elapsedDays + 1, 1, 7);
        info.expectedUsedPercent = ClampDouble(info.cycleDay * info.dailyBudgetPercent, 0.0, 100.0);
        info.deltaPercent = info.actualUsedPercent - info.expectedUsedPercent;
        info.isOver = info.deltaPercent > 0.001;
        info.valid = true;
    }

    // 5-hour budget line only when the API still returns a short session window.
    if (snapshot.fiveHour.available && snapshot.fiveHour.windowSeconds > 0) {
        info.fiveHourActualUsedPercent = static_cast<double>(snapshot.fiveHour.usedPercent);
        const int fiveElapsed = ClampInt(
            snapshot.fiveHour.windowSeconds - snapshot.fiveHour.resetAfterSeconds,
            0,
            snapshot.fiveHour.windowSeconds);
        info.fiveHourExpectedUsedPercent = ClampDouble(
            100.0 * static_cast<double>(fiveElapsed) / static_cast<double>(snapshot.fiveHour.windowSeconds),
            0.0,
            100.0);
        if (!info.valid) {
            info.valid = true;
        }
    }

    return info;
}

}  // namespace

AppBarWindow::AppBarWindow(HINSTANCE instance) : instance_(instance) {}

AppBarWindow::~AppBarWindow() {
    DiscardTextFormats();
    DiscardDeviceResources();
}

bool AppBarWindow::Create() {
    RegisterWindowClass();
    LoadSettings();
    LoadFeatureSettings();
    accounts_.ImportSiblingAuthFiles();
    for (const AccountEntry& account : accounts_.List(L"")) {
        if (_wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0 && !account.provider.empty()) {
            provider_ = account.provider;
            SaveFeatureSettings();
            break;
        }
    }
    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClassName,
        LocalizeText(L"Codex Usage Widget", L"Codex 用量挂件"),
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr) {
        return false;
    }

    if (FAILED(CreateDeviceIndependentResources())) {
        return false;
    }

    RefreshTheme();
    UpdateWindowBounds(true);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);

    refreshCountdownSeconds_ = refreshIntervalSeconds_;
    releaseCheckCountdownSeconds_ = kReleaseCheckIntervalSeconds;
    SetTimer(hwnd_, kCountdownTimerId, 1000, nullptr);
    RestartRefreshTimer();
    SetTimer(hwnd_, kTokenMaintenanceTimerId, static_cast<UINT>(kTokenMaintenanceIntervalSeconds * 1000), nullptr);
    RequestTokenMaintenance();
    RequestRefresh(true);
    RequestLatestReleaseCheck(true);
    if (showModelScores_) {
        RestartModelScoresTimer();
        RequestModelScoresRefresh(true);
    }
    if (resetStatusEnabled_) {
        SetTimer(hwnd_, kResetStatusTimerId, static_cast<UINT>(resetStatusIntervalSeconds_ * 1000), nullptr);
        RequestResetStatus();
    }
    return true;
}

int AppBarWindow::Run() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK AppBarWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<AppBarWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<AppBarWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT AppBarWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            if (wParam == kCountdownTimerId) {
                if (snapshot_.success) {
                    snapshot_.fiveHour.resetAfterSeconds = std::max(0, snapshot_.fiveHour.resetAfterSeconds - 1);
                    snapshot_.weekly.resetAfterSeconds = std::max(0, snapshot_.weekly.resetAfterSeconds - 1);
                }
                refreshCountdownSeconds_ = std::max(0, refreshCountdownSeconds_ - 1);
                releaseCheckCountdownSeconds_ = std::max(0, releaseCheckCountdownSeconds_ - 1);
                if (releaseCheckCountdownSeconds_ == 0) {
                    RequestLatestReleaseCheck(false);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (wParam == kRefreshTimerId) {
                refreshCountdownSeconds_ = refreshIntervalSeconds_;
                RequestRefresh(false);
            } else if (wParam == kTokenMaintenanceTimerId) {
                RequestTokenMaintenance();
            } else if (wParam == kResetConfirmTimerId) {
                KillTimer(hwnd_, kResetConfirmTimerId);
                resetCreditConfirmStep_ = 0;
                resetCreditActionMessage_.clear();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (wParam == kModelScoresTimerId) {
                RequestModelScoresRefresh(false);
            } else if (wParam == kResetStatusTimerId) {
                RequestResetStatus();
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd_, &ps);
            Paint(hdc);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_THEMECHANGED:
            RefreshTheme();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_SETTINGCHANGE:
            RefreshTheme();
            if (taskbarMode_) {
                UpdateWindowBounds(false);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_DISPLAYCHANGE:
            UpdateWindowBounds(true);
            return 0;

        case WM_DPICHANGED:
            DiscardTextFormats();
            UpdateWindowBounds(true);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_SETCURSOR: {
            POINT screenPoint = {};
            GetCursorPos(&screenPoint);
            POINT clientPoint = screenPoint;
            ScreenToClient(hwnd_, &clientPoint);
            if (modelScoresSourceRect_.right > modelScoresSourceRect_.left
                && PtInRect(&modelScoresSourceRect_, clientPoint)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            switch (HitTestDragMode(clientPoint)) {
                case DragMode::ResizeRight:
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                case DragMode::ResizeBottom:
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                case DragMode::ResizeCorner:
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
                    return TRUE;
                case DragMode::Move:
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    return TRUE;
                case DragMode::None:
                    break;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd_, &screenPoint);
            POINT clientPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (TryHandleActionButtonClick(clientPoint)) {
                return 0;
            }
            BeginDrag(HitTestDragMode(clientPoint), screenPoint);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragMode_ != DragMode::None) {
                POINT screenPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd_, &screenPoint);
                UpdateDrag(screenPoint);
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (dragMode_ != DragMode::None) {
                EndDrag(true);
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (dragMode_ != DragMode::None) {
                EndDrag(true);
            }
            return 0;

        case WM_CONTEXTMENU:
            return 0;

        case kUsageUpdatedMessage:
            OnUsageUpdated(reinterpret_cast<UsageSnapshot*>(lParam));
            return 0;

        case kReleaseVersionUpdatedMessage:
            OnLatestReleaseChecked(reinterpret_cast<ReleaseVersionInfo*>(lParam));
            return 0;

        case kResetCreditConsumedMessage:
            OnResetCreditConsumed(reinterpret_cast<ConsumeResetCreditResult*>(lParam));
            return 0;

        case kTokenRefreshedMessage:
            OnTokenRefreshed(reinterpret_cast<TokenRefreshResult*>(lParam));
            return 0;

        case kTokenMaintenanceMessage:
            OnTokenMaintenance(reinterpret_cast<TokenMaintenanceReport*>(lParam));
            return 0;

        case kModelScoresUpdatedMessage:
            OnModelScoresUpdated(reinterpret_cast<ModelIqSnapshot*>(lParam));
            return 0;

        case kBrowserDoneMessage: {
            std::unique_ptr<BrowserSignInResult> result(reinterpret_cast<BrowserSignInResult*>(lParam));
            browserSignInInFlight_ = false;
            if (result != nullptr && result->success) {
                activeAuthId_ = result->accountId;
                SaveActiveAuth();
                RequestRefresh(true);
            } else {
                resetCreditActionMessage_ = result != nullptr ? result->error : L"browser login failed";
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }

        case kResetStatusMessage: {
            std::unique_ptr<ResetStatusInfo> result(reinterpret_cast<ResetStatusInfo*>(lParam));
            resetStatusInFlight_ = false;
            if (result != nullptr) {
                if (result->success) {
                    resetStatus_ = *result;
                } else if (!result->error.empty()) {
                    resetStatus_.error = result->error;
                }
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case kGrokUpdatedMessage: {
            std::unique_ptr<GrokSnapshot> result(reinterpret_cast<GrokSnapshot*>(lParam));
            grokInFlight_ = false;
            refreshInFlight_ = false;
            if (provider_ == L"grok" && result != nullptr) {
                grok_ = *result;
            }
            if (!taskbarMode_) {
                FitWindowToContent();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case kSummaryRowMessage:
            OnSummaryRowUpdated(static_cast<int>(wParam), reinterpret_cast<AccountQuotaRow*>(lParam));
            return 0;

        case kSummaryDoneMessage:
            OnSummaryRefreshDone(static_cast<int>(wParam));
            return 0;

        case kSessionScanMessage: {
            std::unique_ptr<SessionScan> result(reinterpret_cast<SessionScan*>(lParam));
            sessionScanInFlight_ = false;
            if (result != nullptr) {
                sessionScan_ = *result;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd_, kCountdownTimerId);
            KillTimer(hwnd_, kRefreshTimerId);
            KillTimer(hwnd_, kResetConfirmTimerId);
            KillTimer(hwnd_, kModelScoresTimerId);
            KillTimer(hwnd_, kResetStatusTimerId);
            KillTimer(hwnd_, kTokenMaintenanceTimerId);
            SaveSettings();
            SaveFeatureSettings();
            DiscardTextFormats();
            DiscardDeviceResources();
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void AppBarWindow::RegisterWindowClass() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);
}

RECT AppBarWindow::GetDesktopClientRect() const {
    RECT rect = {};
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect;
}

bool AppBarWindow::GetCurrentMonitorInfo(MONITORINFO& monitorInfo) const {
    monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);

    HMONITOR monitor = nullptr;
    if (hwnd_ != nullptr) {
        monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    }
    if (monitor == nullptr && hasSavedRect_) {
        POINT center = {
            savedRect_.left + RectWidth(savedRect_) / 2,
            savedRect_.top + RectHeight(savedRect_) / 2,
        };
        monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    }
    if (monitor == nullptr) {
        POINT origin = { 0, 0 };
        monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
    }

    return monitor != nullptr && GetMonitorInfoW(monitor, &monitorInfo) != FALSE;
}

RECT AppBarWindow::GetCurrentMonitorWorkRect() const {
    MONITORINFO monitorInfo = {};
    if (GetCurrentMonitorInfo(monitorInfo)) {
        return monitorInfo.rcWork;
    }
    return GetDesktopClientRect();
}

int AppBarWindow::GetMinimumWidgetWidth() const {
    if (taskbarMode_) {
        return ScaleForDpi(hwnd_, kTaskbarMinimumWidgetWidth);
    }
    if (surface_ == Surface::Summary) {
        const int summaryWidth = ScaleForDpi(hwnd_, 420);
        if (showModelScores_) {
            return std::max(summaryWidth, ScaleForDpi(hwnd_, 460));
        }
        return summaryWidth;
    }
    if (showModelScores_) {
        return ScaleForDpi(hwnd_, simpleMode_ ? 360 : 460);
    }
    return ScaleForDpi(hwnd_, simpleMode_ ? kSimpleMinimumWidgetWidth : kMinimumWidgetWidth);
}

int AppBarWindow::GetMinimumWidgetHeight(int width) const {
    if (taskbarMode_) {
        return CalculateTaskbarWidgetHeight(hwnd_);
    }
    const int chrome = ScaleForDpi(hwnd_, surface_ == Surface::Summary ? 44 : 78);
    const int dropExtra = surface_ != Surface::Summary && accountDropOpen_
        ? ScaleForDpi(hwnd_, 8 + 32 * std::max(1, static_cast<int>(accounts_.List(provider_).size())))
        : 0;
    if (surface_ == Surface::Summary) {
        return chrome + SummaryContentHeight() + GetModelScoresPanelHeight();
    }
    if (surface_ == Surface::Settings) {
        const int accountRows = static_cast<int>(accounts_.List(L"").size());
        return chrome + ScaleForDpi(hwnd_, 760 + accountRows * 30) + dropExtra;
    }
    if (surface_ == Surface::Accounts) {
        const int count = static_cast<int>(accounts_.List(provider_).size());
        return chrome + ScaleForDpi(hwnd_, 150 + count * 40);
    }
    int height = 0;
    if (simpleMode_) {
        height = CalculateSimpleMinimumWidgetHeight(hwnd_);
    } else {
        // Base = weekly-only layout; add a full limit-row block when 5h is present.
        height = CalculateDetailedMinimumWidgetHeight(hwnd_, width);
        if (snapshot_.fiveHour.available) {
            height += ScaleForDpi(hwnd_, 40);  // compact title/meta + bar + gap
        }
        // Grow only for additional reset-credit rows (one row already in base height).
        if (snapshot_.success && snapshot_.resetCredits.fetched) {
            const int extraRows = std::max(0, static_cast<int>(snapshot_.resetCredits.availableCredits.size()) - 1);
            height += extraRows * ScaleForDpi(hwnd_, 16);
        }
        height = std::max(height, ScaleForDpi(hwnd_, 164));
    }
    height += GetModelScoresPanelHeight();
    height += ExtraFeatureHeight();
    height += chrome + dropExtra;
    if (provider_ == L"grok") {
        height += ScaleForDpi(hwnd_, 96);
    }
    return height;
}

void AppBarWindow::SetLanguage(Language language) {
    if (language_ == language) {
        return;
    }

    language_ = language;
    SaveFeatureSettings();
    if (hwnd_ != nullptr) {
        SetWindowTextW(hwnd_, LocalizeText(L"Codex Usage Widget", L"Codex 用量挂件"));
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
    SaveSettings();
}

void AppBarWindow::SetRefreshIntervalSeconds(int seconds) {
    const int sanitized = SanitizeRefreshIntervalSeconds(seconds);
    if (refreshIntervalSeconds_ == sanitized) {
        return;
    }

    refreshIntervalSeconds_ = sanitized;
    refreshCountdownSeconds_ = refreshIntervalSeconds_;
    if (hwnd_ != nullptr) {
        RestartRefreshTimer();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    SaveSettings();
}

void AppBarWindow::RestartRefreshTimer() {
    if (hwnd_ == nullptr) {
        return;
    }

    KillTimer(hwnd_, kRefreshTimerId);
    SetTimer(hwnd_, kRefreshTimerId, static_cast<UINT>(refreshIntervalSeconds_ * 1000), nullptr);
}

void AppBarWindow::RestartModelScoresTimer() {
    if (hwnd_ == nullptr) {
        return;
    }

    KillTimer(hwnd_, kModelScoresTimerId);
    if (showModelScores_) {
        SetTimer(hwnd_, kModelScoresTimerId, static_cast<UINT>(kModelScoresRefreshIntervalSeconds * 1000), nullptr);
    }
}

int AppBarWindow::GetModelScoreFilterBandHeight(int innerWidth) const {
    if (innerWidth <= 0) {
        return ScaleForDpi(hwnd_, 18);
    }
    const auto chips = BuildModelScoreFilterChips(0, 0, innerWidth);
    if (chips.empty()) {
        return ScaleForDpi(hwnd_, 18);
    }
    int bottom = 0;
    for (const ModelScoreFilterChip& chip : chips) {
        bottom = std::max(bottom, static_cast<int>(chip.rect.bottom));
    }
    return std::max(ScaleForDpi(hwnd_, 18), bottom);
}

std::vector<AppBarWindow::ModelScoreFilterChip> AppBarWindow::BuildModelScoreFilterChips(
    int left, int top, int right) const {
    std::vector<ModelScoreFilterChip> chips;
    const int chipH = ScaleForDpi(hwnd_, 18);
    const int gap = ScaleForDpi(hwnd_, 4);
    const int padX = ScaleForDpi(hwnd_, 8);
    int x = left;
    int y = top;
    auto addChip = [&](const std::wstring& key, const std::wstring& label) {
        const int textW = std::max(ScaleForDpi(hwnd_, 12), static_cast<int>(label.size()) * ScaleForDpi(hwnd_, 7));
        const int chipW = textW + padX * 2;
        if (x > left && x + chipW > right) {
            x = left;
            y += chipH + gap;
        }
        ModelScoreFilterChip chip;
        chip.rect = MakeRect(x, y, std::min(right, x + chipW), y + chipH);
        chip.key = key;
        chip.label = label;
        chip.selected = key.empty() ? selectedModelFamilyKeys_.empty() : IsModelScoreFamilySelected(key);
        chips.push_back(chip);
        x += chipW + gap;
    };
    addChip({}, LocalizeText(L"All", L"全部"));
    for (const auto& family : ListModelScoreFamilies()) {
        addChip(family.first, family.second);
    }
    return chips;
}

int AppBarWindow::GetModelScoresPanelHeight() const {
    if (!showModelScores_ || taskbarMode_) {
        return 0;
    }

    RECT clientRect = {};
    GetClientRect(hwnd_, &clientRect);
    int innerWidth = RectWidth(clientRect) - ScaleForDpi(hwnd_, kHorizontalPadding * 2 + 24);
    if (innerWidth <= 0) {
        innerWidth = GetMinimumWidgetWidth() - ScaleForDpi(hwnd_, kHorizontalPadding * 2 + 24);
    }
    const int filterH = GetModelScoreFilterBandHeight(std::max(1, innerWidth));
    const int rows = GetModelScoresVisibleRowCount();
    // gap + box(padding + filter + header + rows + pager + attribution + padding)
    return ScaleForDpi(hwnd_, 6 + 4 + 16) + filterH + ScaleForDpi(hwnd_, 4 + rows * 16 + 18 + 14 + 4);
}

bool AppBarWindow::IsModelScoreFamilySelected(const std::wstring& familyKey) const {
    if (selectedModelFamilyKeys_.empty()) {
        return familyKey.empty();
    }
    return std::find(selectedModelFamilyKeys_.begin(), selectedModelFamilyKeys_.end(), familyKey)
        != selectedModelFamilyKeys_.end();
}

bool AppBarWindow::MatchesModelScoreFamily(const ModelIqScore& score) const {
    if (selectedModelFamilyKeys_.empty()) {
        return true;
    }
    return std::find(selectedModelFamilyKeys_.begin(), selectedModelFamilyKeys_.end(), score.familyKey)
        != selectedModelFamilyKeys_.end();
}

std::vector<std::pair<std::wstring, std::wstring>> AppBarWindow::ListModelScoreFamilies() const {
    std::vector<std::pair<std::wstring, std::wstring>> families;
    for (const ModelIqScore& score : modelScores_.scores) {
        if (score.familyKey.empty()) {
            continue;
        }
        bool exists = false;
        for (const auto& family : families) {
            if (family.first == score.familyKey) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            families.emplace_back(score.familyKey, score.familyLabel.empty() ? score.familyKey : score.familyLabel);
        }
    }
    std::sort(families.begin(), families.end(),
        [](const std::pair<std::wstring, std::wstring>& lhs, const std::pair<std::wstring, std::wstring>& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second < rhs.second;
            }
            return lhs.first < rhs.first;
        });
    return families;
}

int AppBarWindow::CountFilteredModelScores() const {
    if (!modelScores_.success) {
        return 0;
    }
    if (selectedModelFamilyKeys_.empty()) {
        return static_cast<int>(modelScores_.scores.size());
    }
    int count = 0;
    for (const ModelIqScore& score : modelScores_.scores) {
        if (MatchesModelScoreFamily(score)) {
            ++count;
        }
    }
    return count;
}

int AppBarWindow::GetModelScoresPageCount() const {
    const int count = CountFilteredModelScores();
    if (count <= 0) {
        return 1;
    }
    return (count + kModelScoresPageSize - 1) / kModelScoresPageSize;
}

int AppBarWindow::GetModelScoresVisibleRowCount() const {
    const int count = CountFilteredModelScores();
    if (count <= 0) {
        return 1;
    }
    const int remaining = count - modelScoresPage_ * kModelScoresPageSize;
    return std::max(1, std::min(kModelScoresPageSize, remaining));
}

void AppBarWindow::ClampModelScoresPage() {
    const int pages = GetModelScoresPageCount();
    if (modelScoresPage_ >= pages) {
        modelScoresPage_ = pages - 1;
    }
    if (modelScoresPage_ < 0) {
        modelScoresPage_ = 0;
    }
}

void AppBarWindow::ToggleModelScoreFamily(const std::wstring& familyKey) {
    if (familyKey.empty()) {
        if (selectedModelFamilyKeys_.empty()) {
            return;
        }
        selectedModelFamilyKeys_.clear();
    } else {
        auto it = std::find(selectedModelFamilyKeys_.begin(), selectedModelFamilyKeys_.end(), familyKey);
        if (selectedModelFamilyKeys_.empty()) {
            selectedModelFamilyKeys_.push_back(familyKey);
        } else if (it == selectedModelFamilyKeys_.end()) {
            selectedModelFamilyKeys_.push_back(familyKey);
        } else {
            selectedModelFamilyKeys_.erase(it);
        }
    }
    modelScoresPage_ = 0;
    SaveSettings();
    if (!taskbarMode_ && showModelScores_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::SetModelScoresPage(int page) {
    modelScoresPage_ = page;
    ClampModelScoresPage();
    if (!taskbarMode_ && showModelScores_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

const wchar_t* AppBarWindow::LocalizeText(const wchar_t* english, const wchar_t* chinese) const {
    return language_ == Language::Chinese ? chinese : english;
}

std::wstring AppBarWindow::GetVersionStatusText(bool compact) const {
    if (updateAvailable_ && !latestReleaseTag_.empty()) {
        if (compact) {
            return language_ == Language::Chinese
                ? (std::wstring(kCurrentVersion) + L" -> " + latestReleaseTag_)
                : (std::wstring(kCurrentVersion) + L" -> " + latestReleaseTag_);
        }
        return language_ == Language::Chinese
            ? (L"版本: " + std::wstring(kCurrentVersion) + L"，可更新到 " + latestReleaseTag_)
            : (L"Version: " + std::wstring(kCurrentVersion) + L", update available: " + latestReleaseTag_);
    }

    if (compact) {
        return kCurrentVersion;
    }
    return language_ == Language::Chinese
        ? (L"版本: " + std::wstring(kCurrentVersion))
        : (L"Version: " + std::wstring(kCurrentVersion));
}

RECT AppBarWindow::BuildDefaultRect(const RECT& desktopRect) const {
    if (taskbarMode_) {
        return BuildTaskbarDockRect();
    }

    // Default / reset position: top-right of the current desktop work area.
    const int margin = ScaleForDpi(hwnd_, kDesktopMargin);
    const int width = ScaleForDpi(hwnd_, simpleMode_ ? kSimpleDefaultWidgetWidth : kDefaultWidgetWidth);
    const int height = GetMinimumWidgetHeight(width);
    RECT rect = {};
    rect.right = std::max(width + margin, static_cast<int>(desktopRect.right) - margin);
    rect.left = std::max(static_cast<int>(desktopRect.left) + margin, static_cast<int>(rect.right) - width);
    rect.top = static_cast<int>(desktopRect.top) + margin;
    if (rect.top + height > static_cast<int>(desktopRect.bottom) - margin) {
        rect.top = std::max(static_cast<int>(desktopRect.top) + margin,
            static_cast<int>(desktopRect.bottom) - margin - height);
    }
    rect.bottom = rect.top + height;
    return rect;
}

RECT AppBarWindow::BuildTaskbarDockRect() const {
    MONITORINFO monitorInfo = {};
    RECT monitorRect = GetDesktopClientRect();
    RECT workRect = monitorRect;
    if (GetCurrentMonitorInfo(monitorInfo)) {
        monitorRect = monitorInfo.rcMonitor;
        workRect = monitorInfo.rcWork;
    }

    const int margin = ScaleForDpi(hwnd_, 4);
    const int width = ScaleForDpi(hwnd_, kTaskbarDefaultWidgetWidth);
    const int height = CalculateTaskbarWidgetHeight(hwnd_);
    const int leftGap = std::max(0, static_cast<int>(workRect.left - monitorRect.left));
    const int topGap = std::max(0, static_cast<int>(workRect.top - monitorRect.top));
    const int rightGap = std::max(0, static_cast<int>(monitorRect.right - workRect.right));
    const int bottomGap = std::max(0, static_cast<int>(monitorRect.bottom - workRect.bottom));
    const int largestGap = std::max({ leftGap, topGap, rightGap, bottomGap });

    RECT rect = {};
    if (leftGap == largestGap && leftGap > 0) {
        rect.left = workRect.left + margin;
        rect.top = workRect.bottom - height - margin;
    } else if (rightGap == largestGap && rightGap > 0) {
        rect.left = workRect.right - width - margin;
        rect.top = workRect.bottom - height - margin;
    } else if (topGap == largestGap && topGap > 0) {
        rect.left = workRect.right - width - margin;
        rect.top = workRect.top + margin;
    } else {
        rect.left = workRect.right - width - margin;
        rect.top = workRect.bottom - height - margin;
    }

    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    return rect;
}

RECT AppBarWindow::ClampRectToDesktop(RECT rect) const {
    const RECT desktopRect = taskbarMode_ ? GetCurrentMonitorWorkRect() : GetDesktopClientRect();
    const int minWidth = GetMinimumWidgetWidth();
    const int minHeight = GetMinimumWidgetHeight(std::max(RectWidth(rect), minWidth));

    if (RectWidth(rect) < minWidth) {
        rect.right = rect.left + minWidth;
    }
    if (RectHeight(rect) < minHeight) {
        rect.bottom = rect.top + minHeight;
    }

    if (rect.left < desktopRect.left) {
        const int width = RectWidth(rect);
        rect.left = desktopRect.left;
        rect.right = rect.left + width;
    }
    if (rect.top < desktopRect.top) {
        const int height = RectHeight(rect);
        rect.top = desktopRect.top;
        rect.bottom = rect.top + height;
    }
    if (rect.right > desktopRect.right) {
        const int width = RectWidth(rect);
        rect.right = desktopRect.right;
        rect.left = rect.right - width;
    }
    if (rect.bottom > desktopRect.bottom) {
        const int height = RectHeight(rect);
        rect.bottom = desktopRect.bottom;
        rect.top = rect.bottom - height;
    }

    rect.left = std::max(rect.left, desktopRect.left);
    rect.top = std::max(rect.top, desktopRect.top);
    rect.right = std::min(rect.right, desktopRect.right);
    rect.bottom = std::min(rect.bottom, desktopRect.bottom);
    return rect;
}

void AppBarWindow::UpdateWindowBounds(bool useSavedPosition) {
    const bool usingPersistedRect = useSavedPosition && hasSavedRect_ && !taskbarMode_;
    RECT rect = usingPersistedRect ? savedRect_ : BuildDefaultRect(GetDesktopClientRect());
    if (!taskbarMode_) {
        // Always fit height to current content (weekly-only vs 5h+weekly).
        const int minWidth = GetMinimumWidgetWidth();
        const int width = std::max(RectWidth(rect), minWidth);
        const int height = GetMinimumWidgetHeight(width);
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
    }
    rect = ClampRectToDesktop(rect);
    if (!taskbarMode_) {
        const int height = GetMinimumWidgetHeight(std::max(RectWidth(rect), GetMinimumWidgetWidth()));
        if (RectHeight(rect) < height) {
            rect.top = rect.bottom - height;
            rect = ClampRectToDesktop(rect);
        } else if (RectHeight(rect) > height) {
            rect.bottom = rect.top + height;
            rect = ClampRectToDesktop(rect);
        }
    }
    savedRect_ = rect;
    hasSavedRect_ = true;
    MoveWindow(hwnd_, rect.left, rect.top, RectWidth(rect), RectHeight(rect), TRUE);
    SetWindowPos(hwnd_, (alwaysOnTop_ || taskbarMode_) ? HWND_TOPMOST : HWND_NOTOPMOST,
        rect.left, rect.top, RectWidth(rect), RectHeight(rect), SWP_NOACTIVATE);
    SaveSettings();
}

void AppBarWindow::FitWindowToContent() {
    UpdateWindowBounds(true);
}

void AppBarWindow::SetDisplayMode(bool simpleMode, bool taskbarMode) {
    const bool normalizedSimpleMode = simpleMode && !taskbarMode;
    if (simpleMode_ == normalizedSimpleMode && taskbarMode_ == taskbarMode) {
        return;
    }

    simpleMode_ = normalizedSimpleMode;
    taskbarMode_ = taskbarMode;
    UpdateWindowBounds(false);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void AppBarWindow::LoadSettings() {
    const std::wstring path = GetSettingsPath();
    wchar_t activeAuth[1024] = {};
    GetPrivateProfileStringW(L"account", L"active_auth", L"", activeAuth, 1024, path.c_str());
    activeAuthId_ = activeAuth;
    while (!activeAuthId_.empty()
        && (activeAuthId_.front() == L' ' || activeAuthId_.front() == L'\t')) {
        activeAuthId_.erase(activeAuthId_.begin());
    }
    while (!activeAuthId_.empty()
        && (activeAuthId_.back() == L' ' || activeAuthId_.back() == L'\t'
            || activeAuthId_.back() == L'\r' || activeAuthId_.back() == L'\n')) {
        activeAuthId_.pop_back();
    }
    for (wchar_t& ch : activeAuthId_) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    const int version = GetPrivateProfileIntW(L"layout", L"layout_version", 0, path.c_str());
    alwaysOnTop_ = GetPrivateProfileIntW(L"layout", L"always_on_top", 0, path.c_str()) != 0;
    lockPosition_ = GetPrivateProfileIntW(L"layout", L"lock_position", 0, path.c_str()) != 0;
    simpleMode_ = GetPrivateProfileIntW(L"layout", L"simple_mode", 0, path.c_str()) != 0;
    taskbarMode_ = GetPrivateProfileIntW(L"layout", L"taskbar_mode", 0, path.c_str()) != 0;
    showModelScores_ = GetPrivateProfileIntW(L"layout", L"show_model_scores", 0, path.c_str()) != 0;
    modelScoreKind_ = GetPrivateProfileIntW(L"layout", L"model_score_kind", 0, path.c_str()) == 1
        ? RadarMetricKind::VisualSpatial
        : RadarMetricKind::SoftwareEngineering;
    wchar_t familyList[1024] = {};
    GetPrivateProfileStringW(L"layout", L"model_score_families", L"", familyList, 1024, path.c_str());
    selectedModelFamilyKeys_.clear();
    const std::wstring familyText = familyList;
    size_t start = 0;
    while (start <= familyText.size()) {
        const size_t comma = std::min(familyText.find(L',', start), familyText.size());
        std::wstring key = familyText.substr(start, comma - start);
        while (!key.empty() && (key.front() == L' ' || key.front() == L'\t')) {
            key.erase(key.begin());
        }
        while (!key.empty() && (key.back() == L' ' || key.back() == L'\t')) {
            key.pop_back();
        }
        if (!key.empty()) {
            selectedModelFamilyKeys_.push_back(key);
        }
        if (comma == familyText.size()) {
            break;
        }
        start = comma + 1;
    }
    if (taskbarMode_) {
        simpleMode_ = false;
    }
    refreshIntervalSeconds_ = SanitizeRefreshIntervalSeconds(
        GetPrivateProfileIntW(L"layout", L"refresh_interval_seconds", 60, path.c_str()));
    refreshCountdownSeconds_ = refreshIntervalSeconds_;
    const int languageValue = GetPrivateProfileIntW(L"layout", L"language", 0, path.c_str());
    language_ = languageValue == 1 ? Language::Chinese : Language::English;
    if (version < kLayoutVersion) {
        hasSavedRect_ = false;
        return;
    }

    const int width = GetPrivateProfileIntW(L"layout", L"width", 0, path.c_str());
    const int height = GetPrivateProfileIntW(L"layout", L"height", 0, path.c_str());
    if (width <= 0 || height <= 0) {
        hasSavedRect_ = false;
        return;
    }

    savedRect_.left = GetPrivateProfileIntW(L"layout", L"x", 0, path.c_str());
    savedRect_.top = GetPrivateProfileIntW(L"layout", L"y", 0, path.c_str());
    savedRect_.right = savedRect_.left + width;
    savedRect_.bottom = savedRect_.top + height;
    savedRect_ = ClampRectToDesktop(savedRect_);
    hasSavedRect_ = true;
}

void AppBarWindow::SaveSettings() const {
    if (!hasSavedRect_) {
        return;
    }

    const std::wstring path = GetSettingsPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    WritePrivateProfileStringW(L"layout", L"layout_version", std::to_wstring(kLayoutVersion).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"always_on_top", alwaysOnTop_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"lock_position", lockPosition_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"simple_mode", simpleMode_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"taskbar_mode", taskbarMode_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"show_model_scores", showModelScores_ ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"layout", L"model_score_kind",
        modelScoreKind_ == RadarMetricKind::VisualSpatial ? L"1" : L"0", path.c_str());
    std::wstring familyList;
    for (const std::wstring& key : selectedModelFamilyKeys_) {
        if (!familyList.empty()) {
            familyList += L',';
        }
        familyList += key;
    }
    WritePrivateProfileStringW(L"layout", L"model_score_families", familyList.c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"refresh_interval_seconds", std::to_wstring(refreshIntervalSeconds_).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"language", std::to_wstring(static_cast<int>(language_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"x", std::to_wstring(savedRect_.left).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"y", std::to_wstring(savedRect_.top).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"width", std::to_wstring(RectWidth(savedRect_)).c_str(), path.c_str());
    WritePrivateProfileStringW(L"layout", L"height", std::to_wstring(RectHeight(savedRect_)).c_str(), path.c_str());
}

void AppBarWindow::SaveActiveAuth() const {
    const std::wstring path = GetSettingsPath();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::wstring stored = activeAuthId_;
    for (wchar_t& ch : stored) {
        if (ch == L'\\') {
            ch = L'/';
        }
    }
    WritePrivateProfileStringW(L"account", L"active_auth", stored.c_str(), path.c_str());
}

std::wstring AppBarWindow::ActiveAuthPath() const {
    const std::vector<AccountEntry> accounts = accounts_.List(L"");
    if (!activeAuthId_.empty()) {
        for (const AccountEntry& account : accounts) {
            if (_wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0) {
                return account.path;
            }
        }
    }
    for (const AccountEntry& account : accounts) {
        if (account.provider == provider_) {
            return account.path;
        }
    }
    return {};
}

bool AppBarWindow::IsActiveAuth(const AccountEntry& account) const {
    if (authMenuAccounts_.empty()) {
        return false;
    }
    const AccountEntry* selected = &authMenuAccounts_.front();
    if (!activeAuthId_.empty()) {
        for (const AccountEntry& item : authMenuAccounts_) {
            if (_wcsicmp(item.id.c_str(), activeAuthId_.c_str()) == 0) {
                selected = &item;
                break;
            }
        }
    }
    return _wcsicmp(account.id.c_str(), selected->id.c_str()) == 0;
}

std::wstring AppBarWindow::GetSettingsPath() const {
    PWSTR appDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath))) {
        const std::filesystem::path path = std::filesystem::path(appDataPath) / L"CodexUsageBar" / L"settings.ini";
        CoTaskMemFree(appDataPath);
        return path.wstring();
    }

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(instance_, modulePath, MAX_PATH);
    return (std::filesystem::path(modulePath).parent_path() / L"settings.ini").wstring();
}

std::wstring AppBarWindow::GetExecutablePath() const {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(instance_, modulePath, MAX_PATH);
    return modulePath;
}

void AppBarWindow::RefreshTheme() {
    lightTheme_ = IsDesktopLightTheme();
}

bool AppBarWindow::IsDesktopLightTheme() const {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LONG status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    return value != 0;
}

bool AppBarWindow::IsLaunchAtStartupEnabled() const {
    wchar_t value[2048] = {};
    DWORD size = sizeof(value);
    const LONG status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"CodexUsageBar",
        RRF_RT_REG_SZ,
        nullptr,
        value,
        &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring expected = L"\"" + GetExecutablePath() + L"\"";
    return std::wstring(value) == expected;
}

bool AppBarWindow::SetLaunchAtStartupEnabled(bool enabled) const {
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"CodexUsageBar";

    if (enabled) {
        const std::wstring command = L"\"" + GetExecutablePath() + L"\"";
        const LONG status = RegSetKeyValueW(
            HKEY_CURRENT_USER,
            subkey,
            valueName,
            REG_SZ,
            command.c_str(),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        return status == ERROR_SUCCESS;
    }

    const LONG status = RegDeleteKeyValueW(HKEY_CURRENT_USER, subkey, valueName);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

HRESULT AppBarWindow::CreateDeviceIndependentResources() {
    if (!d2dFactory_) {
        const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!dwriteFactory_) {
        const HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT AppBarWindow::CreateTextFormat(float sizePixels, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) {
    return dwriteFactory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        sizePixels,
        L"zh-CN",
        format);
}

void AppBarWindow::DiscardTextFormats() {
    textFormatKicker_.Reset();
    textFormatTitle_.Reset();
    textFormatDelta_.Reset();
    textFormatMetricLabel_.Reset();
    textFormatMetricValue_.Reset();
    textFormatFoot_.Reset();
    textFormatDpi_ = 0;
}

HRESULT AppBarWindow::EnsureTextFormats() {
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (textFormatDpi_ == dpi &&
        textFormatKicker_ &&
        textFormatTitle_ &&
        textFormatDelta_ &&
        textFormatMetricLabel_ &&
        textFormatMetricValue_ &&
        textFormatFoot_) {
        return S_OK;
    }

    DiscardTextFormats();

    HRESULT hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 12)), DWRITE_FONT_WEIGHT_NORMAL, textFormatKicker_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 18)), DWRITE_FONT_WEIGHT_SEMI_BOLD, textFormatTitle_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 28)), DWRITE_FONT_WEIGHT_BOLD, textFormatDelta_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 12)), DWRITE_FONT_WEIGHT_SEMI_BOLD, textFormatMetricLabel_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 17)), DWRITE_FONT_WEIGHT_BOLD, textFormatMetricValue_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = CreateTextFormat(static_cast<float>(ScaleForDpi(hwnd_, 12)), DWRITE_FONT_WEIGHT_NORMAL, textFormatFoot_.GetAddressOf());
    if (FAILED(hr)) return hr;

    textFormatDpi_ = dpi;
    return S_OK;
}

HRESULT AppBarWindow::CreateDeviceResources() {
    if (FAILED(CreateDeviceIndependentResources())) {
        return E_FAIL;
    }

    if (!renderTarget_) {
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        HRESULT hr = d2dFactory_->CreateDCRenderTarget(&properties, renderTarget_.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }

        hr = renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0, 0.0f), solidBrush_.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }
        EnsureBrandIcons();
    }

    return EnsureTextFormats();
}

HRESULT AppBarWindow::EnsureBrandIcons() {
    if (codexIcon_ && grokIcon_) {
        return S_OK;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitCom = com == S_OK;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    auto load = [&](const unsigned char* bytes, size_t size, ID2D1Bitmap** bitmap) -> HRESULT {
        Microsoft::WRL::ComPtr<IWICStream> stream;
        HRESULT status = factory->CreateStream(stream.GetAddressOf());
        if (FAILED(status)) {
            return status;
        }
        status = stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(size));
        if (FAILED(status)) {
            return status;
        }
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        status = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        if (FAILED(status)) {
            return status;
        }
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        status = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(status)) {
            return status;
        }
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        status = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(status)) {
            return status;
        }
        status = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(status)) {
            return status;
        }
        return renderTarget_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, bitmap);
    };
    if (SUCCEEDED(hr)) {
        if (!codexIcon_) {
            load(kCodexIconPng, kCodexIconPngSize, codexIcon_.GetAddressOf());
        }
        if (!grokIcon_) {
            load(kGrokIconPng, kGrokIconPngSize, grokIcon_.GetAddressOf());
        }
    }
    if (uninitCom) {
        CoUninitialize();
    }
    return hr;
}

void AppBarWindow::DiscardDeviceResources() {
    codexIcon_.Reset();
    grokIcon_.Reset();
    solidBrush_.Reset();
    renderTarget_.Reset();
}

AppBarWindow::DragMode AppBarWindow::HitTestDragMode(POINT clientPoint) const {
    if (lockPosition_) {
        return DragMode::None;
    }
    if (taskbarMode_) {
        return DragMode::None;
    }

    RECT clientRect = {};
    GetClientRect(hwnd_, &clientRect);
    const int grip = ScaleForDpi(hwnd_, kResizeGrip);
    const bool nearRight = clientPoint.x >= clientRect.right - grip;
    const bool nearBottom = clientPoint.y >= clientRect.bottom - grip;
    if (nearRight && nearBottom) {
        return DragMode::ResizeCorner;
    }
    if (nearRight) {
        return DragMode::ResizeRight;
    }
    if (nearBottom) {
        return DragMode::ResizeBottom;
    }
    return DragMode::Move;
}

void AppBarWindow::BeginDrag(DragMode mode, POINT screenPoint) {
    if (mode == DragMode::None) {
        return;
    }

    dragMode_ = mode;
    dragStartPoint_ = screenPoint;
    dragStartRect_ = savedRect_;
    SetCapture(hwnd_);
}

void AppBarWindow::UpdateDrag(POINT screenPoint) {
    if (dragMode_ == DragMode::None) {
        return;
    }

    RECT rect = dragStartRect_;
    const int deltaX = screenPoint.x - dragStartPoint_.x;
    const int deltaY = screenPoint.y - dragStartPoint_.y;

    switch (dragMode_) {
        case DragMode::Move:
            OffsetRect(&rect, deltaX, deltaY);
            break;
        case DragMode::ResizeRight:
            rect.right += deltaX;
            break;
        case DragMode::ResizeBottom:
            rect.bottom += deltaY;
            break;
        case DragMode::ResizeCorner:
            rect.right += deltaX;
            rect.bottom += deltaY;
            break;
        case DragMode::None:
            break;
    }

    savedRect_ = ClampRectToDesktop(rect);
    MoveWindow(hwnd_, savedRect_.left, savedRect_.top, RectWidth(savedRect_), RectHeight(savedRect_), TRUE);
}

void AppBarWindow::EndDrag(bool saveSettings) {
    ReleaseCapture();
    dragMode_ = DragMode::None;
    if (saveSettings) {
        SaveSettings();
    }
}

void AppBarWindow::RequestRefresh(bool force) {
    bool expected = false;
    if (!force && !refreshInFlight_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (force && refreshInFlight_.exchange(true)) {
        return;
    }

    refreshCountdownSeconds_ = refreshIntervalSeconds_;
    RestartRefreshTimer();
    if (surface_ == Surface::Summary) {
        refreshInFlight_ = false;
        RequestSummaryRefresh();
        return;
    }
    if (provider_ == L"grok") {
        refreshInFlight_ = false;
        RequestGrokRefresh();
        return;
    }

    const HWND target = hwnd_;
    const std::wstring authPath = ActiveAuthPath();
    inflightAuthId_ = activeAuthId_;
    std::thread([this, target, authPath]() {
        auto* result = new UsageSnapshot(fetcher_.Fetch(authPath));
        PostMessageW(target, kUsageUpdatedMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::OnUsageUpdated(UsageSnapshot* snapshot) {
    std::unique_ptr<UsageSnapshot> holder(snapshot);
    refreshInFlight_ = false;
    if (inflightAuthId_ != activeAuthId_) {
        RequestRefresh(true);
        return;
    }
    if (snapshot != nullptr && snapshot->success && estimateEnabled_ && snapshot->weekly.available) {
        estimate_ = RecordWeeklyEstimate(
            std::filesystem::path(GetSettingsPath()).parent_path() / L"quota-estimate-history.json",
            snapshot->weekly.usedPercent,
            false,
            0,
            true);
    }
    if (snapshot != nullptr) {
        snapshot_ = *snapshot;
        if (snapshot_.success) {
            lastSuccessfulRefreshUnixSeconds_ = static_cast<long long>(std::time(nullptr));
        }
    }
    // Limit-lane count (5h present/absent) and credit rows change preferred height.
    if (!taskbarMode_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int AppBarWindow::SummaryContentHeight() const {
    const int count = static_cast<int>(accounts_.List(L"").size());
    int height = 10;
    height += 24;
    height += std::max(1, count) * 22;
    height += 22;
    return ScaleForDpi(hwnd_, height);
}

void AppBarWindow::SeedSummaryRows() {
    const std::vector<AccountEntry> accounts = accounts_.List(L"");
    std::vector<AccountQuotaRow> next;
    next.reserve(accounts.size());
    for (const AccountEntry& account : accounts) {
        AccountQuotaRow row;
        row.id = account.id;
        row.label = account.label;
        row.alias = account.alias;
        row.email = account.email;
        row.provider = account.provider.empty() ? L"codex" : account.provider;
        row.loading = true;
        for (const AccountQuotaRow& previous : summaryRows_) {
            if (_wcsicmp(previous.id.c_str(), account.id.c_str()) == 0 && !previous.loading) {
                row = previous;
                row.label = account.label;
                row.alias = account.alias;
                row.email = account.email;
                row.provider = account.provider.empty() ? L"codex" : account.provider;
                break;
            }
        }
        const bool active = !activeAuthId_.empty()
            && _wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0;
        if (row.loading && active && _wcsicmp(row.provider.c_str(), L"grok") == 0 && grok_.success) {
            row.loading = false;
            row.success = true;
            const int remaining = GrokWeeklyRemainingPercent(grok_);
            if (remaining >= 0) {
                row.hasQuota = true;
                row.remainingPercent = remaining;
            }
            row.resetIso = grok_.periodEnd;
        } else if (row.loading && active && _wcsicmp(row.provider.c_str(), L"codex") == 0
            && snapshot_.success && snapshot_.weekly.available) {
            row.loading = false;
            row.success = true;
            row.hasQuota = true;
            row.remainingPercent = snapshot_.weekly.remainingPercent;
            row.resetAtUnixSeconds = snapshot_.weekly.resetAtUnixSeconds;
        }
        next.push_back(std::move(row));
    }
    summaryRows_ = std::move(next);
}

void AppBarWindow::RequestSummaryRefresh() {
    SeedSummaryRows();
    if (hwnd_ != nullptr && !taskbarMode_) {
        FitWindowToContent();
    }
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (summaryInFlight_.exchange(true)) {
        return;
    }
    const int generation = ++summaryGeneration_;
    const HWND target = hwnd_;
    const std::vector<AccountEntry> accounts = accounts_.List(L"");
    std::thread([target, generation, accounts]() {
        CodexUsageFetcher fetcher;
        for (const AccountEntry& account : accounts) {
            auto* row = new AccountQuotaRow();
            row->id = account.id;
            row->label = account.label;
            row->alias = account.alias;
            row->email = account.email;
            row->provider = account.provider.empty() ? L"codex" : account.provider;
            row->loading = false;
            if (_wcsicmp(row->provider.c_str(), L"grok") == 0) {
                const GrokSnapshot snap = FetchGrokBillingFile(account.path);
                row->success = snap.success;
                row->error = snap.error;
                const int remaining = GrokWeeklyRemainingPercent(snap);
                if (remaining >= 0) {
                    row->hasQuota = true;
                    row->remainingPercent = remaining;
                }
                row->resetIso = snap.periodEnd;
            } else {
                const UsageSnapshot snap = fetcher.Fetch(account.path);
                row->success = snap.success;
                row->error = snap.errorMessage;
                if (snap.weekly.available) {
                    row->hasQuota = true;
                    row->remainingPercent = snap.weekly.remainingPercent;
                    row->resetAtUnixSeconds = snap.weekly.resetAtUnixSeconds;
                }
            }
            if (target == nullptr || !PostMessageW(
                    target,
                    kSummaryRowMessage,
                    static_cast<WPARAM>(generation),
                    reinterpret_cast<LPARAM>(row))) {
                delete row;
                break;
            }
        }
        if (target != nullptr) {
            PostMessageW(target, kSummaryDoneMessage, static_cast<WPARAM>(generation), 0);
        }
    }).detach();
}

void AppBarWindow::OnSummaryRowUpdated(int generation, AccountQuotaRow* row) {
    std::unique_ptr<AccountQuotaRow> holder(row);
    if (holder == nullptr || generation != summaryGeneration_) {
        return;
    }
    for (AccountQuotaRow& existing : summaryRows_) {
        if (_wcsicmp(existing.id.c_str(), holder->id.c_str()) == 0) {
            existing = *holder;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
    summaryRows_.push_back(*holder);
    if (!taskbarMode_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::OnSummaryRefreshDone(int generation) {
    if (generation != summaryGeneration_) {
        return;
    }
    summaryInFlight_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::RequestLatestReleaseCheck(bool force) {
    bool expected = false;
    if (!force && !releaseCheckInFlight_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (force && releaseCheckInFlight_.exchange(true)) {
        return;
    }

    releaseCheckCountdownSeconds_ = kReleaseCheckIntervalSeconds;

    const HWND target = hwnd_;
    std::thread([this, target]() {
        auto* result = new ReleaseVersionInfo(fetcher_.FetchLatestRelease());
        PostMessageW(target, kReleaseVersionUpdatedMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::OnLatestReleaseChecked(ReleaseVersionInfo* info) {
    std::unique_ptr<ReleaseVersionInfo> holder(info);
    releaseCheckInFlight_ = false;
    lastReleaseCheckUnixSeconds_ = static_cast<long long>(std::time(nullptr));

    if (info != nullptr) {
        hasReleaseCheckResult_ = info->success;
        releaseCheckErrorMessage_ = info->errorMessage;
        if (info->success) {
            latestReleaseTag_ = info->latestTag;
            updateAvailable_ = CompareVersions(kCurrentVersion, latestReleaseTag_) < 0;
        } else {
            latestReleaseTag_.clear();
            updateAvailable_ = false;
        }
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring AppBarWindow::CreateRedeemRequestId() const {
    static thread_local std::mt19937_64 rng{ std::random_device{}() };
    std::uniform_int_distribution<uint32_t> dist32(0, 0xFFFFFFFFu);
    std::uniform_int_distribution<uint16_t> dist16(0, 0xFFFFu);

    const uint32_t a = dist32(rng);
    const uint16_t b = dist16(rng);
    const uint16_t c = static_cast<uint16_t>((dist16(rng) & 0x0FFFu) | 0x4000u);  // UUID version 4
    const uint16_t d = static_cast<uint16_t>((dist16(rng) & 0x3FFFu) | 0x8000u);  // RFC 4122 variant
    const uint16_t e1 = dist16(rng);
    const uint32_t e2 = dist32(rng);

    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%08x-%04x-%04x-%04x-%04x%08x", a, b, c, d, e1, e2);
    return buffer;
}

std::wstring AppBarWindow::BuildResetCreditsSummaryText() const {
    if (!snapshot_.success) {
        return LocalizeText(L"Reset credits: --", L"重置卡: --");
    }
    if (!snapshot_.resetCredits.fetched) {
        if (!snapshot_.resetCredits.errorMessage.empty()) {
            return LocalizeText(L"Reset credits: unavailable", L"重置卡: 不可用");
        }
        return LocalizeText(L"Reset credits: --", L"重置卡: --");
    }
    return std::wstring(LocalizeText(L"Reset credits: ", L"重置卡: "))
        + std::to_wstring(snapshot_.resetCredits.availableCount)
        + LocalizeText(L" available", L" 张可用");
}

std::wstring AppBarWindow::BuildResetCreditsExpiryText() const {
    if (!snapshot_.success || !snapshot_.resetCredits.fetched) {
        return LocalizeText(L"Expiry: --", L"过期: --");
    }
    if (snapshot_.resetCredits.availableCount <= 0) {
        return LocalizeText(L"Expiry: none", L"过期: 无");
    }
    if (!snapshot_.resetCredits.hasNextExpiry) {
        return LocalizeText(L"Next expiry: none", L"最近过期: 无期限");
    }
    return std::wstring(LocalizeText(L"Next expiry: ", L"最近过期: "))
        + FormatDateTime(snapshot_.resetCredits.nextExpiresAtUnixSeconds);
}

bool AppBarWindow::TryHandleActionButtonClick(POINT clientPoint) {
    if (!taskbarMode_) {
        for (const UiHit& hit : uiHits_) {
            if (hit.rect.right <= hit.rect.left || !PtInRect(&hit.rect, clientPoint)) {
                continue;
            }
            if (hit.command == kCommandDeleteAccount && !hit.accountId.empty()) {
                DeleteAccountById(hit.accountId);
            } else if (hit.command == kCommandAlias && !hit.accountId.empty()) {
                RenameAccountById(hit.accountId);
            } else if (!hit.accountId.empty()) {
                for (const AccountEntry& account : accounts_.List(L"")) {
                    if (_wcsicmp(account.id.c_str(), hit.accountId.c_str()) != 0) {
                        continue;
                    }
                    provider_ = account.provider.empty() ? L"codex" : account.provider;
                    activeAuthId_ = account.id;
                    surface_ = Surface::Usage;
                    accountDropOpen_ = false;
                    SaveActiveAuth();
                    SaveFeatureSettings();
                    snapshot_ = {};
                    grok_ = {};
                    RequestRefresh(true);
                    break;
                }
            } else if (hit.command == kUiSettings) {
                accountDropOpen_ = false;
                const bool fromSummary = surface_ == Surface::Summary;
                surface_ = surface_ == Surface::Settings ? Surface::Usage : Surface::Settings;
                if (fromSummary && surface_ == Surface::Usage) {
                    RequestRefresh(true);
                }
            } else if (hit.command == kUiSummary) {
                accountDropOpen_ = false;
                if (surface_ == Surface::Summary) {
                    surface_ = Surface::Usage;
                    RequestRefresh(true);
                } else {
                    surface_ = Surface::Summary;
                    RequestSummaryRefresh();
                }
            } else if (hit.command == kUiAccounts) {
                accountDropOpen_ = !accountDropOpen_;
            } else if (hit.command == kUiCloseSheet) {
                const bool fromSummary = surface_ == Surface::Summary;
                surface_ = Surface::Usage;
                if (fromSummary) {
                    RequestRefresh(true);
                }
            } else {
                if (hit.command == kCommandProviderCodex || hit.command == kCommandProviderGrok) {
                    surface_ = Surface::Usage;
                    accountDropOpen_ = false;
                }
                HandleMenuCommand(hit.command);
            }
            FitWindowToContent();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (accountDropOpen_) {
            accountDropOpen_ = false;
            FitWindowToContent();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
    }

    if (taskbarMode_) {
        return false;
    }

    if (showModelScores_) {
        for (const ModelScoreFilterChip& chip : modelScoreFilterChips_) {
            if (chip.rect.right > chip.rect.left && PtInRect(&chip.rect, clientPoint)) {
                ToggleModelScoreFamily(chip.key);
                return true;
            }
        }
        if (modelScoresPrevRect_.right > modelScoresPrevRect_.left
            && PtInRect(&modelScoresPrevRect_, clientPoint)) {
            if (modelScoresPage_ > 0) {
                SetModelScoresPage(modelScoresPage_ - 1);
            }
            return true;
        }
        if (modelScoresNextRect_.right > modelScoresNextRect_.left
            && PtInRect(&modelScoresNextRect_, clientPoint)) {
            if (modelScoresPage_ + 1 < GetModelScoresPageCount()) {
                SetModelScoresPage(modelScoresPage_ + 1);
            }
            return true;
        }
        if (modelScoresSourceRect_.right > modelScoresSourceRect_.left
            && PtInRect(&modelScoresSourceRect_, clientPoint)) {
            ShellExecuteW(hwnd_, L"open", L"https://codexradar.com/", nullptr, nullptr, SW_SHOWNORMAL);
            return true;
        }
    }

    if (resetLinkRect_.right > resetLinkRect_.left && PtInRect(&resetLinkRect_, clientPoint) && !resetStatus_.url.empty()) {
        ShellExecuteW(hwnd_, L"open", resetStatus_.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }

    if (refreshButtonRect_.right > refreshButtonRect_.left && PtInRect(&refreshButtonRect_, clientPoint)) {
        RequestRefresh(true);
        return true;
    }

    return false;
}

void AppBarWindow::ArmOrConsumeResetCredit() {
    if (taskbarMode_) {
        return;
    }
    if (!snapshot_.success || !snapshot_.resetCredits.fetched) {
        return;
    }
    if (snapshot_.resetCredits.availableCount <= 0 || resetCreditInFlight_) {
        return;
    }

    // Triple confirmation: arm twice from the context menu, then a final MessageBox.
    if (resetCreditConfirmStep_ < 2) {
        ++resetCreditConfirmStep_;
        resetCreditActionMessage_ = resetCreditConfirmStep_ == 1
            ? LocalizeText(
                L"Reset armed 1/2 — choose Reset credits again",
                L"已预备 1/2 — 请再次右键选择「重置额度…」")
            : LocalizeText(
                L"Reset armed 2/2 — choose Reset credits again for final confirm",
                L"已预备 2/2 — 请再次右键选择「重置额度…」以最终确认");
        KillTimer(hwnd_, kResetConfirmTimerId);
        SetTimer(hwnd_, kResetConfirmTimerId, 5000, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    KillTimer(hwnd_, kResetConfirmTimerId);
    resetCreditConfirmStep_ = 0;

    const int result = MessageBoxW(
        hwnd_,
        LocalizeText(
            L"Final confirmation:\nThis will spend 1 rate-limit reset credit and force-reset the 5-hour window.\n\nContinue?",
            L"最终确认：\n将消耗 1 张额度重置卡，并强制重置 5 小时窗口。\n\n确认继续？"),
        LocalizeText(L"Confirm use reset credit", L"确认使用重置卡"),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (result != IDYES) {
        resetCreditActionMessage_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    RequestConsumeResetCredit();
}

void AppBarWindow::RequestConsumeResetCredit() {
    if (resetCreditInFlight_.exchange(true)) {
        return;
    }

    resetCreditActionMessage_ = LocalizeText(L"Using reset credit...", L"正在使用重置卡...");
    InvalidateRect(hwnd_, nullptr, FALSE);

    const HWND target = hwnd_;
    const std::wstring redeemId = CreateRedeemRequestId();
    const std::wstring authPath = ActiveAuthPath();
    std::thread([this, target, redeemId, authPath]() {
        auto* result = new ConsumeResetCreditResult(
            fetcher_.ConsumeRateLimitResetCredit(redeemId, authPath));
        PostMessageW(target, kResetCreditConsumedMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::OnResetCreditConsumed(ConsumeResetCreditResult* result) {
    std::unique_ptr<ConsumeResetCreditResult> holder(result);
    resetCreditInFlight_ = false;
    resetCreditConfirmStep_ = 0;

    if (result != nullptr && result->success) {
        resetCreditActionMessage_ = LocalizeText(L"Reset credit used. Refreshing...", L"重置卡已使用，正在刷新...");
        RequestRefresh(true);
    } else {
        resetCreditActionMessage_ = result != nullptr && !result->errorMessage.empty()
            ? result->errorMessage
            : std::wstring(LocalizeText(L"Failed to use reset credit", L"使用重置卡失败"));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::ImportAccount() {
    wchar_t file[MAX_PATH] = {};
    const std::wstring initialDirectory = std::filesystem::path(GetExecutablePath()).parent_path().wstring();
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"Auth JSON\0*.json\0All files\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrTitle = LocalizeText(L"Choose an account to import", L"选择要导入的账号");
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    const AccountOpResult imported = accounts_.ImportFile(file, provider_);
    if (!imported.success) {
        resetCreditActionMessage_ = imported.error.empty()
            ? std::wstring(LocalizeText(L"Failed to import account", L"导入账号失败"))
            : imported.error;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    activeAuthId_ = imported.id;
    SaveActiveAuth();
    resetCreditConfirmStep_ = 0;
    KillTimer(hwnd_, kResetConfirmTimerId);
    resetCreditActionMessage_.clear();
    snapshot_ = {};
    snapshot_.errorMessage = LocalizeText(L"Account imported. Loading...", L"账号已导入，正在读取...");
    InvalidateRect(hwnd_, nullptr, FALSE);
    RequestRefresh(true);
}

void AppBarWindow::RequestTokenMaintenance() {
    if (tokenMaintenanceInFlight_.exchange(true)) {
        return;
    }
    const long long now = static_cast<long long>(std::time(nullptr));
    std::vector<std::wstring> skip;
    for (const auto& item : tokenRefreshNotBefore_) {
        if (item.second > now) {
            skip.push_back(item.first);
        }
    }
    const std::vector<AccountEntry> accounts = accounts_.List(L"");
    const HWND target = hwnd_;
    std::thread([target, accounts, skip]() {
        auto* report = new TokenMaintenanceReport();
        CodexUsageFetcher fetcher;
        auto cooling = [&](const std::wstring& id) {
            for (const std::wstring& item : skip) {
                if (_wcsicmp(item.c_str(), id.c_str()) == 0) {
                    return true;
                }
            }
            return false;
        };
        auto noteFailure = [&](const std::wstring& id, const std::wstring& error) {
            ++report->failed;
            if (report->error.empty()) {
                report->error = error;
            }
            const bool revoked = error.find(L"invalid_grant") != std::wstring::npos
                || error.find(L"revoked") != std::wstring::npos
                || error.find(L"Revoked") != std::wstring::npos;
            if (revoked) {
                report->revokedIds.push_back(id);
            } else {
                report->failedIds.push_back(id);
            }
        };
        for (const AccountEntry& account : accounts) {
            if (cooling(account.id)) {
                continue;
            }
            if (_wcsicmp(account.provider.c_str(), L"grok") == 0) {
                const GrokTokenRefreshResult refreshed = RefreshGrokAuthIfNeeded(account.path, kGrokRefreshLeadSeconds, false);
                if (!refreshed.attempted) {
                    continue;
                }
                if (refreshed.success) {
                    ++report->refreshed;
                } else {
                    noteFailure(account.id, refreshed.error);
                }
            } else {
                const TokenRefreshResult refreshed = fetcher.RefreshAuthIfNeeded(account.path);
                if (!refreshed.attempted) {
                    continue;
                }
                if (refreshed.success) {
                    ++report->refreshed;
                } else {
                    noteFailure(account.id, refreshed.errorMessage);
                }
            }
        }
        if (target == nullptr || !PostMessageW(target, kTokenMaintenanceMessage, 0, reinterpret_cast<LPARAM>(report))) {
            delete report;
        }
    }).detach();
}

void AppBarWindow::OnTokenMaintenance(TokenMaintenanceReport* report) {
    std::unique_ptr<TokenMaintenanceReport> holder(report);
    tokenMaintenanceInFlight_ = false;
    if (holder == nullptr) {
        return;
    }
    const long long now = static_cast<long long>(std::time(nullptr));
    auto pushUntil = [&](const std::wstring& id, long long until) {
        for (auto& item : tokenRefreshNotBefore_) {
            if (_wcsicmp(item.first.c_str(), id.c_str()) == 0) {
                item.second = until;
                return;
            }
        }
        tokenRefreshNotBefore_.emplace_back(id, until);
    };
    for (const std::wstring& id : holder->revokedIds) {
        pushUntil(id, now + 6LL * 60 * 60);
    }
    for (const std::wstring& id : holder->failedIds) {
        pushUntil(id, now + 5LL * 60);
    }
    if (holder->refreshed > 0) {
        resetCreditActionMessage_ = std::wstring(LocalizeText(L"Refreshed ", L"已自动刷新 "))
            + std::to_wstring(holder->refreshed)
            + LocalizeText(L" token(s)", L" 个 Token");
        RequestRefresh(true);
    } else if (holder->failed > 0 && !holder->error.empty()) {
        resetCreditActionMessage_ = holder->error;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::RequestRefreshToken() {
    if (tokenRefreshInFlight_.exchange(true)) {
        return;
    }

    resetCreditActionMessage_ = LocalizeText(L"Refreshing token...", L"正在刷新 Token...");
    InvalidateRect(hwnd_, nullptr, FALSE);

    const HWND target = hwnd_;
    const std::wstring authPath = ActiveAuthPath();
    const bool grok = provider_ == L"grok";
    std::thread([target, authPath, grok]() {
        auto* result = new TokenRefreshResult();
        if (grok) {
            const GrokTokenRefreshResult refreshed = RefreshGrokAuthIfNeeded(authPath, 0, true);
            result->attempted = refreshed.attempted;
            result->success = refreshed.success;
            result->wroteAuthFile = refreshed.success && refreshed.attempted;
            result->errorMessage = refreshed.error;
        } else {
            *result = CodexUsageFetcher().ForceRefreshAuthTokens(authPath);
        }
        if (!PostMessageW(target, kTokenRefreshedMessage, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void AppBarWindow::OnTokenRefreshed(TokenRefreshResult* result) {
    std::unique_ptr<TokenRefreshResult> holder(result);
    tokenRefreshInFlight_ = false;

    if (result != nullptr && result->success) {
        resetCreditActionMessage_ = result->wroteAuthFile
            ? LocalizeText(L"Token refreshed. Updating usage...", L"Token 已刷新，正在更新额度...")
            : LocalizeText(L"Token refreshed (auth.json not written). Updating usage...",
                L"Token 已刷新（未写回 auth.json），正在更新额度...");
        InvalidateRect(hwnd_, nullptr, FALSE);
        RequestRefresh(true);
        return;
    }

    resetCreditActionMessage_ = result != nullptr && !result->errorMessage.empty()
        ? result->errorMessage
        : std::wstring(LocalizeText(L"Failed to refresh token", L"刷新 Token 失败"));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::RequestModelScoresRefresh(bool force) {
    if (!showModelScores_) {
        return;
    }

    bool expected = false;
    if (!force && !modelScoresInFlight_.compare_exchange_strong(expected, true)) {
        return;
    }
    modelScoresInFlight_ = true;

    RestartModelScoresTimer();

    const HWND target = hwnd_;
    const RadarMetricKind kind = modelScoreKind_;
    std::thread([this, target, kind]() {
        auto* result = new ModelIqSnapshot(fetcher_.FetchModelIq(kind));
        PostMessageW(target, kModelScoresUpdatedMessage, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void AppBarWindow::OnModelScoresUpdated(ModelIqSnapshot* snapshot) {
    std::unique_ptr<ModelIqSnapshot> holder(snapshot);
    modelScoresInFlight_ = false;
    if (snapshot != nullptr && showModelScores_ && snapshot->kind == modelScoreKind_) {
        modelScores_ = *snapshot;
        ClampModelScoresPage();
    }
    if (!taskbarMode_ && showModelScores_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppBarWindow::SetModelScoreMode(bool enabled, RadarMetricKind kind) {
    if (showModelScores_ == enabled && (!enabled || modelScoreKind_ == kind)) {
        return;
    }

    showModelScores_ = enabled;
    if (enabled) {
        modelScoreKind_ = kind;
    }
    modelScores_ = {};
    modelScoresPage_ = 0;
    SaveSettings();
    RestartModelScoresTimer();
    if (showModelScores_) {
        RequestModelScoresRefresh(true);
    }
    if (!taskbarMode_) {
        FitWindowToContent();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

static long long Iso8601ToUnix(const std::wstring& text);

void AppBarWindow::Paint(HDC hdc) {
    RECT clientRect = {};
    GetClientRect(hwnd_, &clientRect);

    if (RectWidth(clientRect) <= 0 || RectHeight(clientRect) <= 0) {
        return;
    }

    if (FAILED(CreateDeviceResources())) {
        return;
    }

    if (FAILED(renderTarget_->BindDC(hdc, &clientRect))) {
        DiscardDeviceResources();
        return;
    }

    const UINT dpi = GetDpiForWindow(hwnd_);
    renderTarget_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(96.0f / dpi, 96.0f / dpi));
    renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    renderTarget_->BeginDraw();
    PaintContent(clientRect);
    DrawAccountDropdown();
    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void AppBarWindow::DrawAccountDropdown() {
    if (!accountDropOpen_ || taskbarMode_ || accountDropRect_.right <= accountDropRect_.left) {
        return;
    }
    const std::vector<AccountEntry> accounts = accounts_.List(provider_);
    const int rowH = ScaleForDpi(hwnd_, 32);
    const int gap = ScaleForDpi(hwnd_, 4);
    int top = accountDropRect_.bottom + gap;
    const COLORREF panel = lightTheme_ ? RGB(255, 255, 255) : RGB(28, 34, 31);
    const COLORREF ink = lightTheme_ ? RGB(21, 27, 24) : RGB(240, 244, 241);
    const COLORREF muted = lightTheme_ ? RGB(94, 106, 97) : RGB(167, 178, 171);
    const COLORREF line = lightTheme_ ? RGB(219, 224, 220) : RGB(57, 66, 60);
    const COLORREF selectedFill = lightTheme_ ? RGB(224, 246, 239) : RGB(31, 58, 46);
    const int count = std::max(1, static_cast<int>(accounts.size()));
    RECT frame = MakeRect(accountDropRect_.left, top, accountDropRect_.right, top + count * rowH);
    solidBrush_->SetColor(ToColorF(panel));
    renderTarget_->FillRectangle(ToRectF(frame), solidBrush_.Get());
    solidBrush_->SetColor(ToColorF(line));
    renderTarget_->DrawRectangle(ToRectF(frame), solidBrush_.Get(), 1.0f);
    auto drawRow = [&](const RECT& row, const std::wstring& text, bool selected, const std::wstring& accountId) {
        if (selected) {
            solidBrush_->SetColor(ToColorF(selectedFill));
            renderTarget_->FillRectangle(ToRectF(row), solidBrush_.Get());
        }
        textFormatFoot_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormatFoot_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        textFormatFoot_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), textFormatFoot_.Get(),
                std::max(1.0f, static_cast<float>(RectWidth(row) - ScaleForDpi(hwnd_, 16))),
                std::max(1.0f, static_cast<float>(rowH)), layout.GetAddressOf()))) {
            solidBrush_->SetColor(ToColorF(selected ? ink : muted));
            renderTarget_->DrawTextLayout(
                D2D1::Point2F(static_cast<float>(row.left + ScaleForDpi(hwnd_, 12)), static_cast<float>(row.top)),
                layout.Get(), solidBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        UiHit hit;
        hit.rect = row;
        hit.accountId = accountId;
        uiHits_.insert(uiHits_.begin(), std::move(hit));
    };
    if (accounts.empty()) {
        drawRow(frame, LocalizeText(L"Import an account in Settings", L"请在设置里导入账号"), false, L"");
        return;
    }
    for (const AccountEntry& account : accounts) {
        const RECT row = MakeRect(accountDropRect_.left, top, accountDropRect_.right, top + rowH);
        const bool selected = _wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0;
        drawRow(row, account.label, selected, account.id);
        top += rowH;
    }
}

void AppBarWindow::PaintContent(const RECT& outerRect) {
    RECT clientRect = outerRect;
    refreshButtonRect_ = {};
    uiHits_.clear();
    modelScoresPrevRect_ = {};
    modelScoresNextRect_ = {};
    modelScoresSourceRect_ = {};
    modelScoreFilterChips_.clear();
    const PaceInfo pace = BuildPaceInfo(snapshot_);
    const int padX = ScaleForDpi(hwnd_, kHorizontalPadding);
    const int padY = ScaleForDpi(hwnd_, kVerticalPadding);
    const int meterHeight = ScaleForDpi(hwnd_, 12);
    const int footerTop = ScaleForDpi(hwnd_, 10);
    const int footerGap = ScaleForDpi(hwnd_, 12);
    const int sectionGap = ScaleForDpi(hwnd_, 10);
    const int heroHeight = ScaleForDpi(hwnd_, 76);
    const int metricRowHeight = ScaleForDpi(hwnd_, 52);
    (void)meterHeight;
    (void)footerTop;
    (void)footerGap;
    (void)sectionGap;
    (void)heroHeight;
    (void)metricRowHeight;

    // Background follows the lower remaining among available windows (100 green -> 0 red).
    int lowestRemaining = 100;
    if (snapshot_.success) {
        bool hasRemaining = false;
        if (snapshot_.fiveHour.available) {
            lowestRemaining = snapshot_.fiveHour.remainingPercent;
            hasRemaining = true;
        }
        if (snapshot_.weekly.available) {
            lowestRemaining = hasRemaining
                ? std::min(lowestRemaining, snapshot_.weekly.remainingPercent)
                : snapshot_.weekly.remainingPercent;
            hasRemaining = true;
        }
        if (!hasRemaining) {
            lowestRemaining = 100;
        }
    }
    const COLORREF background = snapshot_.success
        ? ColorForRemainingPercent(lowestRemaining, true)
        : (lightTheme_ ? RGB(251, 252, 248) : RGB(24, 28, 25));
    const COLORREF textPrimary = lightTheme_ ? RGB(21, 27, 24) : RGB(240, 244, 241);
    const COLORREF textSecondary = lightTheme_ ? RGB(94, 106, 97) : RGB(167, 178, 171);
    const COLORREF border = lightTheme_ ? RGB(219, 224, 220) : RGB(57, 66, 60);
    const COLORREF shadow = lightTheme_ ? RGB(226, 231, 225) : RGB(10, 14, 12);
    const COLORREF heroValue = snapshot_.success
        ? ColorForRemainingPercent(lowestRemaining, false)
        : textPrimary;
    const COLORREF trackColor = lightTheme_ ? RGB(229, 235, 230) : RGB(68, 76, 71);
    const COLORREF budgetMarkerColor = lightTheme_ ? RGB(21, 27, 24) : RGB(240, 244, 241);
    auto fillRect = [&](const RECT& rect, COLORREF color) {
        solidBrush_->SetColor(ToColorF(color));
        renderTarget_->FillRectangle(ToRectF(rect), solidBrush_.Get());
    };

    auto drawRectBorder = [&](const RECT& rect, COLORREF color) {
        solidBrush_->SetColor(ToColorF(color));
        renderTarget_->DrawRectangle(ToRectF(rect), solidBrush_.Get(), 1.0f);
    };

    auto drawTextBlock = [&](IDWriteTextFormat* format,
                             const std::wstring& text,
                             const RECT& rect,
                             COLORREF color,
                             DWRITE_TEXT_ALIGNMENT textAlignment,
                             DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment,
                             DWRITE_WORD_WRAPPING wrapping,
                             bool trimEllipsis) {
        format->SetTextAlignment(textAlignment);
        format->SetParagraphAlignment(paragraphAlignment);
        format->SetWordWrapping(wrapping);

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        const float layoutWidth = std::max(1.0f, static_cast<float>(RectWidth(rect)));
        const float layoutHeight = std::max(1.0f, static_cast<float>(RectHeight(rect)));
        if (FAILED(dwriteFactory_->CreateTextLayout(
                text.c_str(),
                static_cast<UINT32>(text.size()),
                format,
                layoutWidth,
                layoutHeight,
                layout.GetAddressOf()))) {
            return;
        }

        if (trimEllipsis) {
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsisSign;
            const DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(format, ellipsisSign.GetAddressOf()))) {
                layout->SetTrimming(&trimming, ellipsisSign.Get());
            }
        }

        solidBrush_->SetColor(ToColorF(color));
        renderTarget_->DrawTextLayout(
            D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
            layout.Get(),
            solidBrush_.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    auto measureTextWidth = [&](IDWriteTextFormat* format, const std::wstring& text) -> float {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory_->CreateTextLayout(
                text.c_str(),
                static_cast<UINT32>(text.size()),
                format,
                4096.0f,
                256.0f,
                layout.GetAddressOf()))) {
            return 0.0f;
        }

        DWRITE_TEXT_METRICS metrics = {};
        if (FAILED(layout->GetMetrics(&metrics))) {
            return 0.0f;
        }
        return metrics.widthIncludingTrailingWhitespace;
    };

    auto formatRadarScore = [](double score) {
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"%.1f", score);
        return std::wstring(buffer);
    };

    auto formatRadarCost = [](const ModelIqScore& score) {
        if (!score.hasPrice) {
            return std::wstring(L"--");
        }
        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"$%.2f", score.averagePriceUsd);
        return std::wstring(buffer);
    };

    auto formatRadarDuration = [&](const ModelIqScore& score) {
        if (!score.hasDuration) {
            return std::wstring(L"--");
        }
        wchar_t buffer[32] = {};
        swprintf_s(buffer, language_ == Language::Chinese ? L"%.1f分" : L"%.1fm", score.averageMinutes);
        return std::wstring(buffer);
    };

    auto radarStatusColor = [&](const std::wstring& status) -> COLORREF {
        if (status == L"red") {
            return lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120);
        }
        if (status == L"yellow") {
            return lightTheme_ ? RGB(184, 121, 38) : RGB(233, 180, 91);
        }
        return lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163);
    };

    auto drawModelScoresPanel = [&](int top, int left, int right) -> int {
        if (!showModelScores_) {
            return 0;
        }

        const int gap = ScaleForDpi(hwnd_, 6);
        const int pad = ScaleForDpi(hwnd_, 4);
        const int headerH = ScaleForDpi(hwnd_, 16);
        const int rowH = ScaleForDpi(hwnd_, 16);
        const int footH = ScaleForDpi(hwnd_, 14);
        const int innerPad = ScaleForDpi(hwnd_, 8);
        const int scoreColW = ScaleForDpi(hwnd_, 44);
        const int timeColW = ScaleForDpi(hwnd_, 52);
        const int costColW = ScaleForDpi(hwnd_, 56);
        const int colGap = ScaleForDpi(hwnd_, 6);
        const int metricColsW = scoreColW + colGap + timeColW + colGap + costColW;
        const int pagerH = ScaleForDpi(hwnd_, 18);
        const int rows = GetModelScoresVisibleRowCount();
        const int filterInnerLeft = left + innerPad;
        const int filterInnerRight = right - innerPad;
        const int filterH = GetModelScoreFilterBandHeight(std::max(1, filterInnerRight - filterInnerLeft));
        const int boxH = pad + filterH + headerH + ScaleForDpi(hwnd_, 4) + rows * rowH + pagerH + footH + pad;
        RECT box = MakeRect(left, top + gap, right, top + gap + boxH);
        fillRect(box, lightTheme_ ? RGB(248, 249, 248) : RGB(34, 39, 36));
        drawRectBorder(box, border);

        const int filterTop = box.top + pad;
        modelScoreFilterChips_ = BuildModelScoreFilterChips(
            box.left + innerPad, filterTop, box.right - innerPad);
        const COLORREF chipSelectedBg = lightTheme_ ? RGB(224, 246, 239) : RGB(31, 58, 46);
        const COLORREF chipSelectedText = lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163);
        for (const ModelScoreFilterChip& chip : modelScoreFilterChips_) {
            if (chip.selected) {
                fillRect(chip.rect, chipSelectedBg);
            }
            drawRectBorder(chip.rect, border);
            drawTextBlock(textFormatFoot_.Get(), chip.label, chip.rect,
                chip.selected ? chipSelectedText : textSecondary,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        }

        const std::wstring title = modelScoreKind_ == RadarMetricKind::VisualSpatial
            ? LocalizeText(L"Visual-spatial", L"视觉空间评分")
            : LocalizeText(L"Software engineering", L"软件工程评分");
        const int headerTop = filterTop + filterH;
        RECT headerLeft = MakeRect(box.left + innerPad, headerTop,
            box.right - innerPad - metricColsW - colGap, headerTop + headerH);
        RECT headerScore = MakeRect(box.right - innerPad - metricColsW, headerTop,
            box.right - innerPad - timeColW - colGap - costColW - colGap, headerTop + headerH);
        RECT headerTime = MakeRect(box.right - innerPad - timeColW - colGap - costColW, headerTop,
            box.right - innerPad - costColW - colGap, headerTop + headerH);
        RECT headerCost = MakeRect(box.right - innerPad - costColW, headerTop,
            box.right - innerPad, headerTop + headerH);
        drawTextBlock(textFormatFoot_.Get(), title, headerLeft, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Score", L"分数"), headerScore, textSecondary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Time", L"时间"), headerTime, textSecondary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Cost", L"金额"), headerCost, textSecondary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

        int rowTop = headerLeft.bottom + ScaleForDpi(hwnd_, 4);
        std::vector<const ModelIqScore*> visibleScores;
        if (modelScores_.success) {
            const int start = modelScoresPage_ * kModelScoresPageSize;
            int skipped = 0;
            for (const ModelIqScore& score : modelScores_.scores) {
                if (!MatchesModelScoreFamily(score)) {
                    continue;
                }
                if (skipped < start) {
                    ++skipped;
                    continue;
                }
                visibleScores.push_back(&score);
                if (static_cast<int>(visibleScores.size()) >= kModelScoresPageSize) {
                    break;
                }
            }
        }
        if (!visibleScores.empty()) {
            for (const ModelIqScore* scorePtr : visibleScores) {
                const ModelIqScore& score = *scorePtr;
                RECT nameRect = MakeRect(box.left + innerPad, rowTop,
                    box.right - innerPad - metricColsW - colGap, rowTop + rowH);
                RECT scoreRect = MakeRect(box.right - innerPad - metricColsW, rowTop,
                    box.right - innerPad - timeColW - colGap - costColW - colGap, rowTop + rowH);
                RECT timeRect = MakeRect(box.right - innerPad - timeColW - colGap - costColW, rowTop,
                    box.right - innerPad - costColW - colGap, rowTop + rowH);
                RECT costRect = MakeRect(box.right - innerPad - costColW, rowTop,
                    box.right - innerPad, rowTop + rowH);
                drawTextBlock(textFormatFoot_.Get(), score.label, nameRect, textPrimary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                drawTextBlock(textFormatFoot_.Get(), formatRadarScore(score.score), scoreRect,
                    radarStatusColor(score.status),
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                drawTextBlock(textFormatFoot_.Get(), formatRadarDuration(score), timeRect, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                drawTextBlock(textFormatFoot_.Get(), formatRadarCost(score), costRect, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                rowTop += rowH;
            }
        } else {
            RECT row = MakeRect(box.left + innerPad, rowTop, box.right - innerPad, rowTop + rowH);
            const std::wstring message = modelScoresInFlight_
                ? std::wstring(LocalizeText(L"Loading scores...", L"正在加载评分..."))
                : (modelScores_.success
                    ? std::wstring(LocalizeText(L"No models in this group", L"该模型组暂无数据"))
                    : (modelScores_.errorMessage.empty()
                        ? std::wstring(LocalizeText(L"No score data", L"暂无评分数据"))
                        : modelScores_.errorMessage));
            drawTextBlock(textFormatFoot_.Get(), message, row, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        }

        const int pageCount = GetModelScoresPageCount();
        const int pagerTop = box.bottom - pad - footH - pagerH;
        const int pagerBtnW = ScaleForDpi(hwnd_, 44);
        RECT prevRect = MakeRect(box.left + innerPad, pagerTop,
            box.left + innerPad + pagerBtnW, pagerTop + pagerH);
        RECT nextRect = MakeRect(box.right - innerPad - pagerBtnW, pagerTop,
            box.right - innerPad, pagerTop + pagerH);
        RECT pageRect = MakeRect(prevRect.right, pagerTop, nextRect.left, pagerTop + pagerH);
        modelScoresPrevRect_ = prevRect;
        modelScoresNextRect_ = nextRect;
        const bool canPrev = modelScoresPage_ > 0;
        const bool canNext = modelScoresPage_ + 1 < pageCount;
        const COLORREF pagerBg = lightTheme_ ? RGB(248, 249, 248) : RGB(40, 46, 42);
        fillRect(prevRect, pagerBg);
        drawRectBorder(prevRect, border);
        fillRect(nextRect, pagerBg);
        drawRectBorder(nextRect, border);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Prev", L"上一页"), prevRect,
            canPrev ? textPrimary : textSecondary,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Next", L"下一页"), nextRect,
            canNext ? textPrimary : textSecondary,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        const std::wstring pageText = std::to_wstring(modelScoresPage_ + 1) + L" / " + std::to_wstring(pageCount);
        drawTextBlock(textFormatFoot_.Get(), pageText, pageRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

        RECT attrRect = MakeRect(box.left + innerPad, box.bottom - pad - footH,
            box.right - innerPad, box.bottom - pad);
        modelScoresSourceRect_ = attrRect;
        const std::wstring sourceText = LocalizeText(L"Data: Codex Radar", L"数据来自 Codex 雷达");
        const COLORREF linkColor = lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163);
        drawTextBlock(textFormatFoot_.Get(), sourceText, attrRect, linkColor,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        const int linkWidth = static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), sourceText)));
        const int underlineTop = attrRect.bottom - std::max(1, ScaleForDpi(hwnd_, 2));
        fillRect(MakeRect(attrRect.left, underlineTop, attrRect.left + linkWidth, underlineTop + 1), linkColor);
        return gap + boxH;
    };

    if (taskbarMode_) {
        fillRect(MakeRect(clientRect.left + 1, clientRect.top + 2, clientRect.right + 1, clientRect.bottom + 2), shadow);
        fillRect(clientRect, background);
        drawRectBorder(clientRect, border);

        const bool showFive = snapshot_.success && snapshot_.fiveHour.available;
        const bool showWeek = snapshot_.success && snapshot_.weekly.available;
        const bool exhausted = snapshot_.success && (
            (showFive && snapshot_.fiveHour.remainingPercent <= 0)
            || (showWeek && snapshot_.weekly.remainingPercent <= 0)
            || (!showFive && !showWeek));
        const bool warning = snapshot_.success &&
            !exhausted &&
            ((showFive && snapshot_.fiveHour.remainingPercent <= 15)
                || (showWeek && snapshot_.weekly.remainingPercent <= 15)
                || pace.isOver);
        const COLORREF statusColor = !snapshot_.success
            ? textSecondary
            : (exhausted ? (lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120))
                         : (warning ? (lightTheme_ ? RGB(184, 121, 38) : RGB(233, 180, 91))
                                    : (lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163))));
        const COLORREF leftPane = lightTheme_ ? RGB(224, 246, 239) : RGB(31, 58, 46);
        const COLORREF rightPane = lightTheme_ ? RGB(239, 247, 226) : RGB(47, 59, 35);
        const int stripWidth = ScaleForDpi(hwnd_, 4);
        const int innerPad = ScaleForDpi(hwnd_, 8);
        const int dividerWidth = ScaleForDpi(hwnd_, 1);

        RECT statusStrip = MakeRect(clientRect.left, clientRect.top, clientRect.left + stripWidth, clientRect.bottom);
        fillRect(statusStrip, statusColor);

        RECT contentRect = MakeRect(statusStrip.right, clientRect.top, clientRect.right, clientRect.bottom);
        if (showFive && showWeek) {
            const int columnWidth = (RectWidth(contentRect) - dividerWidth) / 2;
            RECT fiveRect = MakeRect(contentRect.left, contentRect.top, contentRect.left + columnWidth, contentRect.bottom);
            RECT weekRect = MakeRect(fiveRect.right + dividerWidth, contentRect.top, contentRect.right, contentRect.bottom);
            fillRect(fiveRect, leftPane);
            fillRect(weekRect, rightPane);
            fillRect(MakeRect(fiveRect.right, contentRect.top + ScaleForDpi(hwnd_, 6),
                fiveRect.right + dividerWidth, contentRect.bottom - ScaleForDpi(hwnd_, 6)), border);

            RECT fiveLabelRect = MakeRect(fiveRect.left + innerPad, fiveRect.top + ScaleForDpi(hwnd_, 4),
                fiveRect.right - innerPad, fiveRect.top + ScaleForDpi(hwnd_, 18));
            RECT fiveValueRect = MakeRect(fiveRect.left + innerPad, fiveRect.top + ScaleForDpi(hwnd_, 16),
                fiveRect.right - innerPad, fiveRect.bottom - ScaleForDpi(hwnd_, 3));
            RECT weekLabelRect = MakeRect(weekRect.left + innerPad, weekRect.top + ScaleForDpi(hwnd_, 4),
                weekRect.right - innerPad, weekRect.top + ScaleForDpi(hwnd_, 18));
            RECT weekValueRect = MakeRect(weekRect.left + innerPad, weekRect.top + ScaleForDpi(hwnd_, 16),
                weekRect.right - innerPad, weekRect.bottom - ScaleForDpi(hwnd_, 3));

            drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"5h left", L"5小时剩余"), fiveLabelRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatMetricValue_.Get(), FormatPercent(snapshot_.fiveHour.remainingPercent), fiveValueRect, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
            drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Week left", L"本周剩余"), weekLabelRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatMetricValue_.Get(), FormatPercent(snapshot_.weekly.remainingPercent), weekValueRect, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        } else {
            // Single available window (usually weekly after 5h was removed).
            fillRect(contentRect, showFive ? leftPane : rightPane);
            const std::wstring label = showFive
                ? LocalizeText(L"5h left", L"5小时剩余")
                : LocalizeText(L"Week left", L"本周剩余");
            const std::wstring value = snapshot_.success
                ? FormatPercent(showFive ? snapshot_.fiveHour.remainingPercent : snapshot_.weekly.remainingPercent)
                : L"--";
            RECT labelRect = MakeRect(contentRect.left + innerPad, contentRect.top + ScaleForDpi(hwnd_, 4),
                contentRect.right - innerPad, contentRect.top + ScaleForDpi(hwnd_, 18));
            RECT valueRect = MakeRect(contentRect.left + innerPad, contentRect.top + ScaleForDpi(hwnd_, 16),
                contentRect.right - innerPad, contentRect.bottom - ScaleForDpi(hwnd_, 3));
            drawTextBlock(textFormatFoot_.Get(), label, labelRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatMetricValue_.Get(), value, valueRect, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        }
        return;
    }

    if (!taskbarMode_) {
        fillRect(clientRect, background);
        drawRectBorder(clientRect, border);
        const int chromeH = ScaleForDpi(hwnd_, surface_ == Surface::Summary ? 44 : 78);
        const int rowH = ScaleForDpi(hwnd_, 28);
        const int gap = ScaleForDpi(hwnd_, 6);
        const int left = clientRect.left + ScaleForDpi(hwnd_, 14);
        const int right = clientRect.right - ScaleForDpi(hwnd_, 14);
        const int row1 = clientRect.top + ScaleForDpi(hwnd_, 10);
        const int row2 = row1 + rowH + gap;
        auto addHit = [&](const RECT& rect, UINT command, const std::wstring& accountId = L"") {
            UiHit hit;
            hit.rect = rect;
            hit.command = command;
            hit.accountId = accountId;
            uiHits_.push_back(std::move(hit));
        };
        auto chip = [&](RECT rect, const std::wstring& text, bool selected, UINT command) {
            fillRect(rect, selected ? (lightTheme_ ? RGB(21, 27, 24) : RGB(236, 240, 236)) : trackColor);
            drawTextBlock(textFormatFoot_.Get(), text, rect, selected ? (lightTheme_ ? RGB(248, 249, 248) : RGB(21, 27, 24)) : textPrimary,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            addHit(rect, command);
        };
        RECT codexTab = MakeRect(left, row1, left + ScaleForDpi(hwnd_, 70), row1 + rowH);
        RECT grokTab = MakeRect(codexTab.right + gap, row1, codexTab.right + gap + ScaleForDpi(hwnd_, 62), row1 + rowH);
        chip(codexTab, L"Codex", provider_ == L"codex" && surface_ == Surface::Usage, kCommandProviderCodex);
        chip(grokTab, L"Grok", provider_ == L"grok" && surface_ == Surface::Usage, kCommandProviderGrok);
        const std::wstring summaryLabel = LocalizeText(L"Summary", L"汇总");
        const std::wstring settingsLabel = surface_ == Surface::Settings
            ? LocalizeText(L"Done", L"完成")
            : LocalizeText(L"Settings", L"设置");
        const std::wstring refreshLabel = LocalizeText(L"Reload", L"刷新");
        auto chipWidth = [&](const std::wstring& text) {
            return std::max(
                ScaleForDpi(hwnd_, 48),
                static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), text))) + ScaleForDpi(hwnd_, 18));
        };
        const int refreshW = chipWidth(refreshLabel);
        const int settingsW = chipWidth(settingsLabel);
        const int summaryW = chipWidth(summaryLabel);
        RECT refreshRect = MakeRect(right - refreshW, row1, right, row1 + rowH);
        RECT settingsRect = MakeRect(refreshRect.left - gap - settingsW, row1, refreshRect.left - gap, row1 + rowH);
        RECT summaryRect = MakeRect(settingsRect.left - gap - summaryW, row1, settingsRect.left - gap, row1 + rowH);
        chip(summaryRect, summaryLabel, surface_ == Surface::Summary, kUiSummary);
        chip(settingsRect, settingsLabel, surface_ == Surface::Settings, kUiSettings);
        chip(refreshRect, refreshLabel, false, kCommandRefresh);
        const std::vector<AccountEntry> providerAccounts = accounts_.List(provider_);
        std::wstring accountLabel = LocalizeText(L"Import an account", L"导入账号");
        for (const AccountEntry& account : providerAccounts) {
            if (activeAuthId_.empty() || _wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0) {
                accountLabel = account.label;
                break;
            }
        }
        if (surface_ == Surface::Summary) {
            accountDropRect_ = {};
        } else {
        accountDropRect_ = MakeRect(left, row2, right, row2 + rowH);
        fillRect(accountDropRect_, accountDropOpen_
            ? (lightTheme_ ? RGB(232, 236, 233) : RGB(46, 54, 49))
            : (lightTheme_ ? RGB(244, 246, 243) : RGB(36, 42, 38)));
        drawRectBorder(accountDropRect_, border);
        RECT labelRect = accountDropRect_;
        labelRect.left += ScaleForDpi(hwnd_, 12);
        labelRect.right -= ScaleForDpi(hwnd_, 28);
        drawTextBlock(textFormatFoot_.Get(), accountLabel, labelRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        RECT chevronRect = MakeRect(accountDropRect_.right - ScaleForDpi(hwnd_, 28), row2, accountDropRect_.right, row2 + rowH);
        drawTextBlock(textFormatFoot_.Get(), accountDropOpen_ ? L"^" : L"v", chevronRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        addHit(accountDropRect_, kUiAccounts);
        }
        clientRect.top += chromeH;
        if (surface_ == Surface::Accounts || surface_ == Surface::Settings) {
            fillRect(clientRect, background);
            int y = clientRect.top + ScaleForDpi(hwnd_, 8);
            const int chipH = ScaleForDpi(hwnd_, 28);
            auto wrapChips = [&](std::vector<std::pair<std::wstring, UINT>> items, auto selected) {
                int x = left;
                for (const auto& item : items) {
                    const int w = std::max(ScaleForDpi(hwnd_, 64), static_cast<int>(measureTextWidth(textFormatFoot_.Get(), item.first)) + ScaleForDpi(hwnd_, 22));
                    if (x + w > right) {
                        x = left;
                        y += chipH + gap;
                    }
                    chip(MakeRect(x, y, x + w, y + chipH), item.first, selected(item.second), item.second);
                    x += w + gap;
                }
                y += chipH + ScaleForDpi(hwnd_, 14);
            };
            auto section = [&](const wchar_t* title) {
                RECT titleRect = MakeRect(left, y, right, y + ScaleForDpi(hwnd_, 18));
                drawTextBlock(textFormatFoot_.Get(), title, titleRect, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                y += ScaleForDpi(hwnd_, 22);
            };
            if (surface_ == Surface::Accounts) {
                section(provider_ == L"grok" ? L"Grok" : L"Codex");
                if (providerAccounts.empty()) {
                    RECT empty = MakeRect(left, y, right, y + chipH);
                    drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"No account in this provider yet", L"这个服务还没有账号"), empty, textSecondary,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                    y += chipH + gap;
                }
                for (const AccountEntry& account : providerAccounts) {
                    const bool selected = _wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0;
                    RECT row = MakeRect(left, y, right, y + ScaleForDpi(hwnd_, 34));
                    fillRect(row, selected ? (lightTheme_ ? RGB(224, 246, 239) : RGB(31, 58, 46)) : trackColor);
                    drawTextBlock(textFormatFoot_.Get(), (selected ? L"● " : L"") + account.label, row, textPrimary,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                    addHit(row, 0, account.id);
                    y += ScaleForDpi(hwnd_, 40);
                }
                wrapChips({
                    {LocalizeText(L"Import", L"导入文件"), kCommandImportAccount},
                    {LocalizeText(L"Paste", L"粘贴"), kCommandPasteImport},
                    {LocalizeText(L"Browser", L"浏览器登录"), kCommandBrowserLogin},
                    {LocalizeText(L"Rename", L"重命名"), kCommandAlias},
                    {LocalizeText(L"Delete", L"删除"), kCommandDeleteAccount},
                }, [](UINT) { return false; });
            } else {
                section(LocalizeText(L"Account", L"账号"));
                const std::vector<AccountEntry> allAccounts = accounts_.List(L"");
                if (allAccounts.empty()) {
                    RECT empty = MakeRect(left, y, right, y + chipH);
                    drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"No accounts imported yet", L"还没有导入账号"), empty, textSecondary,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                    y += chipH + gap;
                }
                for (const AccountEntry& account : allAccounts) {
                    const int rowH = ScaleForDpi(hwnd_, 28);
                    const int deleteW = ScaleForDpi(hwnd_, 52);
                    const int aliasW = ScaleForDpi(hwnd_, 52);
                    const int typeW = ScaleForDpi(hwnd_, 58);
                    const bool grokAccount = _wcsicmp(account.provider.c_str(), L"grok") == 0;
                    RECT row = MakeRect(left, y, right, y + rowH);
                    fillRect(row, trackColor);
                    RECT typeRect = MakeRect(row.left, row.top, row.left + typeW, row.bottom);
                    fillRect(typeRect, grokAccount
                        ? (lightTheme_ ? RGB(232, 236, 255) : RGB(36, 44, 72))
                        : (lightTheme_ ? RGB(232, 246, 236) : RGB(31, 58, 46)));
                    drawTextBlock(textFormatFoot_.Get(), grokAccount ? L"Grok" : L"Codex", typeRect, textPrimary,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                    const std::wstring who = account.email.empty() ? account.label : account.email;
                    const std::wstring shown = account.alias.empty() ? who : (account.alias + L"  ·  " + who);
                    RECT nameRect = MakeRect(typeRect.right + ScaleForDpi(hwnd_, 8), row.top, row.right - deleteW - aliasW - gap * 2, row.bottom);
                    drawTextBlock(textFormatFoot_.Get(), shown, nameRect, textPrimary,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                    RECT aliasRect = MakeRect(row.right - deleteW - gap - aliasW, row.top, row.right - deleteW - gap, row.bottom);
                    fillRect(aliasRect, lightTheme_ ? RGB(232, 236, 233) : RGB(46, 54, 49));
                    drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Alias", L"别称"), aliasRect, textPrimary,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                    addHit(aliasRect, kCommandAlias, account.id);
                    RECT deleteRect = MakeRect(row.right - deleteW, row.top, row.right, row.bottom);
                    fillRect(deleteRect, lightTheme_ ? RGB(255, 236, 232) : RGB(92, 42, 36));
                    drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Delete", L"删除"), deleteRect, lightTheme_ ? RGB(176, 48, 32) : RGB(255, 180, 168),
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                    addHit(deleteRect, kCommandDeleteAccount, account.id);
                    y += rowH + gap;
                }
                wrapChips({
                    {LocalizeText(L"Import", L"导入文件"), kCommandImportAccount},
                    {LocalizeText(L"Paste", L"粘贴"), kCommandPasteImport},
                    {LocalizeText(L"Browser", L"浏览器登录"), kCommandBrowserLogin},
                }, [](UINT) { return false; });
                section(LocalizeText(L"Display", L"显示"));
                wrapChips({
                    {LocalizeText(L"Full", L"完整"), kCommandFullMode},
                    {LocalizeText(L"Simple", L"简单"), kCommandSimpleMode},
                    {LocalizeText(L"Taskbar", L"任务栏"), kCommandTaskbarMode},
                }, [&](UINT command) {
                    return (command == kCommandFullMode && !simpleMode_ && !taskbarMode_)
                        || (command == kCommandSimpleMode && simpleMode_)
                        || (command == kCommandTaskbarMode && taskbarMode_);
                });
                section(LocalizeText(L"Language", L"语言"));
                wrapChips({
                    {L"EN", kCommandLanguageEnglish},
                    {L"简", kCommandLanguageChinese},
                    {L"繁", kCommandLanguageTraditional},
                    {L"한", kCommandLanguageKorean},
                    {L"日", kCommandLanguageJapanese},
                    {L"RU", kCommandLanguageRussian},
                    {L"FR", kCommandLanguageFrench},
                }, [&](UINT command) {
                    return (command == kCommandLanguageEnglish && language_ == Language::English)
                        || (command == kCommandLanguageChinese && language_ == Language::Chinese)
                        || (command == kCommandLanguageTraditional && language_ == Language::Traditional)
                        || (command == kCommandLanguageKorean && language_ == Language::Korean)
                        || (command == kCommandLanguageJapanese && language_ == Language::Japanese)
                        || (command == kCommandLanguageRussian && language_ == Language::Russian)
                        || (command == kCommandLanguageFrench && language_ == Language::French);
                });
                section(LocalizeText(L"Refresh", L"刷新"));
                wrapChips({
                    {L"1m", kCommandRefreshInterval1Minute},
                    {L"3m", kCommandRefreshInterval3Minutes},
                    {L"5m", kCommandRefreshInterval5Minutes},
                    {L"10m", kCommandRefreshInterval10Minutes},
                    {L"30m", kCommandRefreshInterval30Minutes},
                }, [&](UINT command) {
                    return (command == kCommandRefreshInterval1Minute && refreshIntervalSeconds_ == 60)
                        || (command == kCommandRefreshInterval3Minutes && refreshIntervalSeconds_ == 180)
                        || (command == kCommandRefreshInterval5Minutes && refreshIntervalSeconds_ == 300)
                        || (command == kCommandRefreshInterval10Minutes && refreshIntervalSeconds_ == 600)
                        || (command == kCommandRefreshInterval30Minutes && refreshIntervalSeconds_ == 1800);
                });
                section(LocalizeText(L"Switches", L"开关"));
                wrapChips({
                    {LocalizeText(L"Reset news", L"重置公告"), kCommandResetStatusToggle},
                    {LocalizeText(L"Estimate", L"周估算"), kCommandEstimateToggle},
                    {LocalizeText(L"On top", L"置顶"), kCommandAlwaysOnTop},
                    {LocalizeText(L"Lock", L"锁定"), kCommandLockPosition},
                    {LocalizeText(L"Startup", L"开机启动"), kCommandLaunchAtStartup},
                }, [&](UINT command) {
                    return (command == kCommandResetStatusToggle && resetStatusEnabled_)
                        || (command == kCommandEstimateToggle && estimateEnabled_)
                        || (command == kCommandAlwaysOnTop && alwaysOnTop_)
                        || (command == kCommandLockPosition && lockPosition_)
                        || (command == kCommandLaunchAtStartup && IsLaunchAtStartupEnabled());
                });
                section(LocalizeText(L"Proxy", L"代理"));
                wrapChips({
                    {LocalizeText(L"System", L"系统"), kCommandProxySystem},
                    {L"HTTP", kCommandProxyHttp},
                    {L"SOCKS5", kCommandProxySocks},
                    {LocalizeText(L"Edit", L"编辑"), kCommandProxyEdit},
                    {LocalizeText(L"Test", L"测试"), kCommandProxyTest},
                }, [&](UINT command) {
                    return (command == kCommandProxySystem && proxy_.mode == ProxyConfig::Mode::System)
                        || (command == kCommandProxyHttp && proxy_.mode == ProxyConfig::Mode::Http)
                        || (command == kCommandProxySocks && proxy_.mode == ProxyConfig::Mode::Socks5);
                });
                section(LocalizeText(L"Usage detail", L"用量"));
                wrapChips({
                    {LocalizeText(L"Hidden", L"隐藏"), kCommandDetailOff},
                    {LocalizeText(L"Chart", L"图表"), kCommandDetailChart},
                    {LocalizeText(L"Sessions", L"会话"), kCommandDetailSessions},
                    {LocalizeText(L"Heat", L"热力"), kCommandChartHeat},
                    {LocalizeText(L"Line", L"折线"), kCommandChartLine},
                    {LocalizeText(L"Bar", L"柱状"), kCommandChartBar},
                }, [&](UINT command) {
                    return (command == kCommandDetailOff && featurePage_ == FeaturePage::None)
                        || (command == kCommandDetailChart && featurePage_ == FeaturePage::Chart)
                        || (command == kCommandDetailSessions && featurePage_ == FeaturePage::Sessions)
                        || (command == kCommandChartHeat && chartKind_ == ChartKind::Heat)
                        || (command == kCommandChartLine && chartKind_ == ChartKind::Line)
                        || (command == kCommandChartBar && chartKind_ == ChartKind::Bar);
                });
                section(LocalizeText(L"Maintain", L"维护"));
                wrapChips({
                    {LocalizeText(L"Version", L"检查版本"), kCommandCheckVersion},
                    {LocalizeText(L"Update", L"下载更新"), kCommandDownloadUpdate},
                    {LocalizeText(L"Reset place", L"重置位置"), kCommandResetPosition},
                    {LocalizeText(L"Exit", L"退出"), kCommandExit},
                }, [](UINT) { return false; });
            }
            chip(MakeRect(left, y, left + ScaleForDpi(hwnd_, 120), y + chipH), LocalizeText(L"Back to usage", L"返回用量"), false, kUiCloseSheet);
            return;
        }
        if (surface_ == Surface::Summary) {
            fillRect(clientRect, background);
            int y = clientRect.top + ScaleForDpi(hwnd_, 6);
            const int rowH = ScaleForDpi(hwnd_, 22);
            const int iconSize = ScaleForDpi(hwnd_, 16);
            const int nameW = iconSize + ScaleForDpi(hwnd_, 8)
                + static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), L"Grok  账号别称")))
                + ScaleForDpi(hwnd_, 8);
            const int timeW = static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), L"00/00 00:00")))
                + ScaleForDpi(hwnd_, 14);
            const int percentW = static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), L"100.0%")))
                + ScaleForDpi(hwnd_, 8);
            auto drawPlatformIcon = [&](bool grokRowIcon, float centerX, float centerY, float size) {
                ID2D1Bitmap* bitmap = grokRowIcon ? grokIcon_.Get() : codexIcon_.Get();
                if (bitmap == nullptr) {
                    return;
                }
                const D2D1_RECT_F dest = D2D1::RectF(centerX - size * 0.5f, centerY - size * 0.5f, centerX + size * 0.5f, centerY + size * 0.5f);
                renderTarget_->DrawBitmap(bitmap, dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            };
            std::vector<AccountQuotaRow> tableRows = summaryRows_;
            std::stable_sort(tableRows.begin(), tableRows.end(), [](const AccountQuotaRow& leftRow, const AccountQuotaRow& rightRow) {
                const auto resetOf = [](const AccountQuotaRow& row) -> long long {
                    if (row.loading || !row.hasQuota) {
                        return 0;
                    }
                    const long long resetAt = row.resetAtUnixSeconds > 0
                        ? row.resetAtUnixSeconds
                        : Iso8601ToUnix(row.resetIso);
                    return resetAt > 0 ? resetAt : 0;
                };
                const long long leftReset = resetOf(leftRow);
                const long long rightReset = resetOf(rightRow);
                const bool leftKnown = leftReset > 0;
                const bool rightKnown = rightReset > 0;
                if (leftKnown != rightKnown) {
                    return leftKnown;
                }
                return leftReset < rightReset;
            });
            const int bodyRows = std::max(1, static_cast<int>(tableRows.size()));
            RECT table = MakeRect(left, y, right, y + rowH * (bodyRows + 1));
            drawRectBorder(table, border);
            const int nameRight = table.left + nameW;
            const int timeLeft = table.right - timeW;
            auto vline = [&](int x, int top, int bottom) {
                fillRect(MakeRect(x, top, x + 1, bottom), border);
            };
            auto hline = [&](int lineY) {
                fillRect(MakeRect(table.left, lineY, table.right, lineY + 1), border);
            };
            RECT headName = MakeRect(table.left + ScaleForDpi(hwnd_, 6), table.top, nameRight, table.top + rowH);
            RECT headUsed = MakeRect(nameRight + ScaleForDpi(hwnd_, 6), table.top, timeLeft, table.top + rowH);
            RECT headTime = MakeRect(timeLeft, table.top, table.right - ScaleForDpi(hwnd_, 6), table.top + rowH);
            drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Account", L"账号"), headName, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Used", L"已用"), headUsed, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Reset", L"重置"), headTime, textSecondary,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            hline(table.top + rowH);
            vline(nameRight, table.top, table.bottom);
            vline(timeLeft, table.top, table.bottom);
            if (tableRows.empty()) {
                RECT empty = MakeRect(table.left + ScaleForDpi(hwnd_, 6), table.top + rowH, table.right - ScaleForDpi(hwnd_, 6), table.bottom);
                drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"No accounts imported yet", L"还没有导入账号"), empty, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            }
            for (int index = 0; index < static_cast<int>(tableRows.size()); ++index) {
                const AccountQuotaRow& row = tableRows[static_cast<size_t>(index)];
                const int rowTop = table.top + rowH * (index + 1);
                const bool grokRow = _wcsicmp(row.provider.c_str(), L"grok") == 0;
                std::wstring resetText = L"--";
                std::wstring usedText = row.loading ? LocalizeText(L"...", L"…") : L"--";
                int usedPercent = 0;
                bool showBar = false;
                if (!row.loading && row.hasQuota) {
                    usedPercent = ClampInt(100 - row.remainingPercent, 0, 100);
                    usedText = FormatPercent(usedPercent);
                    showBar = true;
                    const long long resetAt = row.resetAtUnixSeconds > 0
                        ? row.resetAtUnixSeconds
                        : Iso8601ToUnix(row.resetIso);
                    resetText = FormatDateTime(resetAt);
                } else if (!row.loading && !row.success && !row.error.empty()) {
                    resetText = row.error;
                }
                const float iconX = static_cast<float>(table.left + ScaleForDpi(hwnd_, 8) + iconSize / 2);
                const float iconY = static_cast<float>(rowTop + rowH / 2);
                drawPlatformIcon(grokRow, iconX, iconY, static_cast<float>(iconSize));
                RECT nameRect = MakeRect(table.left + ScaleForDpi(hwnd_, 10) + iconSize, rowTop, nameRight - ScaleForDpi(hwnd_, 4), rowTop + rowH);
                RECT percentRect = MakeRect(timeLeft - percentW, rowTop, timeLeft - ScaleForDpi(hwnd_, 8), rowTop + rowH);
                RECT track = MakeRect(nameRight + ScaleForDpi(hwnd_, 8), rowTop + ScaleForDpi(hwnd_, 7), percentRect.left - ScaleForDpi(hwnd_, 6), rowTop + rowH - ScaleForDpi(hwnd_, 7));
                RECT timeRect = MakeRect(timeLeft + ScaleForDpi(hwnd_, 6), rowTop, table.right - ScaleForDpi(hwnd_, 6), rowTop + rowH);
                const std::wstring accountName = !row.alias.empty()
                    ? row.alias
                    : (row.email.empty() ? row.label : row.email);
                const std::wstring shownName = (grokRow ? L"Grok  " : L"Codex  ") + accountName;
                drawTextBlock(textFormatFoot_.Get(), shownName, nameRect, textPrimary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                fillRect(track, lightTheme_ ? RGB(255, 255, 255) : RGB(20, 24, 22));
                if (showBar && RectWidth(track) > 0) {
                    RECT fill = track;
                    fill.right = fill.left + RectWidth(track) * usedPercent / 100;
                    if (fill.right > fill.left) {
                        fillRect(fill, ColorForRemainingPercent(100 - usedPercent, false));
                    }
                }
                drawTextBlock(textFormatFoot_.Get(), usedText, percentRect, showBar ? ColorForRemainingPercent(100 - usedPercent, false) : textSecondary,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
                drawTextBlock(textFormatFoot_.Get(), resetText, timeRect, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            }
            y = table.bottom + ScaleForDpi(hwnd_, 4);
            if (summaryInFlight_) {
                RECT status = MakeRect(left, y, right, y + ScaleForDpi(hwnd_, 18));
                drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Refreshing weekly limits...", L"正在刷新周限额..."), status, textSecondary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
                y += ScaleForDpi(hwnd_, 22);
            }
            y += drawModelScoresPanel(y, left, right);
            return;
        }
    }

    if (simpleMode_ && provider_ != L"grok") {
        fillRect(MakeRect(clientRect.left + 2, clientRect.top + 3, clientRect.right + 2, clientRect.bottom + 3), shadow);
        fillRect(clientRect, background);
        drawRectBorder(clientRect, border);

        const bool loadFailed = !snapshot_.success && !snapshot_.errorMessage.empty();
        const bool showFive = snapshot_.success && snapshot_.fiveHour.available;
        const bool showWeek = snapshot_.success && snapshot_.weekly.available;
        const bool exhausted = snapshot_.success && (
            (showFive && snapshot_.fiveHour.remainingPercent <= 0)
            || (showWeek && snapshot_.weekly.remainingPercent <= 0));
        const bool warning = snapshot_.success &&
            !exhausted &&
            ((showFive && snapshot_.fiveHour.remainingPercent <= 15)
                || (showWeek && snapshot_.weekly.remainingPercent <= 15)
                || pace.isOver);
        const wchar_t* statusText = !snapshot_.success
            ? (loadFailed
                ? LocalizeText(L"Failed", L"失败")
                : LocalizeText(L"Loading", L"加载中"))
            : (exhausted
                ? LocalizeText(L"Exhausted", L"用尽")
                : (warning ? LocalizeText(L"Tight", L"紧张") : LocalizeText(L"Normal", L"正常")));
        const COLORREF statusColor = !snapshot_.success
            ? (loadFailed
                ? (lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120))
                : textSecondary)
            : (exhausted ? (lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120))
                         : (warning ? (lightTheme_ ? RGB(184, 121, 38) : RGB(233, 180, 91))
                                    : (lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163))));
        const COLORREF dayCard = lightTheme_ ? RGB(224, 246, 239) : RGB(31, 58, 46);
        const COLORREF weekCard = lightTheme_ ? RGB(239, 247, 226) : RGB(47, 59, 35);
        const std::wstring versionStatusText = GetVersionStatusText(true);
        const int topBandHeight = ScaleForDpi(hwnd_, 34);
        const int innerPad = ScaleForDpi(hwnd_, 12);
        const int cardGap = ScaleForDpi(hwnd_, 10);
        const int footerHeight = ScaleForDpi(hwnd_, 16);

        RECT titleRect = MakeRect(clientRect.left + innerPad, clientRect.top + ScaleForDpi(hwnd_, 6),
            clientRect.right - innerPad - ScaleForDpi(hwnd_, 66), clientRect.top + topBandHeight);
        RECT statusRect = MakeRect(clientRect.right - innerPad - ScaleForDpi(hwnd_, 54), clientRect.top + ScaleForDpi(hwnd_, 8),
            clientRect.right - innerPad, clientRect.top + ScaleForDpi(hwnd_, 28));
        drawTextBlock(textFormatMetricValue_.Get(), LocalizeText(L"Remaining", L"剩余额度"), titleRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        drawTextBlock(textFormatMetricLabel_.Get(), statusText, statusRect, statusColor,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

        const int resetBandHeight = ScaleForDpi(hwnd_, 18);
        RECT cardsRect = MakeRect(clientRect.left + innerPad, clientRect.top + topBandHeight + ScaleForDpi(hwnd_, 2),
            clientRect.right - innerPad,
            clientRect.bottom - footerHeight - resetBandHeight - GetModelScoresPanelHeight() - ScaleForDpi(hwnd_, 6));

        auto drawSimpleCard = [&](const RECT& cardRect, COLORREF cardColor, const wchar_t* label, const std::wstring& value) {
            fillRect(cardRect, cardColor);
            RECT labelRect = MakeRect(cardRect.left + innerPad, cardRect.top + ScaleForDpi(hwnd_, 8),
                cardRect.right - innerPad, cardRect.top + ScaleForDpi(hwnd_, 24));
            RECT valueRect = MakeRect(cardRect.left + innerPad, cardRect.top + ScaleForDpi(hwnd_, 24),
                cardRect.right - innerPad, cardRect.bottom - ScaleForDpi(hwnd_, 8));
            drawTextBlock(textFormatMetricLabel_.Get(), label, labelRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, false);
            drawTextBlock(textFormatDelta_.Get(), value, valueRect, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        };

        if (showFive && showWeek) {
            const int cardWidth = (RectWidth(cardsRect) - cardGap) / 2;
            RECT dayRect = MakeRect(cardsRect.left, cardsRect.top, cardsRect.left + cardWidth, cardsRect.bottom);
            RECT weekRect = MakeRect(dayRect.right + cardGap, cardsRect.top, cardsRect.right, cardsRect.bottom);
            drawSimpleCard(dayRect, dayCard, LocalizeText(L"5h left", L"5小时剩余"),
                FormatPercent(snapshot_.fiveHour.remainingPercent));
            drawSimpleCard(weekRect, weekCard, LocalizeText(L"Week left", L"本周剩余"),
                FormatPercent(snapshot_.weekly.remainingPercent));
        } else {
            const std::wstring value = snapshot_.success
                ? FormatPercent(showFive ? snapshot_.fiveHour.remainingPercent : snapshot_.weekly.remainingPercent)
                : L"--";
            drawSimpleCard(
                cardsRect,
                showFive ? dayCard : weekCard,
                showFive ? LocalizeText(L"5h left", L"5小时剩余") : LocalizeText(L"Week left", L"本周剩余"),
                value);
        }

        const std::wstring resetSummary = BuildResetCreditsSummaryText();
        const std::wstring resetExpiry = BuildResetCreditsExpiryText();
        RECT resetSummaryRect = MakeRect(clientRect.left + innerPad, cardsRect.bottom + ScaleForDpi(hwnd_, 2),
            clientRect.right - innerPad, cardsRect.bottom + resetBandHeight);
        drawTextBlock(textFormatFoot_.Get(), resetSummary + L" · " + resetExpiry, resetSummaryRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        if (!resetCreditActionMessage_.empty()) {
            RECT actionMsg = MakeRect(clientRect.left + innerPad, cardsRect.bottom + ScaleForDpi(hwnd_, 2),
                clientRect.right - innerPad, cardsRect.bottom + resetBandHeight);
            drawTextBlock(textFormatFoot_.Get(), resetCreditActionMessage_, actionMsg,
                lightTheme_ ? RGB(176, 78, 18) : RGB(255, 186, 120),
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        }

        const int simpleScoresH = GetModelScoresPanelHeight();
        if (simpleScoresH > 0) {
            drawModelScoresPanel(
                clientRect.bottom - footerHeight - simpleScoresH,
                clientRect.left + innerPad,
                clientRect.right - innerPad);
        }

        const std::wstring refreshCountdownText = refreshInFlight_
            ? std::wstring(LocalizeText(L"Refreshing", L"刷新中"))
            : FormatRefreshCountdown(refreshCountdownSeconds_);
        RECT footerLeftRect = MakeRect(clientRect.left + innerPad, clientRect.bottom - footerHeight - ScaleForDpi(hwnd_, 1),
            clientRect.right / 2, clientRect.bottom - ScaleForDpi(hwnd_, 1));
        RECT footerRightRect = MakeRect(clientRect.right / 2, clientRect.bottom - footerHeight - ScaleForDpi(hwnd_, 1),
            clientRect.right - innerPad, clientRect.bottom - ScaleForDpi(hwnd_, 1));
        drawTextBlock(textFormatFoot_.Get(), versionStatusText, footerLeftRect, updateAvailable_ ? heroValue : textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        drawTextBlock(textFormatFoot_.Get(), refreshCountdownText, footerRightRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        return;
    }

    // Full mode card layout (screenshot style, no Spark rows).
    int y = clientRect.top + padY;
    if (provider_ == L"grok") {
        fillRect(MakeRect(clientRect.left + 2, clientRect.top + 3, clientRect.right + 2, clientRect.bottom + 3), shadow);
        fillRect(clientRect, background);
        drawRectBorder(clientRect, border);
        RECT titleRect = MakeRect(clientRect.left + padX, clientRect.top + padY, clientRect.right - padX, clientRect.top + padY + ScaleForDpi(hwnd_, 24));
        std::wstring grokTitle = L"Grok";
        for (const AccountEntry& account : accounts_.List(L"grok")) {
            if (_wcsicmp(account.id.c_str(), activeAuthId_.c_str()) == 0 || activeAuthId_.empty()) {
                grokTitle = L"Grok  " + account.label;
                break;
            }
        }
        drawTextBlock(textFormatMetricValue_.Get(), grokTitle, titleRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        y = titleRect.bottom + ScaleForDpi(hwnd_, 8);
        if (!grok_.success) {
            RECT errorRect = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 48));
            drawTextBlock(textFormatFoot_.Get(),
                grok_.error.empty() ? std::wstring(LocalizeText(L"Loading Grok usage", L"正在加载 Grok 用量")) : grok_.error,
                errorRect, lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120),
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP, false);
            y += ScaleForDpi(hwnd_, 52);
        } else {
        auto meter = [&](const std::wstring& label, int usedPercent) {
            const int clamped = std::max(0, std::min(100, usedPercent));
            RECT labelRect = MakeRect(clientRect.left + padX, y, clientRect.right - padX - ScaleForDpi(hwnd_, 48), y + ScaleForDpi(hwnd_, 18));
            RECT valueRect = MakeRect(labelRect.right, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 18));
            drawTextBlock(textFormatFoot_.Get(), label, labelRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            drawTextBlock(textFormatFoot_.Get(), std::to_wstring(clamped) + L"%", valueRect, textPrimary,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
            y += ScaleForDpi(hwnd_, 20);
            RECT track = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 10));
            fillRect(track, trackColor);
            RECT used = track;
            used.right = used.left + RectWidth(track) * clamped / 100;
            const COLORREF bar = clamped >= 100
                ? (lightTheme_ ? RGB(196, 54, 32) : RGB(255, 144, 120))
                : (clamped >= 85 ? (lightTheme_ ? RGB(184, 121, 38) : RGB(233, 180, 91))
                                 : (lightTheme_ ? RGB(21, 148, 78) : RGB(118, 216, 163)));
            if (used.right > used.left) {
                fillRect(used, bar);
            }
            y += ScaleForDpi(hwnd_, 18);
        };
        if (grok_.hasUsagePercent) {
            meter(LocalizeText(L"Week used", L"本周已用"), static_cast<int>(grok_.usagePercent));
        }
        for (const GrokProduct& product : grok_.products) {
            if (!product.hasPercent) {
                continue;
            }
            meter(product.name, static_cast<int>(product.usagePercent));
        }
        if (grok_.hasPrepaid || grok_.hasOnDemand) {
            std::wstring extra;
            if (grok_.hasPrepaid) {
                extra += L"prepaid " + std::to_wstring(static_cast<int>(grok_.prepaidCents));
            }
            if (grok_.hasOnDemand) {
                if (!extra.empty()) {
                    extra += L"   ";
                }
                extra += L"on-demand " + std::to_wstring(static_cast<int>(grok_.onDemandCents));
            }
            RECT extraRect = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 18));
            drawTextBlock(textFormatFoot_.Get(), extra, extraRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            y += ScaleForDpi(hwnd_, 20);
        }
        if (!grok_.periodEnd.empty()) {
            const long long endUnix = Iso8601ToUnix(grok_.periodEnd);
            const std::wstring endText = endUnix > 0 ? FormatFullDateTime(endUnix) : grok_.periodEnd;
            RECT endRect = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 18));
            drawTextBlock(textFormatFoot_.Get(), std::wstring(LocalizeText(L"Until ", L"到期 ")) + endText, endRect, textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
            y += ScaleForDpi(hwnd_, 22);
        }
        }
    }
    if (provider_ != L"grok") {
    fillRect(MakeRect(clientRect.left + 2, clientRect.top + 3, clientRect.right + 2, clientRect.bottom + 3), shadow);
    fillRect(clientRect, background);
    drawRectBorder(clientRect, border);

    if (!snapshot_.success) {
        const bool loadFailed = !snapshot_.errorMessage.empty();
        RECT titleRect = MakeRect(clientRect.left + padX, clientRect.top + padY,
            clientRect.right - padX, clientRect.top + padY + ScaleForDpi(hwnd_, 28));
        drawTextBlock(textFormatMetricValue_.Get(),
            loadFailed
                ? LocalizeText(L"Failed to load usage data", L"加载用量失败")
                : LocalizeText(L"Loading usage data", L"正在加载用量信息"),
            titleRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        if (!snapshot_.errorMessage.empty()) {
            RECT errorRect = MakeRect(clientRect.left + padX, titleRect.bottom + ScaleForDpi(hwnd_, 6),
                clientRect.right - padX, clientRect.bottom - ScaleForDpi(hwnd_, 24));
            drawTextBlock(textFormatFoot_.Get(), snapshot_.errorMessage, errorRect, RGB(215, 73, 73),
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP, false);
        }
        const int failedScoresH = GetModelScoresPanelHeight();
        if (failedScoresH > 0) {
            drawModelScoresPanel(
                clientRect.bottom - ScaleForDpi(hwnd_, 18) - failedScoresH,
                clientRect.left + padX,
                clientRect.right - padX);
        }
        RECT versionRect = MakeRect(clientRect.left + padX, clientRect.bottom - ScaleForDpi(hwnd_, 18),
            clientRect.right - padX, clientRect.bottom - ScaleForDpi(hwnd_, 4));
        drawTextBlock(textFormatFoot_.Get(), GetVersionStatusText(true), versionRect, updateAvailable_ ? heroValue : textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
        return;
    }
    }

    auto estimateExhaustAt = [&](const UsageWindow& window) -> long long {
        if (window.usedPercent <= 0 || window.windowSeconds <= 0) {
            return 0;
        }
        const int elapsed = ClampInt(window.windowSeconds - window.resetAfterSeconds, 1, window.windowSeconds);
        const double secondsToExhaust =
            (static_cast<double>(window.remainingPercent) / static_cast<double>(window.usedPercent)) * elapsed;
        if (!std::isfinite(secondsToExhaust) || secondsToExhaust <= 0.0) {
            return 0;
        }
        return static_cast<long long>(std::time(nullptr)) + static_cast<long long>(std::llround(secondsToExhaust));
    };

    auto drawUsageBar = [&](int top,
                            const std::wstring& title,
                            const UsageWindow& window,
                            double expectedUsedPercent) {
        const int rowHeight = ScaleForDpi(hwnd_, 36);
        // Bar/background color by remaining% (100 green -> 0 red).
        const COLORREF barColor = ColorForRemainingPercent(window.remainingPercent, false);

        RECT titleRect = MakeRect(clientRect.left + padX, top, clientRect.right - padX - ScaleForDpi(hwnd_, 120),
            top + ScaleForDpi(hwnd_, 14));
        drawTextBlock(textFormatMetricLabel_.Get(), title, titleRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

        const std::wstring percentText = FormatPercent(window.remainingPercent);
        const std::wstring resetText = FormatDateTime(window.resetAtUnixSeconds);
        // Percent uses the same semi-bold primary style as the limit title.
        // Reset time stays secondary on the far right.
        RECT percentRect = MakeRect(
            clientRect.right - padX - ScaleForDpi(hwnd_, 150),
            top,
            clientRect.right - padX - ScaleForDpi(hwnd_, 78),
            top + ScaleForDpi(hwnd_, 14));
        RECT resetTimeRect = MakeRect(
            clientRect.right - padX - ScaleForDpi(hwnd_, 74),
            top,
            clientRect.right - padX,
            top + ScaleForDpi(hwnd_, 14));
        drawTextBlock(textFormatMetricLabel_.Get(), percentText, percentRect, textPrimary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        drawTextBlock(textFormatFoot_.Get(), resetText, resetTimeRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

        const std::wstring startText = window.hasStartAt ? FormatDateTime(window.startAtUnixSeconds) : L"--";
        const long long exhaustAt = estimateExhaustAt(window);
        const std::wstring etaText = exhaustAt > 0
            ? (std::wstring(LocalizeText(L"ETA ", L"预计用完 ")) + FormatDateTime(exhaustAt))
            : LocalizeText(L"ETA --", L"预计用完 --");
        RECT metaRect = MakeRect(clientRect.left + padX, top + ScaleForDpi(hwnd_, 13),
            clientRect.right - padX, top + ScaleForDpi(hwnd_, 25));
        const std::wstring meta =
            std::wstring(LocalizeText(L"Start ", L"开始 ")) + startText
            + L"  ·  " + std::wstring(LocalizeText(L"Reset ", L"重置 ")) + resetText
            + L"  ·  " + etaText;
        drawTextBlock(textFormatFoot_.Get(), meta, metaRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

        RECT track = MakeRect(clientRect.left + padX, top + ScaleForDpi(hwnd_, 27),
            clientRect.right - padX, top + ScaleForDpi(hwnd_, 27) + ScaleForDpi(hwnd_, 6));
        fillRect(track, trackColor);

        // Fill = remaining%, growing from the left.
        RECT fill = track;
        fill.right = fill.left + static_cast<int>(
            RectWidth(track) * ClampDouble(static_cast<double>(window.remainingPercent), 0.0, 100.0) / 100.0);
        if (fill.right > fill.left) {
            fillRect(fill, barColor);
        }

        // Black budget marker on remaining scale: expected remaining = 100 - expected used.
        // Left of marker = still above budget remaining; right of marker = burned past budget.
        const double expectedRemainingPercent = ClampDouble(100.0 - expectedUsedPercent, 0.0, 100.0);
        const int markerX = track.left + static_cast<int>(
            RectWidth(track) * expectedRemainingPercent / 100.0);
        fillRect(
            MakeRect(markerX - 1, track.top - ScaleForDpi(hwnd_, 2), markerX + 1, track.bottom + ScaleForDpi(hwnd_, 2)),
            budgetMarkerColor);
        return rowHeight;
    };

    if (provider_ != L"grok") {
    // Header: badge + email
    const int contentPadY = ScaleForDpi(hwnd_, 8);
    RECT badgeRect = MakeRect(clientRect.left + padX, clientRect.top + contentPadY,
        clientRect.left + padX + ScaleForDpi(hwnd_, 48), clientRect.top + contentPadY + ScaleForDpi(hwnd_, 18));
    fillRect(badgeRect, lightTheme_ ? RGB(236, 233, 255) : RGB(52, 46, 84));
    drawTextBlock(textFormatFoot_.Get(), L"Codex", badgeRect,
        lightTheme_ ? RGB(96, 74, 210) : RGB(190, 176, 255),
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

    const std::wstring emailText = !snapshot_.email.empty() ? snapshot_.email : L"--";
    RECT emailRect = MakeRect(badgeRect.right + ScaleForDpi(hwnd_, 8), badgeRect.top,
        clientRect.right - padX, badgeRect.bottom);
    drawTextBlock(textFormatMetricLabel_.Get(), emailText, emailRect, textPrimary,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

    // Plan row
    y = badgeRect.bottom + ScaleForDpi(hwnd_, 6);
    RECT planLabelRect = MakeRect(clientRect.left + padX, y, clientRect.left + padX + ScaleForDpi(hwnd_, 32), y + ScaleForDpi(hwnd_, 16));
    drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"Plan", L"套餐"), planLabelRect, textSecondary,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

    const std::wstring planText = FormatPlanDisplayName();
    const float planWidth = measureTextWidth(textFormatFoot_.Get(), planText) + ScaleForDpi(hwnd_, 12);
    RECT planPillRect = MakeRect(planLabelRect.right + ScaleForDpi(hwnd_, 4), y,
        planLabelRect.right + ScaleForDpi(hwnd_, 4) + static_cast<int>(std::ceil(planWidth)), y + ScaleForDpi(hwnd_, 16));
    fillRect(planPillRect, lightTheme_ ? RGB(255, 244, 214) : RGB(84, 64, 24));
    drawTextBlock(textFormatFoot_.Get(), planText, planPillRect,
        lightTheme_ ? RGB(176, 110, 12) : RGB(255, 206, 120),
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);

    const std::wstring planStartText = snapshot_.hasPlanStart ? FormatDateTime(snapshot_.planStartUnixSeconds) : L"--";
    const std::wstring planUntilText = snapshot_.hasPlanUntil ? FormatDateTime(snapshot_.planUntilUnixSeconds) : L"--";
    const std::wstring planDates =
        std::wstring(LocalizeText(L"Start ", L"开通 ")) + planStartText
        + L"  ·  " + std::wstring(LocalizeText(L"Until ", L"续期/到期 ")) + planUntilText;
    RECT planDatesRect = MakeRect(planPillRect.right + ScaleForDpi(hwnd_, 8), y,
        clientRect.right - padX, y + ScaleForDpi(hwnd_, 16));
    drawTextBlock(textFormatFoot_.Get(), planDates, planDatesRect, textSecondary,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

    y += ScaleForDpi(hwnd_, 18);
    }
    if (resetStatusEnabled_) {
        const long long resetUnix = Iso8601ToUnix(resetStatus_.whenText);
        const std::wstring resetWhen = resetUnix > 0 ? FormatFullDateTime(resetUnix) : resetStatus_.whenText;
        const std::wstring resetLine = resetStatus_.success
            ? (resetWhen + L"  " + resetStatus_.summary)
            : (resetStatus_.error.empty() ? std::wstring(L"reset status") : resetStatus_.error);
        resetLinkRect_ = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 16));
        drawTextBlock(textFormatFoot_.Get(), resetLine, resetLinkRect_, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        y += ScaleForDpi(hwnd_, 18);
    } else {
        resetLinkRect_ = {};
    }
    if (estimateEnabled_) {
        RECT estimateRect = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 16));
        drawTextBlock(textFormatFoot_.Get(), estimate_.summary.empty() ? std::wstring(L"weekly estimate") : estimate_.summary, estimateRect, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
        y += ScaleForDpi(hwnd_, 18);
    }

    // Reset credits inventory: header + one row per credit (screenshot style).
    const int creditCount = snapshot_.resetCredits.fetched
        ? static_cast<int>(snapshot_.resetCredits.availableCredits.size())
        : 0;
    const int creditRows = snapshot_.resetCredits.fetched
        ? std::max(1, creditCount)
        : 1;
    const int creditRowH = ScaleForDpi(hwnd_, 16);
    const int creditHeaderH = ScaleForDpi(hwnd_, 16);
    const int creditBoxH = ScaleForDpi(hwnd_, 4) + creditHeaderH + creditRows * creditRowH + ScaleForDpi(hwnd_, 2);
    RECT creditBox = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + creditBoxH);
    fillRect(creditBox, lightTheme_ ? RGB(248, 249, 248) : RGB(34, 39, 36));
    drawRectBorder(creditBox, border);

    RECT creditsTitleRect = MakeRect(creditBox.left + ScaleForDpi(hwnd_, 8), creditBox.top + ScaleForDpi(hwnd_, 2),
        creditBox.right - ScaleForDpi(hwnd_, 8), creditBox.top + ScaleForDpi(hwnd_, 2) + creditHeaderH);
    const std::wstring creditsTitle =
        std::wstring(LocalizeText(L"Manual reset expiry (local)", L"主动重置过期时间"))
        + L"  ·  "
        + std::to_wstring(snapshot_.resetCredits.fetched ? snapshot_.resetCredits.availableCount : 0)
        + LocalizeText(L" available", L" 张");
    drawTextBlock(textFormatFoot_.Get(), creditsTitle, creditsTitleRect, textSecondary,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);

    int creditY = creditsTitleRect.bottom;
    if (!snapshot_.resetCredits.fetched) {
        RECT row = MakeRect(creditBox.left + ScaleForDpi(hwnd_, 8), creditY,
            creditBox.right - ScaleForDpi(hwnd_, 8), creditY + creditRowH);
        drawTextBlock(textFormatFoot_.Get(),
            snapshot_.resetCredits.errorMessage.empty()
                ? LocalizeText(L"Unavailable", L"不可用")
                : snapshot_.resetCredits.errorMessage,
            row, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
    } else if (creditCount == 0) {
        RECT row = MakeRect(creditBox.left + ScaleForDpi(hwnd_, 8), creditY,
            creditBox.right - ScaleForDpi(hwnd_, 8), creditY + creditRowH);
        drawTextBlock(textFormatFoot_.Get(), LocalizeText(L"None available", L"暂无可用"), row, textSecondary,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
    } else {
        for (int i = 0; i < creditCount; ++i) {
            const RateLimitResetCredit& credit = snapshot_.resetCredits.availableCredits[static_cast<size_t>(i)];
            RECT left = MakeRect(creditBox.left + ScaleForDpi(hwnd_, 8), creditY,
                creditBox.left + ScaleForDpi(hwnd_, 72), creditY + creditRowH);
            RECT right = MakeRect(left.right, creditY, creditBox.right - ScaleForDpi(hwnd_, 8), creditY + creditRowH);
            const std::wstring indexText = language_ == Language::Chinese
                ? (L"第 " + std::to_wstring(i + 1) + L" 张")
                : (L"#" + std::to_wstring(i + 1));
            const std::wstring expiryText = credit.hasExpiry
                ? FormatFullDateTime(credit.expiresAtUnixSeconds)
                : LocalizeText(L"No expiry", L"无期限");
            drawTextBlock(textFormatFoot_.Get(), indexText, left, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
            drawTextBlock(textFormatFoot_.Get(), expiryText, right, textSecondary,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
            creditY += creditRowH;
        }
    }
    if (!resetCreditActionMessage_.empty()) {
        RECT actionMsg = MakeRect(creditBox.left + ScaleForDpi(hwnd_, 8), creditBox.bottom - ScaleForDpi(hwnd_, 2),
            creditBox.right - ScaleForDpi(hwnd_, 8), creditBox.bottom + ScaleForDpi(hwnd_, 12));
        drawTextBlock(textFormatFoot_.Get(), resetCreditActionMessage_, actionMsg,
            lightTheme_ ? RGB(176, 78, 18) : RGB(255, 186, 120),
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, true);
    }

    // Limit bars: hide lanes the API no longer returns (currently often weekly-only).
    y = creditBox.bottom + ScaleForDpi(hwnd_, 6);
    if (provider_ != L"grok" && snapshot_.fiveHour.available) {
        y += drawUsageBar(
            y,
            LocalizeText(L"5-hour limit", L"5 小时限额"),
            snapshot_.fiveHour,
            pace.fiveHourExpectedUsedPercent);
        y += ScaleForDpi(hwnd_, 4);
    }
    if (provider_ != L"grok" && snapshot_.weekly.available) {
        y += drawUsageBar(
            y,
            LocalizeText(L"Weekly limit", L"周限额"),
            snapshot_.weekly,
            pace.expectedUsedPercent);
    }

    if (featurePage_ != FeaturePage::None) {
        RECT panel = MakeRect(clientRect.left + padX, y, clientRect.right - padX, y + ScaleForDpi(hwnd_, 160));
        fillRect(panel, lightTheme_ ? RGB(248, 249, 248) : RGB(28, 32, 30));
        drawRectBorder(panel, border);
        if (sessionScan_.daily.empty()) {
            drawTextBlock(textFormatFoot_.Get(), sessionScan_.error.empty() ? LocalizeText(L"No local sessions", L"没有本机会话") : sessionScan_.error,
                panel, textSecondary, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP, false);
        } else if (featurePage_ == FeaturePage::Sessions) {
            std::wstring lines;
            for (const SessionRow& row : sessionScan_.recent) {
                lines += Utf8ToWide(row.day) + L"  " + row.name + L"  " + row.model + L"  " + row.status + L"\n";
            }
            drawTextBlock(textFormatFoot_.Get(), lines, panel, textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_WRAP, false);
        } else {
            const int count = static_cast<int>(sessionScan_.daily.size());
            long long maxTokens = 1;
            for (const UsageBucket& bucket : sessionScan_.daily) {
                maxTokens = std::max(maxTokens, bucket.tokens);
            }
            const int left = panel.left + ScaleForDpi(hwnd_, 8);
            const int right = panel.right - ScaleForDpi(hwnd_, 8);
            const int bottom = panel.bottom - ScaleForDpi(hwnd_, 16);
            const int top = panel.top + ScaleForDpi(hwnd_, 16);
            if (chartKind_ == ChartKind::Bar || chartKind_ == ChartKind::Heat) {
                const int slot = std::max(1, (right - left) / std::max(1, count));
                for (int i = 0; i < count; ++i) {
                    const int barH = static_cast<int>((bottom - top) * sessionScan_.daily[static_cast<size_t>(i)].tokens / maxTokens);
                    RECT bar = MakeRect(left + i * slot, bottom - barH, left + i * slot + std::max(1, slot - 2), bottom);
                    fillRect(bar, chartKind_ == ChartKind::Heat ? heroValue : RGB(90, 140, 120));
                }
            } else if (count > 1) {
                for (int i = 1; i < count; ++i) {
                    const int x0 = left + (right - left) * (i - 1) / (count - 1);
                    const int x1 = left + (right - left) * i / (count - 1);
                    const int y0 = bottom - static_cast<int>((bottom - top) * sessionScan_.daily[static_cast<size_t>(i - 1)].tokens / maxTokens);
                    const int y1 = bottom - static_cast<int>((bottom - top) * sessionScan_.daily[static_cast<size_t>(i)].tokens / maxTokens);
                    RECT segment = MakeRect(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 2, std::max(y0, y1) + 2);
                    fillRect(segment, heroValue);
                }
            }
        }
        y += ScaleForDpi(hwnd_, 166);
    }

    if (showModelScores_) {
        y += drawModelScoresPanel(y, clientRect.left + padX, clientRect.right - padX);
    }

    // Footer: version | refresh button | countdown. Reset credits live in the right-click menu.
    y += ScaleForDpi(hwnd_, 4);
    const int actionH = ScaleForDpi(hwnd_, 18);
    const int actionW = ScaleForDpi(hwnd_, 84);
    const int actionGap = ScaleForDpi(hwnd_, 6);
    const int footerRowTop = y;
    const int footerRowBottom = footerRowTop + actionH;
    const std::wstring footerLeft = GetVersionStatusText(true);
    const std::wstring footerRight = refreshInFlight_
        ? LocalizeText(L"Refreshing", L"刷新中")
        : FormatRefreshCountdown(refreshCountdownSeconds_);
    const int countdownW = std::max(
        ScaleForDpi(hwnd_, 48),
        static_cast<int>(std::ceil(measureTextWidth(textFormatFoot_.Get(), footerRight))) + ScaleForDpi(hwnd_, 4));
    RECT countdownRect = MakeRect(clientRect.right - padX - countdownW, footerRowTop,
        clientRect.right - padX, footerRowBottom);
    RECT refreshRect = MakeRect(countdownRect.left - actionGap - actionW, footerRowTop,
        countdownRect.left - actionGap, footerRowBottom);
    refreshButtonRect_ = refreshRect;
    RECT footerLeftRect = MakeRect(clientRect.left + padX, footerRowTop,
        refreshRect.left - actionGap, footerRowBottom);

    fillRect(refreshRect, lightTheme_ ? RGB(248, 249, 248) : RGB(40, 46, 42));
    drawRectBorder(refreshRect, border);
    drawTextBlock(textFormatFoot_.Get(),
        refreshInFlight_
            ? LocalizeText(L"Refreshing...", L"刷新中...")
            : LocalizeText(L"Refresh", L"刷新额度"),
        refreshRect, textPrimary,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
    drawTextBlock(textFormatFoot_.Get(), footerLeft, footerLeftRect, updateAvailable_ ? heroValue : textSecondary,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, true);
    drawTextBlock(textFormatFoot_.Get(), footerRight, countdownRect, textSecondary,
        DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP, false);
}

HMENU AppBarWindow::CreateContextMenuHandle() {
    HMENU menu = CreatePopupMenu();
    HMENU languageMenu = CreatePopupMenu();
    HMENU refreshIntervalMenu = CreatePopupMenu();
    HMENU displayModeMenu = CreatePopupMenu();
    HMENU rankingMenu = CreatePopupMenu();
    HMENU accountMenu = CreatePopupMenu();
    ReloadAccounts();
    const size_t accountCount = std::min(authMenuAccounts_.size(), static_cast<size_t>(kMaxAuthMenuAccounts));
    if (accountCount == 0) {
        AppendMenuW(accountMenu, MF_STRING | MF_GRAYED, 0, Tr(L"(none imported)", L"（尚未导入）", L"（尚未匯入）", L"(가져오지 않음)", L"（未取り込み）", L"（нет импорта）", L"(aucun import)"));
    } else {
        for (size_t i = 0; i < accountCount; ++i) {
            const AccountEntry& account = authMenuAccounts_[i];
            const std::wstring accountText = (account.provider == L"grok" ? L"Grok  " : L"Codex  ") + account.label;
            AppendMenuW(
                accountMenu,
                MF_STRING | (IsActiveAuth(account) ? MF_CHECKED : MF_UNCHECKED),
                kCommandAccountBase + static_cast<UINT>(i),
                accountText.c_str());
        }
        AppendMenuW(accountMenu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(accountMenu, MF_STRING, kCommandImportAccount, Tr(L"Import file...", L"从文件导入…", L"從檔案匯入…", L"파일에서 가져오기…", L"ファイルから取り込み…", L"Импорт из файла…", L"Importer un fichier…"));
    AppendMenuW(accountMenu, MF_STRING, kCommandPasteImport, Tr(L"Paste JSON...", L"粘贴 JSON…", L"貼上 JSON…", L"JSON 붙여넣기…", L"JSON を貼り付け…", L"Вставить JSON…", L"Coller le JSON…"));
    AppendMenuW(accountMenu, MF_STRING, kCommandBrowserLogin, Tr(L"Browser sign-in...", L"浏览器登录…", L"瀏覽器登入…", L"브라우저 로그인…", L"ブラウザでログイン…", L"Вход через браузер…", L"Connexion navigateur…"));
    AppendMenuW(accountMenu, MF_STRING, kCommandAlias, Tr(L"Rename...", L"重命名…", L"重新命名…", L"이름 바꾸기…", L"名前を変更…", L"Переименовать…", L"Renommer…"));
    AppendMenuW(accountMenu, MF_STRING, kCommandMoveUp, Tr(L"Move up", L"上移", L"上移", L"위로", L"上へ", L"Выше", L"Monter"));
    AppendMenuW(accountMenu, MF_STRING, kCommandMoveDown, Tr(L"Move down", L"下移", L"下移", L"아래로", L"下へ", L"Ниже", L"Descendre"));
    AppendMenuW(accountMenu, MF_STRING, kCommandDeleteAccount, Tr(L"Delete copy", L"删除副本", L"刪除副本", L"복사본 삭제", L"コピーを削除", L"Удалить копию", L"Supprimer la copie"));
    const bool launchAtStartup = IsLaunchAtStartupEnabled();
    const UINT alwaysOnTopMenuState = MF_STRING
        | ((alwaysOnTop_ || taskbarMode_) ? MF_CHECKED : MF_UNCHECKED)
        | (taskbarMode_ ? MF_GRAYED : 0);
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::English ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageEnglish, L"English");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::Chinese ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageChinese, L"简体中文");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::Traditional ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageTraditional, L"繁體中文");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::Korean ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageKorean, L"한국어");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::Japanese ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageJapanese, L"日本語");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::Russian ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageRussian, L"Русский");
    AppendMenuW(languageMenu, MF_STRING | (language_ == Language::French ? MF_CHECKED : MF_UNCHECKED),
        kCommandLanguageFrench, L"Français");
    AppendMenuW(refreshIntervalMenu, MF_STRING | (refreshIntervalSeconds_ == 60 ? MF_CHECKED : MF_UNCHECKED),
        kCommandRefreshInterval1Minute, LocalizeText(L"1 minute", L"1分钟"));
    AppendMenuW(refreshIntervalMenu, MF_STRING | (refreshIntervalSeconds_ == 180 ? MF_CHECKED : MF_UNCHECKED),
        kCommandRefreshInterval3Minutes, LocalizeText(L"3 minutes", L"3分钟"));
    AppendMenuW(refreshIntervalMenu, MF_STRING | (refreshIntervalSeconds_ == 300 ? MF_CHECKED : MF_UNCHECKED),
        kCommandRefreshInterval5Minutes, LocalizeText(L"5 minutes", L"5分钟"));
    AppendMenuW(refreshIntervalMenu, MF_STRING | (refreshIntervalSeconds_ == 600 ? MF_CHECKED : MF_UNCHECKED),
        kCommandRefreshInterval10Minutes, LocalizeText(L"10 minutes", L"10分钟"));
    AppendMenuW(refreshIntervalMenu, MF_STRING | (refreshIntervalSeconds_ == 1800 ? MF_CHECKED : MF_UNCHECKED),
        kCommandRefreshInterval30Minutes, LocalizeText(L"30 minutes", L"30分钟"));
    AppendMenuW(displayModeMenu, MF_STRING | (!simpleMode_ && !taskbarMode_ ? MF_CHECKED : MF_UNCHECKED),
        kCommandFullMode, LocalizeText(L"Full mode", L"完整模式"));
    AppendMenuW(displayModeMenu, MF_STRING | (simpleMode_ ? MF_CHECKED : MF_UNCHECKED),
        kCommandSimpleMode, LocalizeText(L"Simple mode", L"简单模式"));
    AppendMenuW(displayModeMenu, MF_STRING | (taskbarMode_ ? MF_CHECKED : MF_UNCHECKED),
        kCommandTaskbarMode, LocalizeText(L"Taskbar mode", L"任务栏模式"));
    AppendMenuW(rankingMenu, MF_STRING | (!showModelScores_ ? MF_CHECKED : MF_UNCHECKED),
        kCommandModelScoresOff, LocalizeText(L"Off", L"关闭"));
    AppendMenuW(rankingMenu, MF_STRING | (showModelScores_ && modelScoreKind_ == RadarMetricKind::SoftwareEngineering ? MF_CHECKED : MF_UNCHECKED),
        kCommandModelScoresSoftware, LocalizeText(L"Software engineering", L"软件工程能力"));
    AppendMenuW(rankingMenu, MF_STRING | (showModelScores_ && modelScoreKind_ == RadarMetricKind::VisualSpatial ? MF_CHECKED : MF_UNCHECKED),
        kCommandModelScoresVisual, LocalizeText(L"Visual-spatial", L"视觉空间能力"));

    const bool canResetCredit = snapshot_.success
        && snapshot_.resetCredits.fetched
        && snapshot_.resetCredits.availableCount > 0
        && !resetCreditInFlight_;
    HMENU providerMenu = CreatePopupMenu();
    AppendMenuW(providerMenu, MF_STRING | (provider_ == L"codex" ? MF_CHECKED : MF_UNCHECKED), kCommandProviderCodex, L"Codex");
    AppendMenuW(providerMenu, MF_STRING | (provider_ == L"grok" ? MF_CHECKED : MF_UNCHECKED), kCommandProviderGrok, L"Grok");
    HMENU detailMenu = CreatePopupMenu();
    AppendMenuW(detailMenu, MF_STRING | (featurePage_ == FeaturePage::None ? MF_CHECKED : MF_UNCHECKED), kCommandDetailOff, Tr(L"Hidden", L"隐藏", L"隱藏", L"숨김", L"非表示", L"Скрыть", L"Masquer"));
    AppendMenuW(detailMenu, MF_STRING | (featurePage_ == FeaturePage::Chart ? MF_CHECKED : MF_UNCHECKED), kCommandDetailChart, Tr(L"Usage chart", L"用量图", L"用量圖", L"사용량 차트", L"使用量グラフ", L"График", L"Graphique"));
    AppendMenuW(detailMenu, MF_STRING | (featurePage_ == FeaturePage::Sessions ? MF_CHECKED : MF_UNCHECKED), kCommandDetailSessions, Tr(L"Sessions", L"会话", L"工作階段", L"세션", L"セッション", L"Сессии", L"Sessions"));
    AppendMenuW(detailMenu, MF_STRING | (chartKind_ == ChartKind::Heat ? MF_CHECKED : MF_UNCHECKED), kCommandChartHeat, Tr(L"Heatmap", L"热力", L"熱力", L"히트맵", L"ヒートマップ", L"Тепловая карта", L"Carte de chaleur"));
    AppendMenuW(detailMenu, MF_STRING | (chartKind_ == ChartKind::Line ? MF_CHECKED : MF_UNCHECKED), kCommandChartLine, Tr(L"Line", L"折线", L"折線", L"선", L"折れ線", L"Линия", L"Courbe"));
    AppendMenuW(detailMenu, MF_STRING | (chartKind_ == ChartKind::Bar ? MF_CHECKED : MF_UNCHECKED), kCommandChartBar, Tr(L"Bar", L"柱状", L"柱狀", L"막대", L"棒", L"Столбцы", L"Barres"));
    AppendMenuW(detailMenu, MF_STRING | (usageRange_ == UsageRange::Today ? MF_CHECKED : MF_UNCHECKED), kCommandRangeToday, Tr(L"Today", L"今日", L"今日", L"오늘", L"今日", L"Сегодня", L"Aujourd'hui"));
    AppendMenuW(detailMenu, MF_STRING | (usageRange_ == UsageRange::Cycle ? MF_CHECKED : MF_UNCHECKED), kCommandRangeCycle, Tr(L"This cycle", L"本周期", L"本週期", L"이번 주기", L"今周期", L"Этот цикл", L"Ce cycle"));
    AppendMenuW(detailMenu, MF_STRING | (usageRange_ == UsageRange::PreviousCycle ? MF_CHECKED : MF_UNCHECKED), kCommandRangePrev, Tr(L"Previous cycle", L"上周期", L"上週期", L"이전 주기", L"前周期", L"Прошлый цикл", L"Cycle précédent"));
    AppendMenuW(detailMenu, MF_STRING | (usageRange_ == UsageRange::Month ? MF_CHECKED : MF_UNCHECKED), kCommandRangeMonth, Tr(L"This month", L"本月", L"本月", L"이번 달", L"今月", L"Этот месяц", L"Ce mois"));
    AppendMenuW(detailMenu, MF_STRING, kCommandRepairSessions, Tr(L"Repair session index", L"修复会话索引", L"修復工作階段索引", L"세션 인덱스 복구", L"セッション索引を修復", L"Починить индекс сессий", L"Réparer l'index"));
    HMENU proxyMenu = CreatePopupMenu();
    AppendMenuW(proxyMenu, MF_STRING | (proxy_.mode == ProxyConfig::Mode::System ? MF_CHECKED : MF_UNCHECKED), kCommandProxySystem, Tr(L"Follow system", L"跟随系统", L"跟隨系統", L"시스템 따름", L"システムに従う", L"Как в системе", L"Suivre le système"));
    AppendMenuW(proxyMenu, MF_STRING | (proxy_.mode == ProxyConfig::Mode::Http ? MF_CHECKED : MF_UNCHECKED), kCommandProxyHttp, L"HTTP");
    AppendMenuW(proxyMenu, MF_STRING | (proxy_.mode == ProxyConfig::Mode::Socks5 ? MF_CHECKED : MF_UNCHECKED), kCommandProxySocks, L"SOCKS5");
    AppendMenuW(proxyMenu, MF_STRING, kCommandProxyEdit, Tr(L"Edit proxy...", L"编辑代理…", L"編輯代理…", L"프록시 편집…", L"プロキシを編集…", L"Изменить прокси…", L"Modifier le proxy…"));
    AppendMenuW(proxyMenu, MF_STRING, kCommandProxyTest, Tr(L"Test connection", L"测试连接", L"測試連線", L"연결 테스트", L"接続テスト", L"Проверить связь", L"Tester la connexion"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(providerMenu), Tr(L"Provider", L"服务", L"服務", L"공급자", L"プロバイダ", L"Служба", L"Fournisseur"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(accountMenu), LocalizeText(L"Account", L"账号"));
    AppendMenuW(menu, MF_STRING, kCommandRefresh, LocalizeText(L"Refresh now", L"立即刷新"));
    AppendMenuW(menu, MF_STRING | (tokenRefreshInFlight_ ? MF_GRAYED : 0),
        kCommandRefreshToken, LocalizeText(L"Refresh token", L"刷新 Token"));
    AppendMenuW(menu, MF_STRING | (canResetCredit ? 0 : MF_GRAYED),
        kCommandResetCredit, LocalizeText(L"Reset credits...", L"重置额度…"));
    AppendMenuW(menu, MF_STRING | (resetStatusEnabled_ ? MF_CHECKED : MF_UNCHECKED), kCommandResetStatusToggle, Tr(L"Reset updates", L"重置公告", L"重置公告", L"리셋 공지", L"リセット告知", L"Сбросы", L"Annonces de reset"));
    AppendMenuW(menu, MF_STRING | (estimateEnabled_ ? MF_CHECKED : MF_UNCHECKED), kCommandEstimateToggle, Tr(L"Weekly estimate", L"周额度估算", L"週額度估算", L"주간 추정", L"週間見積もり", L"Оценка недели", L"Estimation hebdo"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(detailMenu), Tr(L"Usage detail", L"用量详情", L"用量詳情", L"사용량 상세", L"使用量の詳細", L"Подробности", L"Détail d'usage"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(proxyMenu), Tr(L"Proxy", L"代理", L"代理", L"프록시", L"プロキシ", L"Прокси", L"Proxy"));
    AppendMenuW(menu, MF_STRING, kCommandCheckVersion, LocalizeText(L"Check version", L"检查版本"));
    AppendMenuW(menu, MF_STRING, kCommandDownloadUpdate, Tr(L"Download update...", L"下载更新…", L"下載更新…", L"업데이트 다운로드…", L"更新をダウンロード…", L"Скачать обновление…", L"Télécharger la mise à jour…"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(refreshIntervalMenu), LocalizeText(L"Refresh interval", L"刷新间隔"));
    AppendMenuW(menu, MF_STRING | (launchAtStartup ? MF_CHECKED : MF_UNCHECKED),
        kCommandLaunchAtStartup, LocalizeText(L"Launch at startup", L"开机自启"));
    AppendMenuW(menu, alwaysOnTopMenuState, kCommandAlwaysOnTop, LocalizeText(L"Always on top", L"始终置顶"));
    AppendMenuW(menu, MF_STRING | (lockPosition_ ? MF_CHECKED : MF_UNCHECKED),
        kCommandLockPosition, LocalizeText(L"Lock position", L"固定位置"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(displayModeMenu), LocalizeText(L"Display mode", L"显示模式"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rankingMenu), LocalizeText(L"Smart ranking", L"智能评分排名"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu), LocalizeText(L"Language", L"语言"));
    AppendMenuW(menu, MF_STRING, kCommandResetPosition, LocalizeText(L"Reset widget position", L"重置组件位置"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, LocalizeText(L"Exit", L"退出"));

    return menu;
}

void AppBarWindow::HandleMenuCommand(UINT command) {
    const size_t accountCount = std::min(authMenuAccounts_.size(), static_cast<size_t>(kMaxAuthMenuAccounts));
    if (command == kCommandImportAccount) {
        ImportAccount();
    } else if (command == kCommandPasteImport) {
        PasteImport(provider_);
    } else if (command == kCommandAlias) {
        RenameActiveAccount();
    } else if (command == kCommandMoveUp) {
        MoveActiveAccount(-1);
    } else if (command == kCommandMoveDown) {
        MoveActiveAccount(1);
    } else if (command == kCommandDeleteAccount) {
        DeleteActiveAccount();
    } else if (command == kCommandBrowserLogin) {
        StartBrowserSignIn();
    } else if (command == kCommandProviderCodex || command == kCommandProviderGrok) {
        provider_ = command == kCommandProviderGrok ? L"grok" : L"codex";
        activeAuthId_.clear();
        SaveFeatureSettings();
        RequestRefresh(true);
    } else if (command == kCommandResetStatusToggle) {
        resetStatusEnabled_ = !resetStatusEnabled_;
        if (resetStatusEnabled_) {
            SetTimer(hwnd_, kResetStatusTimerId, static_cast<UINT>(resetStatusIntervalSeconds_ * 1000), nullptr);
            RequestResetStatus();
        } else {
            KillTimer(hwnd_, kResetStatusTimerId);
        }
        SaveFeatureSettings();
        FitWindowToContent();
    } else if (command == kCommandEstimateToggle) {
        estimateEnabled_ = !estimateEnabled_;
        SaveFeatureSettings();
        FitWindowToContent();
    } else if (command == kCommandDetailOff || command == kCommandDetailChart || command == kCommandDetailSessions) {
        featurePage_ = command == kCommandDetailChart ? FeaturePage::Chart
            : command == kCommandDetailSessions ? FeaturePage::Sessions
            : FeaturePage::None;
        if (simpleMode_ || taskbarMode_) {
            SetDisplayMode(false, false);
        }
        SaveFeatureSettings();
        if (featurePage_ != FeaturePage::None) {
            RequestSessionScan();
        }
        FitWindowToContent();
    } else if (command == kCommandChartHeat || command == kCommandChartLine || command == kCommandChartBar) {
        chartKind_ = command == kCommandChartLine ? ChartKind::Line
            : command == kCommandChartBar ? ChartKind::Bar
            : ChartKind::Heat;
        featurePage_ = FeaturePage::Chart;
        SaveFeatureSettings();
        RequestSessionScan();
        FitWindowToContent();
    } else if (command == kCommandRepairSessions) {
        RepairSessions();
    } else if (command == kCommandProxySystem || command == kCommandProxyHttp || command == kCommandProxySocks) {
        ApplyProxyMode(command == kCommandProxyHttp ? ProxyConfig::Mode::Http
            : command == kCommandProxySocks ? ProxyConfig::Mode::Socks5
            : ProxyConfig::Mode::System);
    } else if (command == kCommandProxyEdit) {
        ConfigureProxyServer();
    } else if (command == kCommandProxyTest) {
        TestProxy();
    } else if (command == kCommandDownloadUpdate) {
        DownloadAndStageUpdate();
    } else if (command == kCommandRangeToday || command == kCommandRangeCycle || command == kCommandRangePrev || command == kCommandRangeMonth) {
        usageRange_ = command == kCommandRangeToday ? UsageRange::Today
            : command == kCommandRangePrev ? UsageRange::PreviousCycle
            : command == kCommandRangeMonth ? UsageRange::Month
            : UsageRange::Cycle;
        SaveFeatureSettings();
        RequestSessionScan();
    } else if (command >= kCommandAccountBase && command < kCommandAccountBase + accountCount) {
        const AccountEntry& account = authMenuAccounts_[command - kCommandAccountBase];
        provider_ = account.provider.empty() ? L"codex" : account.provider;
        activeAuthId_ = account.id;
        SaveActiveAuth();
        SaveFeatureSettings();
        resetCreditConfirmStep_ = 0;
        KillTimer(hwnd_, kResetConfirmTimerId);
        resetCreditActionMessage_.clear();
        snapshot_ = {};
        grok_ = {};
        snapshot_.errorMessage = LocalizeText(L"Switching account...", L"正在切换账号...");
        InvalidateRect(hwnd_, nullptr, FALSE);
        RequestRefresh(true);
    } else if (command == kCommandRefresh) {
        RequestRefresh(true);
        if (showModelScores_) {
            RequestModelScoresRefresh(true);
        }
    } else if (command == kCommandRefreshToken) {
        RequestRefreshToken();
    } else if (command == kCommandResetCredit) {
        ArmOrConsumeResetCredit();
    } else if (command == kCommandCheckVersion) {
        RequestLatestReleaseCheck(true);
    } else if (command == kCommandRefreshInterval1Minute) {
        SetRefreshIntervalSeconds(60);
    } else if (command == kCommandRefreshInterval3Minutes) {
        SetRefreshIntervalSeconds(180);
    } else if (command == kCommandRefreshInterval5Minutes) {
        SetRefreshIntervalSeconds(300);
    } else if (command == kCommandRefreshInterval10Minutes) {
        SetRefreshIntervalSeconds(600);
    } else if (command == kCommandRefreshInterval30Minutes) {
        SetRefreshIntervalSeconds(1800);
    } else if (command == kCommandLaunchAtStartup) {
        SetLaunchAtStartupEnabled(!IsLaunchAtStartupEnabled());
    } else if (command == kCommandAlwaysOnTop) {
        alwaysOnTop_ = !alwaysOnTop_;
        UpdateWindowBounds(true);
        SaveSettings();
    } else if (command == kCommandLockPosition) {
        lockPosition_ = !lockPosition_;
        SaveSettings();
    } else if (command == kCommandFullMode) {
        SetDisplayMode(false, false);
    } else if (command == kCommandSimpleMode) {
        SetDisplayMode(true, false);
    } else if (command == kCommandTaskbarMode) {
        SetDisplayMode(false, true);
    } else if (command == kCommandModelScoresOff) {
        SetModelScoreMode(false, modelScoreKind_);
    } else if (command == kCommandModelScoresSoftware) {
        SetModelScoreMode(true, RadarMetricKind::SoftwareEngineering);
    } else if (command == kCommandModelScoresVisual) {
        SetModelScoreMode(true, RadarMetricKind::VisualSpatial);
    } else if (command == kCommandLanguageEnglish) {
        SetLanguage(Language::English);
    } else if (command == kCommandLanguageChinese) {
        SetLanguage(Language::Chinese);
    } else if (command == kCommandLanguageTraditional) {
        SetLanguage(Language::Traditional);
    } else if (command == kCommandLanguageKorean) {
        SetLanguage(Language::Korean);
    } else if (command == kCommandLanguageJapanese) {
        SetLanguage(Language::Japanese);
    } else if (command == kCommandLanguageRussian) {
        SetLanguage(Language::Russian);
    } else if (command == kCommandLanguageFrench) {
        SetLanguage(Language::French);
    } else if (command == kCommandResetPosition) {
        hasSavedRect_ = false;
        UpdateWindowBounds(false);
        SaveSettings();
    } else if (command == kCommandExit) {
        DestroyWindow(hwnd_);
    }
}

namespace {

struct StickyMenuState {
    AppBarWindow* window = nullptr;
    std::vector<MenuEntry> entries;
    int hover = -1;
    int scroll = 0;
    int itemHeight = 28;
};

constexpr wchar_t kStickyMenuClass[] = L"CodexUsageBarStickyMenu";

int StickyContentHeight(const StickyMenuState& state) {
    return static_cast<int>(state.entries.size()) * state.itemHeight;
}

void PaintStickyMenu(HWND hwnd, StickyMenuState& state) {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client = {};
    GetClientRect(hwnd, &client);
    HBRUSH background = CreateSolidBrush(RGB(28, 32, 30));
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(dc, font);
    int y = -state.scroll;
    for (size_t i = 0; i < state.entries.size(); ++i) {
        const MenuEntry& entry = state.entries[i];
        RECT row = {0, y, client.right, y + state.itemHeight};
        if (static_cast<int>(i) == state.hover && !entry.header && entry.enabled) {
            HBRUSH hover = CreateSolidBrush(RGB(52, 64, 58));
            FillRect(dc, &row, hover);
            DeleteObject(hover);
        }
        SetTextColor(dc, entry.header ? RGB(160, 170, 165) : entry.enabled ? RGB(236, 240, 236) : RGB(110, 116, 112));
        std::wstring text = entry.checked ? (L"● " + entry.text) : (entry.header ? entry.text : (L"   " + entry.text));
        DrawTextW(dc, text.c_str(), -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += state.itemHeight;
    }
    SelectObject(dc, oldFont);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK StickyMenuProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<StickyMenuState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    if (state == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    if (message == WM_PAINT) {
        PaintStickyMenu(hwnd, *state);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        state->scroll -= GET_WHEEL_DELTA_WPARAM(wParam) / 40;
        if (state->scroll < 0) {
            state->scroll = 0;
        }
        RECT client = {};
        GetClientRect(hwnd, &client);
        const int maxScroll = std::max(0, StickyContentHeight(*state) - static_cast<int>(client.bottom));
        if (state->scroll > maxScroll) {
            state->scroll = maxScroll;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        const int index = (GET_Y_LPARAM(lParam) + state->scroll) / state->itemHeight;
        state->hover = index >= 0 && index < static_cast<int>(state->entries.size()) ? index : -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_LBUTTONUP) {
        const int index = (GET_Y_LPARAM(lParam) + state->scroll) / state->itemHeight;
        if (index < 0 || index >= static_cast<int>(state->entries.size())) {
            return 0;
        }
        const MenuEntry entry = state->entries[static_cast<size_t>(index)];
        if (entry.header || !entry.enabled || entry.command == 0) {
            return 0;
        }
        if (entry.command == kCommandCloseMenu) {
            DestroyWindow(hwnd);
            return 0;
        }
        const bool closes = entry.command == kCommandImportAccount
            || entry.command == kCommandPasteImport
            || entry.command == kCommandAlias
            || entry.command == kCommandDeleteAccount
            || entry.command == kCommandProxyEdit
            || entry.command == kCommandDownloadUpdate
            || entry.command == kCommandExit;
        state->window->HandleMenuCommand(entry.command);
        if (closes || !IsWindow(hwnd)) {
            if (IsWindow(hwnd)) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        state->entries = state->window->BuildMenuEntries();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        state->window->SetStickyMenu(nullptr);
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

std::vector<MenuEntry> AppBarWindow::BuildMenuEntries() {
    std::vector<MenuEntry> entries;
    HMENU menu = CreateContextMenuHandle();
    std::function<void(HMENU, bool)> collect = [&](HMENU source, bool child) {
        const int count = GetMenuItemCount(source);
        for (int i = 0; i < count; ++i) {
            wchar_t text[256] = {};
            MENUITEMINFOW info = {};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_STRING;
            info.dwTypeData = text;
            info.cch = 255;
            if (!GetMenuItemInfoW(source, static_cast<UINT>(i), TRUE, &info)) {
                continue;
            }
            if ((info.fType & MFT_SEPARATOR) != 0) {
                continue;
            }
            MenuEntry entry;
            entry.text = text;
            entry.command = info.wID;
            entry.checked = (info.fState & MFS_CHECKED) != 0;
            entry.enabled = (info.fState & MFS_GRAYED) == 0;
            if (info.hSubMenu != nullptr) {
                entry.header = true;
                entry.command = 0;
                entries.push_back(entry);
                collect(info.hSubMenu, true);
                continue;
            }
            if (child) {
                entry.text = L"    " + entry.text;
            }
            entries.push_back(std::move(entry));
        }
    };
    MenuEntry closeEntry;
    closeEntry.command = kCommandCloseMenu;
    closeEntry.text = Tr(L"Close menu", L"关闭菜单", L"關閉選單", L"메뉴 닫기", L"メニューを閉じる", L"Закрыть меню", L"Fermer le menu");
    entries.push_back(std::move(closeEntry));
    collect(menu, false);
    DestroyMenu(menu);
    return entries;
}

void AppBarWindow::ShowContextMenu(POINT) {
    if (stickyMenu_ != nullptr && IsWindow(stickyMenu_)) {
        DestroyWindow(stickyMenu_);
        stickyMenu_ = nullptr;
    }
    if (taskbarMode_ || simpleMode_) {
        SetDisplayMode(false, false);
    }
    surface_ = Surface::Settings;
    FitWindowToContent();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring AppBarWindow::FormatDuration(int totalSeconds) const {
    const int days = totalSeconds / 86400;
    const int hours = (totalSeconds % 86400) / 3600;

    if (days > 0) {
        if (language_ == Language::Chinese) {
            return std::to_wstring(days) + L" 天 " + std::to_wstring(hours) + L" 小时";
        }
        return std::to_wstring(days) + L"d " + std::to_wstring(hours) + L"h";
    }
    const int minutes = (totalSeconds % 3600) / 60;
    if (language_ == Language::Chinese) {
        return std::to_wstring(hours) + L" 小时 " + std::to_wstring(minutes) + L" 分钟";
    }
    return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
}

std::wstring AppBarWindow::FormatRefreshCountdown(int totalSeconds) const {
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (language_ == Language::Chinese) {
        if (hours > 0) {
            return std::to_wstring(hours) + L"小时 " + std::to_wstring(minutes) + L"分";
        }
        if (minutes > 0) {
            return std::to_wstring(minutes) + L"分 " + std::to_wstring(seconds) + L"秒";
        }
        return std::to_wstring(seconds) + L"秒";
    }

    if (hours > 0) {
        return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
    }
    if (minutes > 0) {
        return std::to_wstring(minutes) + L"m " + std::to_wstring(seconds) + L"s";
    }
    return std::to_wstring(seconds) + L"s";
}

static long long Iso8601ToUnix(const std::wstring& text) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6) {
        return 0;
    }
    std::tm parts = {};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    const time_t utc = _mkgmtime(&parts);
    if (utc < 0) {
        return 0;
    }
    long long unix = static_cast<long long>(utc);
    if (text.find(L'Z') != std::wstring::npos) {
        return unix;
    }
    const size_t sign = text.find_first_of(L"+-", 19);
    if (sign != std::wstring::npos) {
        int offsetHour = 0;
        int offsetMinute = 0;
        if (swscanf_s(text.c_str() + sign, L"%d:%d", &offsetHour, &offsetMinute) >= 1) {
            const int magnitude = (offsetHour < 0 ? -offsetHour : offsetHour) * 3600 + offsetMinute * 60;
            unix -= offsetHour < 0 ? -magnitude : magnitude;
        }
    }
    return unix;
}

std::wstring AppBarWindow::FormatDateTime(long long unixSeconds) const {
    if (unixSeconds <= 0) {
        return L"--";
    }

    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm localTime = {};
    localtime_s(&localTime, &t);

    wchar_t buffer[64] = {};
    wcsftime(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%m/%d %H:%M", &localTime);
    return buffer;
}

std::wstring AppBarWindow::FormatFullDateTime(long long unixSeconds) const {
    if (unixSeconds <= 0) {
        return L"--";
    }

    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm localTime = {};
    localtime_s(&localTime, &t);

    wchar_t buffer[64] = {};
    // Full local datetime, e.g. 2026-08-01 05:28
    wcsftime(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%Y-%m-%d %H:%M", &localTime);
    return buffer;
}

std::wstring AppBarWindow::FormatClockTime(long long unixSeconds) const {
    if (unixSeconds <= 0) {
        return L"--";
    }

    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm localTime = {};
    localtime_s(&localTime, &t);

    wchar_t buffer[64] = {};
    wcsftime(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%H:%M:%S", &localTime);
    return buffer;
}

std::wstring AppBarWindow::FormatPercent(double value) const {
    return FormatNumber(value) + L"%";
}

std::wstring AppBarWindow::FormatPlanDisplayName() const {
    std::wstring plan = snapshot_.planType;
    if (plan.empty()) {
        return LocalizeText(L"Unknown", L"未知");
    }

    // Normalize casing: pro -> Pro
    for (wchar_t& ch : plan) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    if (!plan.empty()) {
        plan[0] = static_cast<wchar_t>(towupper(plan[0]));
    }

    // API currently only returns base plan_type (e.g. "pro"). Community tools
    // commonly label Pro tiers as 20x / 5x. Prefer explicit multiplier if the
    // plan string already contains it; otherwise map known Codex Pro display.
    const std::wstring lower = snapshot_.planType;
    if (lower.find(L"20") != std::wstring::npos || lower.find(L"20x") != std::wstring::npos) {
        return L"Pro 20x";
    }
    if (lower.find(L"5x") != std::wstring::npos || lower.find(L"5") != std::wstring::npos) {
        // Avoid mislabeling plain strings; only if 5x-like marker exists.
        if (lower.find(L"x") != std::wstring::npos) {
            return L"Pro 5x";
        }
    }

    if (_wcsicmp(snapshot_.planType.c_str(), L"pro") == 0) {
        // Default Codex Pro pool quota label used by quota dashboards.
        return L"Pro 20x";
    }
    if (_wcsicmp(snapshot_.planType.c_str(), L"plus") == 0) {
        return L"Plus";
    }
    if (_wcsicmp(snapshot_.planType.c_str(), L"team") == 0) {
        return L"Team";
    }
    if (_wcsicmp(snapshot_.planType.c_str(), L"enterprise") == 0) {
        return L"Enterprise";
    }
    if (_wcsicmp(snapshot_.planType.c_str(), L"free") == 0) {
        return L"Free";
    }
    return plan;
}

COLORREF AppBarWindow::ColorForRemainingPercent(int remainingPercent, bool forBackground) const {
    // 100 remaining = original green, 0 remaining = original soft red (not pure).
    // Original baselines from git UI:
    //   green bar: RGB(41, 185, 128) / dark RGB(84, 208, 154)
    //   red text:  RGB(189, 54, 31)  / dark RGB(255, 144, 120)
    //   soft bg:   green RGB(233, 248, 239) / red RGB(255, 240, 234)
    const int remaining = ClampInt(remainingPercent, 0, 100);
    const double t = 1.0 - (remaining / 100.0);  // 0 healthy -> 1 critical

    auto lerp = [](int a, int b, double x) {
        return static_cast<int>(std::lround(a + (b - a) * x));
    };

    if (forBackground) {
        if (lightTheme_) {
            // Soft card tints (not pure white/red/green).
            const int r = lerp(233, 255, t);
            const int g = lerp(248, 240, t);
            const int b = lerp(239, 234, t);
            return RGB(r, g, b);
        }
        const int r = lerp(27, 60, t);
        const int g = lerp(48, 34, t);
        const int b = lerp(36, 28, t);
        return RGB(r, g, b);
    }

    if (lightTheme_) {
        // Bar color: green -> amber -> soft red.
        const int r0 = 41, g0 = 185, b0 = 128;
        const int r1 = 214, g1 = 149, b1 = 57;
        const int r2 = 189, g2 = 54, b2 = 31;
        if (t <= 0.5) {
            const double u = t / 0.5;
            return RGB(lerp(r0, r1, u), lerp(g0, g1, u), lerp(b0, b1, u));
        }
        const double u = (t - 0.5) / 0.5;
        return RGB(lerp(r1, r2, u), lerp(g1, g2, u), lerp(b1, b2, u));
    }

    const int r0 = 84, g0 = 208, b0 = 154;
    const int r1 = 227, g1 = 165, b1 = 79;
    const int r2 = 255, g2 = 144, b2 = 120;
    if (t <= 0.5) {
        const double u = t / 0.5;
        return RGB(lerp(r0, r1, u), lerp(g0, g1, u), lerp(b0, b1, u));
    }
    const double u = (t - 0.5) / 0.5;
    return RGB(lerp(r1, r2, u), lerp(g1, g2, u), lerp(b1, b2, u));
}
