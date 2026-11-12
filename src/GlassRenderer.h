#pragma once

#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// The UI and glass share a premultiplied composition surface. Desktop pixels
// stay on the GPU; only the padded region under the widget is copied.
class GlassRenderer {
public:
    static bool ExcludeWindow(HWND window);
    HRESULT Ensure(HWND window, int width, int height);
    HRESULT DrawBackdrop(bool frosted, int strength, bool light);
    HRESULT Present();
    void Reset();
    ID2D1DeviceContext* Context() const { return drawing_.Get(); }
    bool HasCapture() const { return hasBackdrop_; }
    HRESULT CaptureStatus() const { return captureStatus_; }

private:
    HRESULT Initialize(HWND window);
    HRESULT Resize(int width, int height);
    HRESULT Capture();
    HWND window_ = nullptr;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    int width_ = 0, height_ = 0;
    bool hasBackdrop_ = false;
    bool exclusion_ = false;
    HRESULT captureStatus_ = S_OK;
    ULONGLONG retryAt_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> gpu_;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> capture_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> drawing_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> canvas_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backdrop_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktop_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> desktopView_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> backdropView_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> backdropTarget_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copy_;
    DXGI_MODE_ROTATION rotation_ = DXGI_MODE_ROTATION_IDENTITY;
    Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};
