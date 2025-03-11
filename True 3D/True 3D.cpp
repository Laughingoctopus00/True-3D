// True 3D.cpp : Defines the entry point for the application.
//

#define NOMINMAX  // Prevents Windows from defining min and max macros
#define D3DX12_NO_STATE_OBJECT_HELPERS
#define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#define D3DX12_NO_INTERFACE_HELPERS

#include <windows.h>
#include <gdiplus.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <deque>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")

const int SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);
const int TARGET_FPS = 60;
const int FRAME_DELAY = 1000 / TARGET_FPS;

// Advanced configuration with more parameters
struct DepthIllusionConfig {
    // Basic settings
    float depth_intensity = 3.0f;      // Overall depth effect strength
    float edge_boost = 6.0f;           // Edge detection multiplier
    float base_shift = 10.0f;          // Base pixel displacement amount
    float perspective_strength = 1.5f;  // Perspective effect (stronger at screen bottom)
    float phase = 0.0f;                // Current animation phase
    float phase_speed = 0.15f;         // Animation speed
    BYTE alpha = 180;                  // Global overlay transparency

    // Enhanced settings
    float vertical_shift = 3.0f;       // Vertical displacement amount
    float color_intensity = 1.2f;      // Color separation intensity
    float blur_radius = 1.5f;          // Depth-based blur amount
    float luminance_influence = 0.4f;  // How much brightness affects depth
    float texture_influence = 0.6f;    // How much texture detail affects depth
    float motion_factor = 0.8f;        // Motion detection influence
    float focus_distance = 0.5f;       // Normalized distance (0-1) for focus plane
    float focus_range = 0.3f;          // Range around focus distance that appears sharp

    // Dynamic animation
    float wave_amplitude = 1.2f;       // Amplitude of wave effect
    float wave_frequency = 0.003f;     // Frequency of wave pattern
    bool temporal_smoothing = true;    // Enable temporal smoothing
    int history_frames = 3;            // Number of frames to use for temporal smoothing
} dcfg;

template <typename T>
T clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}

// Smart pointer deleters for Windows GDI resources
struct ResourceDeleter {
    void operator()(HDC hdc) { if (hdc) DeleteDC(hdc); }
    void operator()(HBITMAP hbmp) { if (hbmp) DeleteObject(hbmp); }
    void operator()(Gdiplus::Bitmap* bmp) { if (bmp) delete bmp; }
};

using UniqueHDC = std::unique_ptr<std::remove_pointer<HDC>::type, ResourceDeleter>;
using UniqueBitmap = std::unique_ptr<std::remove_pointer<HBITMAP>::type, ResourceDeleter>;
using UniqueGdiplusBitmap = std::unique_ptr<Gdiplus::Bitmap, ResourceDeleter>;

// Enhanced depth analysis with temporal awareness
class AdvancedDepthGenerator {
public:
    AdvancedDepthGenerator() {
        // Initialize GDI+
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    }

    ~AdvancedDepthGenerator() {
        Gdiplus::GdiplusShutdown(gdiplusToken);
    }

    void Analyze(BYTE* pixels, int width, int height) {
        std::vector<std::vector<float>> currentDepthMap(height, std::vector<float>(width, 0.0f));
        std::vector<std::vector<float>> luminanceMap(height, std::vector<float>(width, 0.0f));
        std::vector<std::vector<float>> textureMap(height, std::vector<float>(width, 0.0f));

        // Extract luminance and perform advanced edge detection
        for (int y = 2; y < height - 2; y++) {
            for (int x = 2; x < width - 2; x++) {
                int offset = (y * width + x) * 4;

                // Calculate luminance
                float luminance = 0.299f * pixels[offset + 2] + // Red
                    0.587f * pixels[offset + 1] + // Green
                    0.114f * pixels[offset];      // Blue
                luminanceMap[y][x] = luminance / 255.0f;

                // Multi-scale edge detection
                CalculateEdgeStrength(pixels, width, height, x, y, textureMap);
            }
        }

        // Combine multiple cues for depth estimation
        for (int y = 2; y < height - 2; y++) {
            for (int x = 2; x < width - 2; x++) {
                float depthFromTexture = textureMap[y][x] * dcfg.texture_influence;
                float depthFromLuminance = (1.0f - luminanceMap[y][x]) * dcfg.luminance_influence;

                // Apply perspective bias (objects lower in frame tend to be closer)
                float perspectiveBias = (float)y / height * 0.2f;

                // Focus plane depth adjustment
                float normalizedDepth = depthFromTexture + depthFromLuminance + perspectiveBias;
                float focusAdjustment = 1.0f - std::min(
                    std::abs(normalizedDepth - dcfg.focus_distance) / dcfg.focus_range,
                    1.0f
                );

                // Store final depth value
                currentDepthMap[y][x] = clamp(normalizedDepth * focusAdjustment * dcfg.depth_intensity, 0.0f, 1.0f);
            }
        }

        // Temporal smoothing
        if (dcfg.temporal_smoothing && !depthHistory.empty()) {
            ApplyTemporalSmoothing(currentDepthMap, width, height);
        }

        // Add to history
        depthHistory.push_front(currentDepthMap);
        if (depthHistory.size() > dcfg.history_frames) {
            depthHistory.pop_back();
        }

        // Update current depth map
        depthMap = std::move(currentDepthMap);
    }

