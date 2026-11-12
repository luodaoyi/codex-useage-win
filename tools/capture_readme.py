"""Capture the real Windows GUI over a generated backdrop using offline preview data.

Run after a Release build: python tools/capture_readme.py
Requires Pillow. No credentials or existing application settings are used.
"""
import ctypes as C
import configparser
from ctypes import wintypes as W
import math
import os
from pathlib import Path
import subprocess
import shutil
import time

from PIL import Image, ImageChops, ImageDraw, ImageGrab, ImageStat

ROOT = Path(__file__).resolve().parents[1]
user, gdi = C.windll.user32, C.windll.gdi32
user.SetProcessDpiAwarenessContext(C.c_void_p(-4))
user.CreateWindowExW.restype = W.HWND
user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, W.LPVOID]
user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.restype = W.LPARAM
user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.IsWindowVisible.argtypes = [W.HWND]
gdi.CreateBitmap.restype = W.HBITMAP
gdi.CreateBitmap.argtypes = [C.c_int, C.c_int, W.UINT, W.UINT, W.LPVOID]
gdi.DeleteObject.argtypes = [W.HGDIOBJ]
user.DestroyWindow.argtypes = [W.HWND]

def pump(seconds):
    until = time.monotonic() + seconds
    msg = W.MSG()
    while time.monotonic() < until:
        while user.PeekMessageW(C.byref(msg), None, 0, 0, 1):
            user.TranslateMessage(C.byref(msg))
            user.DispatchMessageW(C.byref(msg))
        time.sleep(0.01)

def find_window(pid):
    found = []
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    @callback_type
    def visit(hwnd, _):
        value = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(value))
        name = C.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, 128)
        if value.value == pid and user.IsWindowVisible(hwnd) and name.value == "CodexUsageBarWindow":
            found.append(hwnd)
        return True
    user.EnumWindows(visit, 0)
    return found[0] if found else None

