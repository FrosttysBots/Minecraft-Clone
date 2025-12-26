// Voxel Engine Launcher
// Simple Win32 dialog-based launcher

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#include "../core/Config.h"
#include <iostream>
#include <string>
#include <sstream>

// Control IDs
#define ID_RESOLUTION_COMBO     101
#define ID_FULLSCREEN_CHECK     102
#define ID_VSYNC_CHECK          103
#define ID_RENDER_DIST_SLIDER   104
#define ID_RENDER_DIST_LABEL    105
#define ID_FOV_SLIDER           106
#define ID_FOV_LABEL            107
#define ID_CHUNK_SPEED_SLIDER   108
#define ID_CHUNK_SPEED_LABEL    109
#define ID_GPU_CHECK            110
#define ID_PLAY_BUTTON          111
#define ID_SAVE_BUTTON          112
#define ID_QUIT_BUTTON          113
#define ID_SENSITIVITY_SLIDER   114
#define ID_SENSITIVITY_LABEL    115
#define ID_RENDERER_COMBO       116

// Resolution options
struct Resolution {
    int width;
    int height;
    const char* name;
};

const Resolution resolutions[] = {
    {1280, 720, "1280 x 720 (720p)"},
    {1366, 768, "1366 x 768"},
    {1600, 900, "1600 x 900"},
    {1920, 1080, "1920 x 1080 (1080p)"},
    {2560, 1440, "2560 x 1440 (1440p)"},
    {3840, 2160, "3840 x 2160 (4K)"},
};
const int numResolutions = sizeof(resolutions) / sizeof(resolutions[0]);

// Global handles
HWND g_hWnd = NULL;
HWND g_hResolutionCombo = NULL;
HWND g_hFullscreenCheck = NULL;
HWND g_hVsyncCheck = NULL;
HWND g_hRenderDistSlider = NULL;
HWND g_hRenderDistLabel = NULL;
HWND g_hFovSlider = NULL;
HWND g_hFovLabel = NULL;
HWND g_hChunkSpeedSlider = NULL;
HWND g_hChunkSpeedLabel = NULL;
HWND g_hGpuCheck = NULL;
HWND g_hSensitivitySlider = NULL;
HWND g_hSensitivityLabel = NULL;
HWND g_hRendererCombo = NULL;
bool g_shouldLaunch = false;

// Get current resolution index
int getCurrentResolutionIndex() {
    for (int i = 0; i < numResolutions; i++) {
        if (resolutions[i].width == g_config.windowWidth &&
            resolutions[i].height == g_config.windowHeight) {
            return i;
        }
    }
    return 0;
}

// Update label text
void updateSliderLabels() {
    char buf[64];

    int renderDist = (int)SendMessage(g_hRenderDistSlider, TBM_GETPOS, 0, 0);
    sprintf_s(buf, "Render Distance: %d chunks", renderDist);
    SetWindowTextA(g_hRenderDistLabel, buf);

    int fov = (int)SendMessage(g_hFovSlider, TBM_GETPOS, 0, 0);
    sprintf_s(buf, "Field of View: %d", fov);
    SetWindowTextA(g_hFovLabel, buf);

    int chunkSpeed = (int)SendMessage(g_hChunkSpeedSlider, TBM_GETPOS, 0, 0);
    sprintf_s(buf, "Chunk Load Speed: %d/frame", chunkSpeed);
    SetWindowTextA(g_hChunkSpeedLabel, buf);

    int sensitivity = (int)SendMessage(g_hSensitivitySlider, TBM_GETPOS, 0, 0);
    sprintf_s(buf, "Mouse Sensitivity: %.2f", sensitivity / 100.0f);
    SetWindowTextA(g_hSensitivityLabel, buf);
}

