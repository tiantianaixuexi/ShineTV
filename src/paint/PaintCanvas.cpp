#include "paint/PaintCanvas.h"

#include "core/Log.h"

#include <mimalloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace shine::paint {
namespace {

void FillRgba(std::byte* data, std::size_t pixels, Rgba8 c) {
    for (std::size_t i = 0; i < pixels; ++i) {
        std::byte* p = data + i * 4;
        p[0] = static_cast<std::byte>(c.r);
        p[1] = static_cast<std::byte>(c.g);
        p[2] = static_cast<std::byte>(c.b);
        p[3] = static_cast<std::byte>(c.a);
    }
}

} // namespace

PaintCanvas::~PaintCanvas() {
    if (base_ != nullptr) {
        mi_free(base_);
        base_ = nullptr;
    }
    if (initial_ != nullptr) {
        mi_free(initial_);
        initial_ = nullptr;
    }
    if (mask_ != nullptr) {
        mi_free(mask_);
        mask_ = nullptr;
    }
}

PaintCanvas::PaintCanvas(PaintCanvas&& other) noexcept
    : width_(std::exchange(other.width_, 0)), height_(std::exchange(other.height_, 0)),
      stride_(std::exchange(other.stride_, 0)), base_(std::exchange(other.base_, nullptr)),
      initial_(std::exchange(other.initial_, nullptr)), mask_(std::exchange(other.mask_, nullptr)),
      revision_(std::exchange(other.revision_, 0)), strokeActive_(std::exchange(other.strokeActive_, false)) {}

PaintCanvas& PaintCanvas::operator=(PaintCanvas&& other) noexcept {
    if (this != &other) {
        this->~PaintCanvas();
        new (this) PaintCanvas(std::move(other));
    }
    return *this;
}

bool PaintCanvas::Resize(std::uint32_t width, std::uint32_t height, Rgba8 defaultColor) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t bytes = pixels * 4u;
    auto* nb = static_cast<std::byte*>(mi_malloc(bytes));
    auto* ni = static_cast<std::byte*>(mi_malloc(bytes));
    auto* nm = static_cast<std::uint8_t*>(mi_calloc(pixels, 1));
    if (nb == nullptr || ni == nullptr || nm == nullptr) {
        if (nb != nullptr) {
            mi_free(nb);
        }
        if (ni != nullptr) {
            mi_free(ni);
        }
        if (nm != nullptr) {
            mi_free(nm);
        }
        return false;
    }
    if (base_ != nullptr) {
        mi_free(base_);
    }
    if (initial_ != nullptr) {
        mi_free(initial_);
    }
    if (mask_ != nullptr) {
        mi_free(mask_);
    }
    base_ = nb;
    initial_ = ni;
    mask_ = nm;
    width_ = width;
    height_ = height;
    stride_ = static_cast<std::size_t>(width) * 4u;
    FillRgba(base_, pixels, defaultColor);
    std::memcpy(initial_, base_, bytes);
    std::memset(mask_, 0, pixels);
    BumpRevision();
    strokeActive_ = false;
    return true;
}

bool PaintCanvas::LoadFromRgba(std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba8) {
    const std::size_t need = static_cast<std::size_t>(width) * height * 4u;
    if (width == 0 || height == 0 || rgba8.size() < need) {
        return false;
    }
    if (!Resize(width, height)) {
        return false;
    }
    std::memcpy(base_, rgba8.data(), need);
    std::memcpy(initial_, base_, need);
    std::memset(mask_, 0, static_cast<std::size_t>(width) * height);
    BumpRevision();
    return true;
}

std::span<const std::byte> PaintCanvas::BasePixels() const noexcept {
    if (base_ == nullptr) {
        return {};
    }
    return {base_, stride_ * height_};
}

std::span<const std::byte> PaintCanvas::InitialPixels() const noexcept {
    if (initial_ == nullptr) {
        return {};
    }
    return {initial_, stride_ * height_};
}

std::span<const std::uint8_t> PaintCanvas::MaskPixels() const noexcept {
    if (mask_ == nullptr) {
        return {};
    }
    return {mask_, static_cast<std::size_t>(width_) * height_};
}