    std::vector<std::vector<float>> depthMap;

private:
    void CalculateEdgeStrength(BYTE* pixels, int width, int height, int x, int y,
        std::vector<std::vector<float>>& textureMap) {
        int offset = (y * width + x) * 4;
        float edge = 0;

        // Multi-kernel edge detection
        // 3x3 kernel (fine details)
        float edge1 = 0;
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                if (i == 0 && j == 0) continue;
                if (x + i >= 0 && x + i < width && y + j >= 0 && y + j < height) {
                    int neighbor = ((y + j) * width + (x + i)) * 4;
                    edge1 += abs(pixels[offset + 2] - pixels[neighbor + 2]) * 0.9f; // Red
                    edge1 += abs(pixels[offset + 1] - pixels[neighbor + 1]) * 1.0f; // Green
                    edge1 += abs(pixels[offset] - pixels[neighbor]) * 0.8f;         // Blue
                }
            }
        }

        // 5x5 kernel (medium details)
        float edge2 = 0;
        for (int i = -2; i <= 2; i++) {
            for (int j = -2; j <= 2; j++) {
                if (abs(i) <= 1 && abs(j) <= 1) continue; // Skip the inner 3x3
                if (x + i >= 0 && x + i < width && y + j >= 0 && y + j < height) {
                    int neighbor = ((y + j) * width + (x + i)) * 4;
                    edge2 += abs(pixels[offset + 2] - pixels[neighbor + 2]) * 0.7f; // Red
                    edge2 += abs(pixels[offset + 1] - pixels[neighbor + 1]) * 0.9f; // Green
                    edge2 += abs(pixels[offset] - pixels[neighbor]) * 0.6f;         // Blue
                }
            }
        }

        // Combine multi-scale edge information with weighting
        edge = (edge1 * 0.6f + edge2 * 0.4f) / 3000.0f * dcfg.edge_boost;
        textureMap[y][x] = clamp(pow(edge, 2.5f), 0.0f, 1.0f);
    }

    void ApplyTemporalSmoothing(std::vector<std::vector<float>>& currentMap, int width, int height) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float sum = currentMap[y][x];
                float weight = 1.0f;
                float totalWeight = 1.0f;

                // Blend with history
                int frame = 1;
                for (const auto& pastFrame : depthHistory) {
                    if (frame > dcfg.history_frames) break;

                    float frameWeight = 1.0f / (frame + 1);
                    sum += pastFrame[y][x] * frameWeight;
                    totalWeight += frameWeight;
                    frame++;
                }

                currentMap[y][x] = sum / totalWeight;
            }
        }
    }

    std::deque<std::vector<std::vector<float>>> depthHistory;
    ULONG_PTR gdiplusToken;
};

UniqueBitmap CaptureScreen(HDC hdc) {
    UniqueHDC hdcScreen(GetDC(NULL));
    UniqueHDC hdcMem(CreateCompatibleDC(hdcScreen.get()));
    UniqueBitmap hBitmap(CreateCompatibleBitmap(hdcScreen.get(), SCREEN_WIDTH, SCREEN_HEIGHT));

    SelectObject(hdcMem.get(), hBitmap.get());
    BitBlt(hdcMem.get(), 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, hdcScreen.get(), 0, 0, SRCCOPY);
    return hBitmap;
}

