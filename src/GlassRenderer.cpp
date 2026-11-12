#include "GlassRenderer.h"
#include "GlassShader.h"

#include <d3dcompiler.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
constexpr int kPadding = 64;
struct Parameters {
    float width, height, roiWidth, roiHeight;
    float originX, originY, radius, strength;
    float frosted, light, ready, dpi;
    float screenX, screenY, screenWidth, screenHeight;
    float rotation, padding[3];
};
static_assert(sizeof(Parameters) == 80);

}

bool GlassRenderer::ExcludeWindow(HWND window) {
    // Like Atrium, exclude only from Desktop Duplication when available.
    // WDA is the documented alternative, but also hides the widget in recordings.
    struct Data { DWORD attribute; PVOID value; SIZE_T size; };
    using SetAttribute = BOOL(WINAPI*)(HWND, Data*);
    auto set = reinterpret_cast<SetAttribute>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    BOOL enabled = TRUE;
    Data data{24, &enabled, sizeof(enabled)}; // WCA_EXCLUDED_FROM_DDA
    if (set && set(window, &data)) return true;
    return SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) != FALSE;
}

void GlassRenderer::Reset() {
    if (drawing_) drawing_->SetTarget(nullptr);
    canvas_.Reset(); view_.Reset(); backdropView_.Reset(); backdrop_.Reset();
    backdropTarget_.Reset(); desktop_.Reset(); desktopView_.Reset(); copy_.Reset();
    capture_.Reset(); output_.Reset(); drawing_.Reset(); visual_.Reset();
    target_.Reset(); composition_.Reset(); swap_.Reset(); vertex_.Reset();
    pixel_.Reset(); parameters_.Reset(); sampler_.Reset(); gpu_.Reset(); device_.Reset();
    width_ = height_ = 0;
    monitor_ = nullptr;
    hasBackdrop_ = false;
    retryAt_ = 0;
}

HRESULT GlassRenderer::Initialize(HWND window) {
    window_ = window;
    monitor_ = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    exclusion_ = ExcludeWindow(window);
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIAdapter1> selected;
    for (UINT a = 0; !output_; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(a, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return hr;
        for (UINT n = 0; ; ++n) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(n, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) return hr;
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor_) {
                hr = output.As(&output_);
                if (FAILED(hr)) return hr;
                selected = adapter;
                monitorRect_ = desc.DesktopCoordinates;
                break;
            }
        }
    }
    if (!selected) return DXGI_ERROR_NOT_FOUND;
    hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &gpu_);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIDevice> dxgi;
    hr = device_.As(&dxgi);
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1Factory1> d2d;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2d));
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1Device> drawingDevice;
    hr = d2d->CreateDevice(dxgi.Get(), &drawingDevice);
    if (FAILED(hr)) return hr;
    hr = drawingDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &drawing_);
    if (FAILED(hr)) return hr;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = desc.Height = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ComPtr<IDXGIFactory2> factory2;
    hr = factory.As(&factory2);
    if (FAILED(hr)) return hr;
    hr = factory2->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_);
    if (FAILED(hr)) return hr;
    hr = DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition_));
    if (FAILED(hr)) return hr;
    hr = composition_->CreateTargetForHwnd(window, FALSE, &target_);
    if (FAILED(hr)) return hr;
    hr = composition_->CreateVisual(&visual_);
    if (FAILED(hr)) return hr;
    hr = visual_->SetContent(swap_.Get());
    if (FAILED(hr)) return hr;
    hr = target_->SetRoot(visual_.Get());
    if (FAILED(hr)) return hr;
    hr = composition_->Commit();
    if (FAILED(hr)) return hr;
    ComPtr<ID3DBlob> vs, ps, errors;
    hr = D3DCompile(kGlassShader, sizeof(kGlassShader) - 1, "GlassShader", nullptr, nullptr,
        "VS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &errors);
    if (FAILED(hr)) return hr;
    hr = D3DCompile(kGlassShader, sizeof(kGlassShader) - 1, "GlassShader", nullptr, nullptr,
        "PS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
    if (FAILED(hr)) return hr;
    hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_);
    if (FAILED(hr)) return hr;
    hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_);
    if (FAILED(hr)) return hr;
    ps.Reset();
    hr = D3DCompile(kGlassShader, sizeof(kGlassShader) - 1, "GlassShader", nullptr, nullptr,
        "CopyPS", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
    if (FAILED(hr)) return hr;
    hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &copy_);
    if (FAILED(hr)) return hr;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Parameters);
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device_->CreateBuffer(&cb, nullptr, &parameters_);
    if (FAILED(hr)) return hr;
    D3D11_SAMPLER_DESC sample{};
    sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sample.MaxLOD = D3D11_FLOAT32_MAX;
    return device_->CreateSamplerState(&sample, &sampler_);
}