// Apply UI values to config
void applySettings() {
    // Renderer selection
    int rendererIdx = (int)SendMessage(g_hRendererCombo, CB_GETCURSEL, 0, 0);
    g_config.renderer = (rendererIdx == 1) ? RendererType::VULKAN : RendererType::OPENGL;

    int resIdx = (int)SendMessage(g_hResolutionCombo, CB_GETCURSEL, 0, 0);
    if (resIdx >= 0 && resIdx < numResolutions) {
        g_config.windowWidth = resolutions[resIdx].width;
        g_config.windowHeight = resolutions[resIdx].height;
    }

    g_config.fullscreen = (SendMessage(g_hFullscreenCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.vsync = (SendMessage(g_hVsyncCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_config.useHighPerformanceGPU = (SendMessage(g_hGpuCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    g_config.renderDistance = (int)SendMessage(g_hRenderDistSlider, TBM_GETPOS, 0, 0);
    g_config.fov = (int)SendMessage(g_hFovSlider, TBM_GETPOS, 0, 0);
    g_config.maxChunksPerFrame = (int)SendMessage(g_hChunkSpeedSlider, TBM_GETPOS, 0, 0);
    g_config.mouseSensitivity = (int)SendMessage(g_hSensitivitySlider, TBM_GETPOS, 0, 0) / 100.0f;
}

// Launch the game
void launchGame() {
    applySettings();
    g_config.save("settings.cfg");

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (CreateProcessA(
        "VoxelEngine.exe",
        NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    )) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        g_shouldLaunch = true;
        PostQuitMessage(0);
    } else {
        MessageBoxA(g_hWnd, "Failed to launch VoxelEngine.exe!\nMake sure it's in the same folder as the launcher.",
            "Error", MB_OK | MB_ICONERROR);
    }
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_PLAY_BUTTON:
                    launchGame();
                    break;
                case ID_SAVE_BUTTON:
                    applySettings();
                    g_config.save("settings.cfg");
                    MessageBoxA(hWnd, "Settings saved!", "Info", MB_OK | MB_ICONINFORMATION);
                    break;
                case ID_QUIT_BUTTON:
                    PostQuitMessage(0);
                    break;
            }
            break;

        case WM_HSCROLL:
            updateSliderLabels();
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// Create a static label
HWND createLabel(HWND parent, const char* text, int x, int y, int w, int h) {
    return CreateWindowA("STATIC", text, WS_CHILD | WS_VISIBLE,
        x, y, w, h, parent, NULL, GetModuleHandle(NULL), NULL);
}

// Create a slider (trackbar)
HWND createSlider(HWND parent, int id, int x, int y, int w, int h, int min, int max, int value) {
    HWND slider = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_HORZ,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(slider, TBM_SETRANGE, TRUE, MAKELPARAM(min, max));
    SendMessage(slider, TBM_SETPOS, TRUE, value);
    return slider;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Load config
    g_config.load("settings.cfg");

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszClassName = "VoxelEngineLauncher";
    RegisterClassExA(&wc);

    // Create main window
    int winW = 450, winH = 530;  // Increased height for renderer selector
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExA(0, "VoxelEngineLauncher", "Voxel Engine Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screenW - winW) / 2, (screenH - winH) / 2, winW, winH,
        NULL, NULL, hInstance, NULL);

    // Title
    HWND hTitle = createLabel(g_hWnd, "VOXEL ENGINE", 150, 15, 150, 25);
    HFONT hTitleFont = CreateFontA(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

    int y = 50;
    int labelX = 20, controlX = 180, controlW = 230;

    // --- Renderer Section ---
    createLabel(g_hWnd, "--- Renderer ---", labelX, y, 400, 20);
    y += 25;

    // Renderer selection
    createLabel(g_hWnd, "Graphics API:", labelX, y + 3, 150, 20);
    g_hRendererCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        controlX, y, controlW, 100, g_hWnd, (HMENU)ID_RENDERER_COMBO, hInstance, NULL);
    SendMessageA(g_hRendererCombo, CB_ADDSTRING, 0, (LPARAM)"OpenGL 4.6");
    SendMessageA(g_hRendererCombo, CB_ADDSTRING, 0, (LPARAM)"Vulkan (Experimental)");
    SendMessage(g_hRendererCombo, CB_SETCURSEL,
        (g_config.renderer == RendererType::VULKAN) ? 1 : 0, 0);
    y += 35;

    // --- Graphics Section ---
    createLabel(g_hWnd, "--- Graphics ---", labelX, y, 400, 20);
    y += 25;

    // Resolution
    createLabel(g_hWnd, "Resolution:", labelX, y + 3, 150, 20);
    g_hResolutionCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        controlX, y, controlW, 200, g_hWnd, (HMENU)ID_RESOLUTION_COMBO, hInstance, NULL);
    for (int i = 0; i < numResolutions; i++) {
        SendMessageA(g_hResolutionCombo, CB_ADDSTRING, 0, (LPARAM)resolutions[i].name);
    }
    SendMessage(g_hResolutionCombo, CB_SETCURSEL, getCurrentResolutionIndex(), 0);
    y += 30;

    // Fullscreen
    g_hFullscreenCheck = CreateWindowA("BUTTON", "Fullscreen",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        controlX, y, 120, 20, g_hWnd, (HMENU)ID_FULLSCREEN_CHECK, hInstance, NULL);
    SendMessage(g_hFullscreenCheck, BM_SETCHECK, g_config.fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);

    // VSync
    g_hVsyncCheck = CreateWindowA("BUTTON", "VSync",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        controlX + 130, y, 100, 20, g_hWnd, (HMENU)ID_VSYNC_CHECK, hInstance, NULL);
    SendMessage(g_hVsyncCheck, BM_SETCHECK, g_config.vsync ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 30;

    // FOV
    g_hFovLabel = createLabel(g_hWnd, "Field of View: 70", labelX, y + 3, 150, 20);
    g_hFovSlider = createSlider(g_hWnd, ID_FOV_SLIDER, controlX, y, controlW, 25, 50, 120, g_config.fov);
    y += 30;

    // Render Distance
    g_hRenderDistLabel = createLabel(g_hWnd, "Render Distance: 24", labelX, y + 3, 170, 20);
    g_hRenderDistSlider = createSlider(g_hWnd, ID_RENDER_DIST_SLIDER, controlX, y, controlW, 25, 4, 48, g_config.renderDistance);
    y += 35;

    // --- Performance Section ---
    createLabel(g_hWnd, "--- Performance ---", labelX, y, 400, 20);
    y += 25;

    // GPU
    g_hGpuCheck = CreateWindowA("BUTTON", "Use High-Performance GPU (for laptops)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        labelX, y, 350, 20, g_hWnd, (HMENU)ID_GPU_CHECK, hInstance, NULL);
    SendMessage(g_hGpuCheck, BM_SETCHECK, g_config.useHighPerformanceGPU ? BST_CHECKED : BST_UNCHECKED, 0);
    y += 25;

    // Chunk Load Speed
    g_hChunkSpeedLabel = createLabel(g_hWnd, "Chunk Load Speed: 32/frame", labelX, y + 3, 170, 20);
    g_hChunkSpeedSlider = createSlider(g_hWnd, ID_CHUNK_SPEED_SLIDER, controlX, y, controlW, 25, 1, 64, g_config.maxChunksPerFrame);
    y += 35;

    // --- Controls Section ---
    createLabel(g_hWnd, "--- Controls ---", labelX, y, 400, 20);
    y += 25;

    // Mouse Sensitivity
    g_hSensitivityLabel = createLabel(g_hWnd, "Mouse Sensitivity: 0.10", labelX, y + 3, 170, 20);
    g_hSensitivitySlider = createSlider(g_hWnd, ID_SENSITIVITY_SLIDER, controlX, y, controlW, 25, 1, 50, (int)(g_config.mouseSensitivity * 100));
    y += 40;

    // Update labels
    updateSliderLabels();

    // Buttons
    int btnW = 100, btnH = 35, btnY = 440;  // Adjusted for new content
    CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        70, btnY, btnW, btnH, g_hWnd, (HMENU)ID_SAVE_BUTTON, hInstance, NULL);
    CreateWindowA("BUTTON", "PLAY", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        180, btnY, btnW, btnH, g_hWnd, (HMENU)ID_PLAY_BUTTON, hInstance, NULL);
    CreateWindowA("BUTTON", "Quit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        290, btnY, btnW, btnH, g_hWnd, (HMENU)ID_QUIT_BUTTON, hInstance, NULL);

    // Show window
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
