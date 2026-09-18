#include "app/Fonts.h"
#include "core/Log.h"

#include <imgui.h>

#include <windows.h>

namespace shine::app {
namespace {

bool FileExists(const wchar_t* path) {
    const DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

bool LoadUIFonts(float sizePixels) {
    ImGuiIO& io = ImGui::GetIO();

    const wchar_t* candidates[] = {
        L"C:\\Windows\\Fonts\\msyh.ttc",   // 微软雅黑
        L"C:\\Windows\\Fonts\\msyhbd.ttc", // 微软雅黑 Bold
        L"C:\\Windows\\Fonts\\simhei.ttf", // 黑体
        L"C:\\Windows\\Fonts\\simsun.ttc", // 宋体
        L"C:\\Windows\\Fonts\\msyh.ttf",
    };

    const char* names[] = {
        "msyh.ttc", "msyhbd.ttc", "simhei.ttf", "simsun.ttc", "msyh.ttf",
    };

    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    for (int i = 0; i < 5; ++i) {
        if (!FileExists(candidates[i])) continue;

        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;
        cfg.MergeMode = false;

        ImFont* font = io.Fonts->AddFontFromFileTTF(
            // path as UTF-8 for ImGui (system ANSI/UTF-8 path usually works for C:\Windows\Fonts)
            [&]() -> const char* {
                switch (i) {
                case 0: return "C:/Windows/Fonts/msyh.ttc";
                case 1: return "C:/Windows/Fonts/msyhbd.ttc";
                case 2: return "C:/Windows/Fonts/simhei.ttf";
                case 3: return "C:/Windows/Fonts/simsun.ttc";
                default: return "C:/Windows/Fonts/msyh.ttf";
                }
            }(),
            sizePixels, &cfg, ranges);

        if (font) {
            io.FontDefault = font;
            log::Info("UI font loaded: {} ({:.0f}px, Chinese Simplified Common)", names[i], sizePixels);
            return true;
        }
        log::Warn("Failed to load font {}", names[i]);
    }

    log::Error("No Chinese font found under C:/Windows/Fonts — UI may show tofu boxes");
    return false;
}

} // namespace shine::app