HRESULT GlassRenderer::Resize(int width, int height) {
    drawing_->SetTarget(nullptr);
    canvas_.Reset(); view_.Reset(); backdropView_.Reset(); backdropTarget_.Reset(); backdrop_.Reset();
    gpu_->OMSetRenderTargets(0, nullptr, nullptr);
    gpu_->Flush();
    HRESULT hr = swap_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return hr;
    ComPtr<ID3D11Texture2D> back;
    hr = swap_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr)) return hr;
    hr = device_->CreateRenderTargetView(back.Get(), nullptr, &view_);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGISurface> surface;
    hr = back.As(&surface);
    if (FAILED(hr)) return hr;
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = drawing_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &canvas_);
    if (FAILED(hr)) return hr;
    drawing_->SetTarget(canvas_.Get());
    D3D11_TEXTURE2D_DESC tex{};
    tex.Width = width + 2 * kPadding;
    tex.Height = height + 2 * kPadding;
    tex.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex.ArraySize = 1;
    tex.SampleDesc.Count = 1;
    tex.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    tex.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    hr = device_->CreateTexture2D(&tex, nullptr, &backdrop_);
    if (FAILED(hr)) return hr;
    hr = device_->CreateShaderResourceView(backdrop_.Get(), nullptr, &backdropView_);
    if (FAILED(hr)) return hr;
    D3D11_RENDER_TARGET_VIEW_DESC roi{};
    roi.Format = tex.Format;
    roi.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = device_->CreateRenderTargetView(backdrop_.Get(), &roi, &backdropTarget_);
    if (FAILED(hr)) return hr;
    width_ = width; height_ = height;
    hasBackdrop_ = false;
    return S_OK;
}

HRESULT GlassRenderer::Ensure(HWND window, int width, int height) {
    if (width <= 0 || height <= 0) return E_INVALIDARG;
    if (monitor_ != MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)) Reset();
    if (!device_) {
        HRESULT hr = Initialize(window);
        if (FAILED(hr)) { Reset(); return hr; }
    }
    if (width != width_ || height != height_) return Resize(width, height);
    return S_OK;
}

HRESULT GlassRenderer::Capture() {
    if (!exclusion_) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    if (!capture_) {
        if (GetTickCount64() < retryAt_) return captureStatus_;
        HRESULT hr = output_->DuplicateOutput(device_.Get(), &capture_);
        if (FAILED(hr)) { retryAt_ = GetTickCount64() + 2000; return hr; }
    }
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = capture_->AcquireNextFrame(0, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return S_FALSE;
    if (FAILED(hr)) {
        capture_.Reset(); hasBackdrop_ = false; desktop_.Reset(); desktopView_.Reset();
        retryAt_ = GetTickCount64() + 1000;
        return hr;
    }
    ComPtr<ID3D11Texture2D> frame;
    hr = resource.As(&frame);
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC desc{};
        frame->GetDesc(&desc);
        DXGI_OUTDUPL_DESC captureDesc{};
        capture_->GetDesc(&captureDesc);
        rotation_ = captureDesc.Rotation;
        if (!desktop_) {
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = desc.CPUAccessFlags = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            hr = device_->CreateTexture2D(&desc, nullptr, &desktop_);
            if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(desktop_.Get(), nullptr, &desktopView_);
        }
        if (SUCCEEDED(hr)) gpu_->CopyResource(desktop_.Get(), frame.Get());
    }
    HRESULT release = capture_->ReleaseFrame();
    return FAILED(hr) ? hr : release;
}