def scene(width, height):
    image = Image.new("RGB", (width, height))
    data = []
    for y in range(height):
        for x in range(width):
            a = math.exp(-((x / width - 0.17) ** 2 + (y / height - 0.20) ** 2) * 8)
            b = math.exp(-((x / width - 0.84) ** 2 + (y / height - 0.72) ** 2) * 7)
            data.append((int(45 + 90*a + 115*b), int(70 + 125*a + 40*b), int(120 + 90*a + 90*b)))
    image.putdata(data)
    draw = ImageDraw.Draw(image)
    for x in range(-height, width, 70):
        draw.line((x, 0, x + height, height), fill=(155, 185, 219), width=2)
    draw.ellipse((width // 2 - 180, height // 2 - 180, width // 2 + 180, height // 2 + 180), outline=(220, 205, 238), width=26)
    draw.text((28, 22), "CodexUsageBar  /  Live glass preview  /  Sample data", fill=(235, 241, 255), font_size=18)
    return image

def verify_saved_appearance():
    isolated = ROOT / "build/appearance-check"
    isolated.mkdir(exist_ok=True)
    executable = isolated / "CodexUsageBar.exe"
    shutil.copy2(ROOT / "build/Release/CodexUsageBar.exe", executable)
    settings = isolated / "settings.ini"
    settings.write_text("[appearance]\nfrosted=1\nstrength=75\n[features]\nlanguage=1\nreset_status_enabled=0\nestimate_enabled=0\n", encoding="utf-8")
    for restart in (False, True):
        process = subprocess.Popen([str(executable)])
        hwnd = None
        try:
            for _ in range(100):
                pump(0.05)
                hwnd = find_window(process.pid)
                if hwnd:
                    break
            assert hwnd, "Isolated application did not start"
            user.SendMessageW(hwnd, 0x7B, 0, -1)  # Settings via context menu
            pump(0.4)
            scale = user.GetDpiForWindow(hwnd) / 96
            if not restart:
                x, y = int(60 * scale), int(88 * scale)
                user.SendMessageW(hwnd, 0x201, 1, (y << 16) | x)
                user.SendMessageW(hwnd, 0x202, 0, (y << 16) | x)
            user.SendMessageW(hwnd, 0x100, 9, 0)  # Tab focuses the slider
            user.SendMessageW(hwnd, 0x100, 0x25 if restart else 0x23, 0)  # Left / End
            pump(0.2)
        finally:
            if hwnd:
                user.SendMessageW(hwnd, 0x10, 0, 0)
            process.wait(timeout=5)
        saved = configparser.ConfigParser()
        saved.read(settings, encoding="utf-8")
        assert saved.getint("appearance", "frosted") == 0, "Material selection did not persist"
        assert saved.getint("appearance", "strength") == (99 if restart else 100), "Slider value did not persist or reload"
    print("Verified material and strength persistence across a real application restart", flush=True)

def main():
    compilers = sorted((Path(os.environ["ProgramFiles(x86)"]) / "Windows Kits/10/bin").glob("*/x64/fxc.exe"))
    if not compilers:
        raise RuntimeError("Windows SDK shader compiler is required")
    shader = ROOT / "build/GlassShader.hlsl"
    shader.write_text((ROOT / "src/GlassShader.h").read_text(encoding="utf-8").split('R"hlsl(', 1)[1].split(')hlsl"', 1)[0], encoding="utf-8")
    for entry, profile in [("VS", "vs_4_0"), ("PS", "ps_4_0"), ("CopyPS", "ps_4_0")]:
        subprocess.run([str(compilers[-1]), "/nologo", "/T", profile, "/E", entry, "/Fo", str(ROOT / f"build/{entry}.cso"), str(shader)], check=True, capture_output=True)
    print("Verified all three shader entry points", flush=True)
    screen_w, screen_h = user.GetSystemMetrics(0), user.GetSystemMetrics(1)
    width, height = min(1500, screen_w - 80), min(1100, screen_h - 100)
    image = scene(width, height)
    pixels = image.convert("RGBA").tobytes("raw", "BGRA")
    buffer = C.create_string_buffer(pixels)
    bitmap = gdi.CreateBitmap(width, height, 1, 32, buffer)
    backdrop = user.CreateWindowExW(0x80, "STATIC", "Glass preview backdrop", 0x9000000E,
        30, 30, width, height, None, None, None, None)
    if not backdrop or not bitmap:
        raise C.WinError()
    user.SendMessageW(backdrop, 0x172, 0, bitmap)  # STM_SETIMAGE / IMAGE_BITMAP
    user.SetWindowPos(backdrop, W.HWND(-1), 30, 30, width, height, 0x40)
    user.UpdateWindow(backdrop)
    pump(0.3)
    output = ROOT / "IMG"
    output.mkdir(exist_ok=True)
    cases = [
        ("1.png", "--radar"),
        ("2.png", "--simple --liquid"),
        ("3.png", "--radar --liquid"),
        ("4.png", "--simple"),
        ("standard-en.png", "--radar --english"),
        ("glass-settings.png", "--settings --liquid"),
        ("glass-dark.png", "--radar --liquid --dark"),
        ("glass-taskbar.png", "--taskbar --liquid"),
    ]
    try:
        for name, options in cases:
            process = subprocess.Popen([str(ROOT / "build/Release/CodexUsageBar.exe"), "--preview", *options.split()])
            hwnd = None
            try:
                for _ in range(100):
                    pump(0.05)
                    hwnd = find_window(process.pid)
                    if hwnd:
                        break
                    if process.poll() is not None:
                        raise RuntimeError(f"GUI exited: {process.returncode}")
                if not hwnd:
                    raise RuntimeError("Preview window did not appear")
                rect = W.RECT()
                user.GetWindowRect(hwnd, C.byref(rect))
                w, h = rect.right - rect.left, rect.bottom - rect.top
                x = 30 + (width - w) // 2
                y = 30 + max(60, (height - h) // 2)
                user.SetWindowPos(hwnd, W.HWND(-1), x, y, w, h, 0x40)
                pump(1.5)
                if process.poll() is not None:
                    raise RuntimeError("Preview stopped while rendering")
                user.GetWindowRect(hwnd, C.byref(rect))
                bounds = (max(0, rect.left - 24), max(0, rect.top - 24),
                    min(screen_w, rect.right + 24), min(screen_h, rect.bottom + 24))
                shot = ImageGrab.grab(bbox=bounds, all_screens=True)
                shot.save(output / name)
                print(f"Captured {name}: {shot.size}", flush=True)
                if name in ("3.png", "glass-dark.png"):
                    pixel = shot.getpixel((24 + w // 2, 24 + 12))[:3]
                    def channel(v):
                        v /= 255
                        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
                    luma = sum(channel(v) * weight for v, weight in zip(pixel, (0.2126, 0.7152, 0.0722)))
                    assert luma > 0.62 if name == "3.png" else luma < 0.08, f"Glass luminance bound failed: {luma}"
                    print(f"Verified {'light' if name == '3.png' else 'dark'} glass luminance: {luma:.3f}", flush=True)
                if name == "glass-settings.png":
                    user.GetDpiForWindow.argtypes = [W.HWND]
                    scale = user.GetDpiForWindow(hwnd) / 96
                    slider_y = round(154 * scale)
                    left = int(14 * scale + 0.5) + int(10 * scale + 0.5)
                    right = w - left
                    def click(px, py):
                        user.SendMessageW(hwnd, 0x201, 1, (py << 16) | px)
                        user.SendMessageW(hwnd, 0x202, 0, (py << 16) | px)
                        pump(0.35)
                    click(left + 1, slider_y)
                    low = ImageGrab.grab(bbox=bounds, all_screens=True)
                    click(right - 1, slider_y)
                    high = ImageGrab.grab(bbox=bounds, all_screens=True)
                    track_box = (24 + left, 24 + round(130 * scale), 24 + right, 24 + round(168 * scale))
                    difference = sum(ImageStat.Stat(ImageChops.difference(low.crop(track_box), high.crop(track_box))).mean)
                    print(f"Slider control pixel difference: {difference:.2f}", flush=True)
                    assert difference > 2, "Effect slider did not change its rendered control"
                    user.SendMessageW(hwnd, 0x100, 0x24, 0)  # Home: 0%
                    pump(0.2)
                    keyboard_low = ImageGrab.grab(bbox=bounds, all_screens=True)
                    assert sum(ImageStat.Stat(ImageChops.difference(low, keyboard_low)).mean) < 0.5, "Keyboard slider endpoint differs from pointer endpoint"
                    user.SendMessageW(hwnd, 0x20A, ((-120) & 0xFFFF) << 16, 0)
                    pump(0.2)
                    scrolled = ImageGrab.grab(bbox=bounds, all_screens=True)
                    assert sum(ImageStat.Stat(ImageChops.difference(keyboard_low, scrolled)).mean) > 1, "Settings page did not scroll"
                    print("Verified live slider, keyboard endpoint and settings scrolling", flush=True)
            finally:
                if hwnd:
                    user.SendMessageW(hwnd, 0x10, 0, 0)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)
        verify_saved_appearance()
    finally:
        user.DestroyWindow(backdrop)
        gdi.DeleteObject(bitmap)

if __name__ == "__main__":
    main()