// Apply a simple Gaussian blur based on depth
void ApplyDepthBlur(BYTE* pixels, int width, int height, const std::vector<std::vector<float>>& depthMap) {
    std::vector<BYTE> tempBuffer(width * height * 4);
    memcpy(tempBuffer.data(), pixels, width * height * 4);

    // Simple gaussian-like blur with variable radius based on depth
    for (int y = 2; y < height - 2; y++) {
        for (int x = 2; x < width - 2; x++) {
            float depth = depthMap[y][x];
            int blurRadius = static_cast<int>(depth * dcfg.blur_radius);
            if (blurRadius == 0) continue;

            blurRadius = std::min(blurRadius, 3); // Limit blur radius

            float totalR = 0, totalG = 0, totalB = 0;
            float totalWeight = 0;

            for (int j = -blurRadius; j <= blurRadius; j++) {
                for (int i = -blurRadius; i <= blurRadius; i++) {
                    int nx = x + i;
                    int ny = y + j;

                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                    // Gaussian-like weight
                    float weight = exp(-(i * i + j * j) / (2.0f * blurRadius * blurRadius));

                    int offset = (ny * width + nx) * 4;
                    totalR += tempBuffer[offset + 2] * weight;
                    totalG += tempBuffer[offset + 1] * weight;
                    totalB += tempBuffer[offset] * weight;
                    totalWeight += weight;
                }
            }

            int offset = (y * width + x) * 4;
            pixels[offset + 2] = static_cast<BYTE>(totalR / totalWeight);
            pixels[offset + 1] = static_cast<BYTE>(totalG / totalWeight);
            pixels[offset] = static_cast<BYTE>(totalB / totalWeight);
        }
    }
}

UniqueBitmap CreateEnhancedDepthOverlay(HDC hdc, AdvancedDepthGenerator& depthGen) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCREEN_WIDTH;
    bmi.bmiHeader.biHeight = -SCREEN_HEIGHT;  // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits;
    UniqueBitmap hBitmap(CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0));

    auto hScreen = CaptureScreen(hdc);
    GetBitmapBits(hScreen.get(), SCREEN_WIDTH * SCREEN_HEIGHT * 4, pBits);

    BYTE* pixels = static_cast<BYTE*>(pBits);
    depthGen.Analyze(pixels, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Apply depth-based blur
    ApplyDepthBlur(pixels, SCREEN_WIDTH, SCREEN_HEIGHT, depthGen.depthMap);

    // Apply wave effect
    float time = dcfg.phase;

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            float depth = depthGen.depthMap[y][x];
            float perspective = 1.0f - (y / float(SCREEN_HEIGHT)) * dcfg.perspective_strength;

            // Wave effect
            float wave = sin(x * dcfg.wave_frequency + time) *
                cos(y * dcfg.wave_frequency * 0.7f + time * 0.8f) *
                dcfg.wave_amplitude * depth;

            // Combined displacements
            float shiftX = (dcfg.base_shift * depth * perspective + wave) * sin(dcfg.phase);
            float shiftY = (dcfg.vertical_shift * depth * perspective + wave * 0.7f) * cos(dcfg.phase);

            // Calculate adaptive focus effect
            float focusEffect = 1.0f;
            if (std::abs(depth - dcfg.focus_distance) > dcfg.focus_range) {
                focusEffect = 0.6f; // Areas outside focus range get more extreme effect
            }

            int srcX = x + static_cast<int>(shiftX * focusEffect);
            int srcY = y + static_cast<int>(shiftY * focusEffect);

            srcX = clamp(srcX, 0, SCREEN_WIDTH - 1);
            srcY = clamp(srcY, 0, SCREEN_HEIGHT - 1);

            int srcOffset = (srcY * SCREEN_WIDTH + srcX) * 4;
            int offset = (y * SCREEN_WIDTH + x) * 4;

            // Enhanced color separation (chromatic aberration)
            float colorSep = depth * dcfg.color_intensity;

            int redX = clamp(srcX + static_cast<int>(colorSep * 3.0f), 0, SCREEN_WIDTH - 1);
            int redY = srcY;
            int redOffset = (redY * SCREEN_WIDTH + redX) * 4;

            int blueX = clamp(srcX - static_cast<int>(colorSep * 3.0f), 0, SCREEN_WIDTH - 1);
            int blueY = srcY;
            int blueOffset = (blueY * SCREEN_WIDTH + blueX) * 4;

            // Apply chromatic aberration
            pixels[offset + 2] = static_cast<BYTE>(clamp(pixels[redOffset + 2] * (1.0f + colorSep * 0.5f), 0.0f, 255.0f)); // Red
            pixels[offset + 1] = pixels[srcOffset + 1]; // Green stays at source position
            pixels[offset + 0] = static_cast<BYTE>(clamp(pixels[blueOffset + 0] * (1.0f + colorSep * 0.3f), 0.0f, 255.0f)); // Blue

            // Depth-based transparency
            float depthAlpha = 0.3f + depth * 0.7f; // More transparent for areas with less depth
            pixels[offset + 3] = static_cast<BYTE>(dcfg.alpha * depthAlpha);
        }
    }

    dcfg.phase += dcfg.phase_speed;
    return hBitmap;
}