HRESULT GlassRenderer::DrawBackdrop(bool frosted, int strength, bool light) {
    captureStatus_ = Capture();
    const float dpi = GetDpiForWindow(window_) / 96.0f;
    POINT position{};
    ClientToScreen(window_, &position);
    Parameters values{static_cast<float>(width_), static_cast<float>(height_),
        static_cast<float>(width_ + 2 * kPadding), static_cast<float>(height_ + 2 * kPadding),
        kPadding, kPadding, std::min(22.0f * dpi, std::min(width_, height_) * 0.5f - 1),
        std::clamp(strength, 0, 100) / 100.0f, frosted ? 1.f : 0.f, light ? 1.f : 0.f,
        desktopView_ ? 1.f : 0.f, dpi,
        static_cast<float>(position.x - monitorRect_.left - kPadding),
        static_cast<float>(position.y - monitorRect_.top - kPadding),
        static_cast<float>(monitorRect_.right - monitorRect_.left),
        static_cast<float>(monitorRect_.bottom - monitorRect_.top), static_cast<float>(rotation_), {}};
    gpu_->UpdateSubresource(parameters_.Get(), 0, nullptr, &values, 0, 0);
    const float clear[4]{};
    gpu_->ClearRenderTargetView(view_.Get(), clear);
    ID3D11RenderTargetView* view = view_.Get();
    gpu_->OMSetRenderTargets(1, &view, nullptr);
    gpu_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    gpu_->RSSetState(nullptr);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    gpu_->RSSetViewports(1, &viewport);
    gpu_->IASetInputLayout(nullptr);
    gpu_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu_->VSSetShader(vertex_.Get(), nullptr, 0);
    ID3D11Buffer* parameters = parameters_.Get();
    gpu_->PSSetConstantBuffers(0, 1, &parameters);
    ID3D11SamplerState* sampler = sampler_.Get();
    gpu_->PSSetSamplers(0, 1, &sampler);
    ID3D11ShaderResourceView* empty = nullptr;
    if (desktopView_) {
        ID3D11RenderTargetView* roi = backdropTarget_.Get();
        gpu_->OMSetRenderTargets(1, &roi, nullptr);
        viewport.Width = values.roiWidth; viewport.Height = values.roiHeight;
        gpu_->RSSetViewports(1, &viewport);
        gpu_->PSSetShader(copy_.Get(), nullptr, 0);
        ID3D11ShaderResourceView* desktop = desktopView_.Get();
        gpu_->PSSetShaderResources(0, 1, &desktop);
        gpu_->Draw(3, 0);
        gpu_->PSSetShaderResources(0, 1, &empty);
        gpu_->OMSetRenderTargets(0, nullptr, nullptr);
        gpu_->GenerateMips(backdropView_.Get());
        hasBackdrop_ = true;
    }
    gpu_->OMSetRenderTargets(1, &view, nullptr);
    viewport.Width = values.width; viewport.Height = values.height;
    gpu_->RSSetViewports(1, &viewport);
    gpu_->PSSetShader(pixel_.Get(), nullptr, 0);
    ID3D11ShaderResourceView* background = backdropView_.Get();
    gpu_->PSSetShaderResources(0, 1, &background);
    gpu_->Draw(3, 0);
    gpu_->PSSetShaderResources(0, 1, &empty);
    return device_->GetDeviceRemovedReason();
}

HRESULT GlassRenderer::Present() {
    return swap_->Present(1, 0);
}
