#pragma once
// shine::paint::PaintService —— inpaint 图编译 + 提交执行（P6.3）
#include "paint/PaintCanvas.h"
#include "paint/PaintTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace shine::paint {

struct InpaintParams {
    std::string checkpoint; // CheckpointLoaderSimple
    std::string prompt;
    std::string negative;
    int steps = 20;
    double cfg = 7.0;
    double denoise = 0.85;
    std::int64_t seed = -1;
    double growMaskBy = 8.0;
    std::string outputPrefix = "paint/shine";
    MaskMode maskMode = MaskMode::Editable; // Protect → 导出前反相（白=可重绘）
    std::string uploadedImageName;          // 上传后原图名
    std::string uploadedMaskName;           // 上传后遮罩名
};

struct InpaintGraphResult {
    bool ok = false;
    std::string error;
    std::string apiJson;
    int nodeCount = 0;
};

// 九节点图（施工图抄本）；uploaded 名为空时用占位（dry 自检）
[[nodiscard]] InpaintGraphResult BuildInpaintGraph(const InpaintParams& params);

enum class InpaintPhase : int {
    Idle = 0,
    Encoding,
    Uploading,
    Submitting,
    Running,
    Downloading,
    Done,
    Failed,
};

[[nodiscard]] const char* InpaintPhaseLabel(InpaintPhase p) noexcept;

struct InpaintRunState {
    InpaintPhase phase = InpaintPhase::Idle;
    std::string detail;
    std::string error;
    std::string promptId;
    std::string resultPath; // UTF-8 本地结果
};

// UI 线程调用；内部 worker 编码/上传/下载，回调在 UI 线程
[[nodiscard]] bool StartInpaint(const PaintCanvas& canvas, const InpaintParams& params,
                                std::function<void(const InpaintRunState&)> onFinish);

[[nodiscard]] bool InpaintBusy();
void CancelInpaint(); // /interrupt，状态回 Idle
[[nodiscard]] const InpaintRunState& InpaintState();

// 导出画布底图 PNG 到磁盘（UTF-8 路径）
[[nodiscard]] bool ExportCanvasPng(const PaintCanvas& canvas, std::string_view utf8Path, std::string* error);

[[nodiscard]] int RunInpaintSelfCheck();

} // namespace shine::paint