// Settings window handling
HWND g_hwndSettings = NULL;
std::mutex g_configMutex;
bool g_showSettings = false;

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        // Create sliders and controls for the settings window
        CreateWindow(L"BUTTON", L"Close", WS_VISIBLE | WS_CHILD,
            10, 10, 100, 30, hwnd, (HMENU)1001, NULL, NULL);

        // Add more controls for adjusting dcfg parameters
        // ...

        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) { // Close button
            g_showSettings = false;
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_CLOSE:
        g_showSettings = false;
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CreateSettingsWindow(HINSTANCE hInstance) {
    WNDCLASS wcSettings = { 0 };
    wcSettings.lpfnWndProc = SettingsProc;
    wcSettings.hInstance = hInstance;
    wcSettings.lpszClassName = L"DepthIllusionSettings";

    RegisterClass(&wcSettings);

    g_hwndSettings = CreateWindow(
        L"DepthIllusionSettings",
        L"3D Depth Illusion Settings",
        WS_OVERLAPPEDWINDOW,
        100, 100, 400, 600,
        NULL, NULL, hInstance, NULL);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_KEYDOWN:
    {
        std::lock_guard<std::mutex> lock(g_configMutex);

        // Real-time adjustments with more controls
        switch (wParam) {
        case VK_UP: dcfg.base_shift *= 1.1f; break;
        case VK_DOWN: dcfg.base_shift *= 0.9f; break;
        case VK_RIGHT: dcfg.phase_speed *= 1.1f; break;
        case VK_LEFT: dcfg.phase_speed *= 0.9f; break;
        case 'W': dcfg.vertical_shift *= 1.1f; break;
        case 'S': dcfg.vertical_shift *= 0.9f; break;
        case 'A': dcfg.color_intensity *= 0.9f; break;
        case 'D': dcfg.color_intensity *= 1.1f; break;
        case 'Q': dcfg.wave_amplitude *= 1.1f; break;
        case 'E': dcfg.wave_amplitude *= 0.9f; break;
        case 'Z': dcfg.focus_distance = std::max(0.0f, dcfg.focus_distance - 0.05f); break;
        case 'X': dcfg.focus_distance = std::min(1.0f, dcfg.focus_distance + 0.05f); break;
        case 'C': dcfg.focus_range *= 0.9f; break;
        case 'V': dcfg.focus_range *= 1.1f; break;

            // Toggle settings window
        case 'O':
            g_showSettings = !g_showSettings;
            ShowWindow(g_hwndSettings, g_showSettings ? SW_SHOW : SW_HIDE);
            break;

            // Save/load presets
        case '1': /* Save preset 1 */ break;
        case '2': /* Save preset 2 */ break;
        case '3': /* Load preset 1 */ break;
        case '4': /* Load preset 2 */ break;

        case VK_ESCAPE: PostQuitMessage(0); break;
        }
    }
    return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void RenderThreadFunc(HWND hwnd) {
    AdvancedDepthGenerator depthGen;
    UniqueHDC hdc(GetDC(hwnd));
    auto lastFrameTime = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFrameTime).count();

        if (elapsed >= FRAME_DELAY) {
            UniqueBitmap hBitmap;

            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                hBitmap = CreateEnhancedDepthOverlay(hdc.get(), depthGen);
            }

            UniqueHDC hdcMem(CreateCompatibleDC(hdc.get()));
            SelectObject(hdcMem.get(), hBitmap.get());

            POINT ptZero = { 0 };
            SIZE size = { SCREEN_WIDTH, SCREEN_HEIGHT };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            UpdateLayeredWindow(hwnd, hdc.get(), &ptZero, &size, hdcMem.get(),
                &ptZero, 0, &blend, ULW_ALPHA);

            lastFrameTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Register window class
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"EnhancedDepthIllusionOverlay";

    if (!RegisterClass(&wc)) return 0;

    // Create main overlay window
    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"EnhancedDepthIllusionOverlay",
        L"Enhanced 3D Depth Illusion (Press ESC to exit, O for settings)",
        WS_POPUP,
        0, 0,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd) return 0;

    // Create settings window
    CreateSettingsWindow(hInstance);

    // Configure and show the window
    SetLayeredWindowAttributes(hwnd, 0, dcfg.alpha, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Start render thread
    std::thread renderThread(RenderThreadFunc, hwnd);
    renderThread.detach();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}