void PaintCanvas::FillAll(Tool tool, Rgba8 color) {
    if (!valid()) {
        return;
    }
    const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
    switch (tool) {
    case Tool::PaintBase:
        FillRgba(base_, pixels, color);
        break;
    case Tool::EraseBase:
        std::memcpy(base_, initial_, pixels * 4u);
        break;
    case Tool::PaintMask:
        std::memset(mask_, 255, pixels);
        break;
    case Tool::EraseMask:
        std::memset(mask_, 0, pixels);
        break;
    }
    BumpRevision();
}

void PaintCanvas::ClearMask() {
    if (!valid()) {
        return;
    }
    std::memset(mask_, 0, static_cast<std::size_t>(width_) * height_);
    BumpRevision();
}

void PaintCanvas::ResetToInitial() {
    if (!valid()) {
        return;
    }
    const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
    std::memcpy(base_, initial_, pixels * 4u);
    std::memset(mask_, 0, pixels);
    BumpRevision();
}

void PaintCanvas::BeginStroke() noexcept { strokeActive_ = true; }

void PaintCanvas::EndStroke() noexcept { strokeActive_ = false; }

void PaintCanvas::StampDisk(float cx, float cy, float radiusPx, Rgba8 color, Tool tool) {
    if (!valid() || radiusPx <= 0.f) {
        return;
    }
    const auto x0 = std::max(0, static_cast<int>(std::floor(cx - radiusPx)));
    const auto x1 = std::min(static_cast<int>(width_) - 1, static_cast<int>(std::ceil(cx + radiusPx)));
    const auto y0 = std::max(0, static_cast<int>(std::floor(cy - radiusPx)));
    const auto y1 = std::min(static_cast<int>(height_) - 1, static_cast<int>(std::ceil(cy + radiusPx)));
    const float r2 = radiusPx * radiusPx;
    const std::size_t pixelCount = static_cast<std::size_t>(width_) * height_;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            if (dx * dx + dy * dy > r2) {
                continue;
            }
            const std::size_t idx = static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x);
            switch (tool) {
            case Tool::PaintBase: {
                std::byte* p = base_ + idx * 4u;
                p[0] = static_cast<std::byte>(color.r);
                p[1] = static_cast<std::byte>(color.g);
                p[2] = static_cast<std::byte>(color.b);
                p[3] = static_cast<std::byte>(color.a);
                break;
            }
            case Tool::EraseBase: {
                std::memcpy(base_ + idx * 4u, initial_ + idx * 4u, 4u);
                break;
            }
            case Tool::PaintMask:
                mask_[idx] = 255;
                break;
            case Tool::EraseMask:
                mask_[idx] = 0;
                break;
            }
        }
    }
    (void)pixelCount;
}

void PaintCanvas::PaintStroke(Vec2 fromUv, Vec2 toUv, float radiusUv, Rgba8 color, Tool tool) {
    if (!valid()) {
        return;
    }
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float maxSide = std::max(w, h);
    const float radiusPx = std::max(0.5f, radiusUv * maxSide);

    const auto toPx = [&](Vec2 uv) {
        // UV clamp 到 [0,1] 再映射；越界不断笔，只是贴边
        const float u = std::clamp(uv.x, 0.f, 1.f);
        const float v = std::clamp(uv.y, 0.f, 1.f);
        return Vec2{u * (w - 1.f), v * (h - 1.f)};
    };
    const Vec2 a = toPx(fromUv);
    const Vec2 b = toPx(toUv);
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / std::max(1.f, radiusPx * 0.35f))));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        StampDisk(a.x + dx * t, a.y + dy * t, radiusPx, color, tool);
    }
    BumpRevision();
}

int PaintCanvas::RunSelfCheck() {
    int pass = 0;
    int fail = 0;
    const auto expect = [&](bool ok, const char* name) {
        if (ok) {
            ++pass;
            log::Info("paint PASS {}", name);
        } else {
            ++fail;
            log::Error("paint FAIL {}", name);
        }
    };

    PaintCanvas canvas;
    expect(canvas.Resize(64, 64, {.r = 10, .g = 20, .b = 30, .a = 255}), "Resize 64x64");
    expect(canvas.valid() && canvas.Width() == 64 && canvas.Revision() >= 1, "valid + revision");
    const auto base0 = canvas.BasePixels();
    expect(base0.size() == 64ull * 64ull * 4ull, "BasePixels size");
    expect(base0[0] == std::byte{10} && base0[1] == std::byte{20} && base0[2] == std::byte{30},
           "default color written");

    // S2：FillAll(Paint) 后 ResetToInitial 回到初始
    canvas.FillAll(Tool::PaintBase, {.r = 200, .g = 0, .b = 0, .a = 255});
    const auto painted = canvas.BasePixels();
    expect(painted[0] == std::byte{200}, "FillAll paint overwrites");
    canvas.ResetToInitial();
    const auto restored = canvas.BasePixels();
    bool okRestore = true;
    for (std::size_t i = 0; i < restored.size(); i += 4) {
        if (restored[i] != std::byte{10} || restored[i + 1] != std::byte{20} || restored[i + 2] != std::byte{30}) {
            okRestore = false;
            break;
        }
    }
    expect(okRestore, "ResetToInitial restores snapshot");

    // S3：遮罩仅 0/255
    canvas.FillAll(Tool::PaintMask);
    auto mask = canvas.MaskPixels();
    bool allOn = true;
    for (std::uint8_t v : mask) {
        if (v != 255) {
            allOn = false;
            break;
        }
    }
    expect(allOn && !mask.empty(), "FillAll MaskPaint all 255");
    canvas.ClearMask();
    mask = canvas.MaskPixels();
    bool allOff = true;
    for (std::uint8_t v : mask) {
        if (v != 0) {
            allOff = false;
            break;
        }
    }
    expect(allOff, "ClearMask all 0");
    canvas.PaintStroke({0.5f, 0.5f}, {0.5f, 0.5f}, 0.1f, {}, Tool::PaintMask);
    mask = canvas.MaskPixels();
    bool mid = false;
    std::size_t onCount = 0;
    for (std::uint8_t v : mask) {
        if (v != 0 && v != 255) {
            mid = true;
        }
        if (v == 255) {
            ++onCount;
        }
    }
    expect(!mid && onCount > 0, "stroke mask only 0/255 and non-empty");

    // S4：点笔画 + 擦底回快照 + 100 笔不越界
    canvas.FillAll(Tool::EraseBase);
    const auto revBefore = canvas.Revision();
    canvas.BeginStroke();
    canvas.PaintStroke({0.5f, 0.5f}, {0.5f, 0.5f}, 0.05f, {.r = 9, .g = 9, .b = 9, .a = 255}, Tool::PaintBase);
    canvas.EndStroke();
    expect(canvas.Revision() > revBefore, "stroke bumps revision");
    const auto afterDot = canvas.BasePixels();
    const std::size_t midIdx = (32ull * 64ull + 32ull) * 4ull;
    expect(afterDot[midIdx] == std::byte{9}, "dot stroke paints center");
    canvas.PaintStroke({0.f, 0.f}, {1.f, 1.f}, 0.02f, {.r = 1, .g = 2, .b = 3, .a = 255}, Tool::PaintBase);
    canvas.PaintStroke({-0.2f, 0.5f}, {1.2f, 0.5f}, 0.01f, {.r = 4, .g = 4, .b = 4, .a = 255}, Tool::PaintBase);
    for (int i = 0; i < 100; ++i) {
        const float t = static_cast<float>(i) / 99.f;
        canvas.PaintStroke({t, 0.f}, {t, 1.f}, 0.01f, {.r = 5, .g = 5, .b = 5, .a = 255}, Tool::PaintBase);
    }
    expect(canvas.valid() && canvas.BasePixels().size() == 64ull * 64ull * 4ull, "100 strokes stay in bounds");
    canvas.PaintStroke({0.5f, 0.5f}, {0.5f, 0.5f}, 0.08f, {}, Tool::EraseBase);
    // 中心被擦回快照
    canvas.FillAll(Tool::PaintBase, {.r = 77, .g = 77, .b = 77, .a = 255});
    canvas.PaintStroke({0.5f, 0.5f}, {0.5f, 0.5f}, 0.2f, {}, Tool::EraseBase);
    const auto afterErase2 = canvas.BasePixels();
    expect(afterErase2[midIdx] == std::byte{10}, "EraseBase restores snapshot pixel");

    log::Info("PAINT_SELF_CHECK pass={} fail={} {}", pass, fail, fail == 0 ? "PASS" : "FAIL");
    return fail;
}

} // namespace shine::paint
