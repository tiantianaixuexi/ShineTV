#pragma once
// shine::video —— 视频分镜的**数据模型**（P5.1 S1）
//
// 这里是"工程文件里有什么"的唯一真相来源：**字段名就是 JSON 键**
// （静态反射序列化，见 `Doc/RULES-LANG.md` §13.6 与 `src/util/Reflect.h`）。
// 因此：改字段名 = 改存盘格式 —— 老工程靠"宽容读取"兜底（缺的键取默认值），不会崩。
//
// 对齐规则只在这里定义一份（P5.1 S3 的 `Sanitize()` 与 P5.4 S2 的编译器共用）：
//   * 宽高：向上对齐到 `kSizeMultiple`(32) 的倍数（H3 VAE 空间压缩要求）；
//   * 帧数：向上对齐到最近的 `n % kFrameGridStride(17) == kFrameGridOffset(5)`。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shine::video {

// 出片模式：决定 P5.4 编译成哪条 H3 工作流
enum class ShotMode : int {
    Reference = 0,      // ref2va：多参考图**软锚定**（MiniMaxH3ReferenceToVideo）
    FirstLastFrame = 1, // fl2va：首/末帧**硬锚定**（MiniMaxH3ImageToVideo）
};

[[nodiscard]] constexpr const char* ShotModeLabel(ShotMode mode) noexcept {
    switch (mode) {
    case ShotMode::Reference:
        return "参考图";
    case ShotMode::FirstLastFrame:
        return "首末帧";
    }
    return "未知";
}

// —— 硬性常量（与 ComfyUI 侧一致，别在各处再抄一遍）——
inline constexpr int kMaxReferenceImagesPerShot = 9; // H3 参考图上限
inline constexpr int kSizeMultiple = 32;             // 宽高对齐粒度
inline constexpr int kFrameGridStride = 17;          // 帧网格步长
inline constexpr int kFrameGridOffset = 5;           // 帧网格起点（合法帧数 n % 17 == 5）
inline constexpr double kDefaultFps = 24.0;

// —— 新建分镜的默认值（刻意都满足对齐规则，`Sanitize()` 不会改它们）——
inline constexpr int kDefaultWidth = 832;  // 26 × 32
inline constexpr int kDefaultHeight = 480; // 15 × 32
inline constexpr int kDefaultLength = 107; // 17 × 6 + 5
inline constexpr int kDefaultSteps = 20;
inline constexpr double kDefaultCfg = 3.0;
inline constexpr double kDefaultDenoise = 1.0;
inline constexpr double kDefaultShift = 3.0;

// 向上对齐到 multiple 的倍数；value/multiple 非正 → 原样返回（调用方负责报"非法值"）
[[nodiscard]] constexpr int AlignToMultiple(int value, int multiple = kSizeMultiple) noexcept {
    if (value <= 0 || multiple <= 0) {
        return value;
    }
    return ((value + multiple - 1) / multiple) * multiple;
}

// 向上对齐到最近的 `n % 17 == 5`：100 → 107、6 → 22、5 → 5、负数/0 → 5
[[nodiscard]] constexpr int AlignFrameCount(int value) noexcept {
    if (value <= kFrameGridOffset) {
        return kFrameGridOffset;
    }
    const int k = (value - kFrameGridOffset + kFrameGridStride - 1) / kFrameGridStride;
    return k * kFrameGridStride + kFrameGridOffset;
}

// 一个分镜。字段全部参与存盘（含末尾的"运行期回填"三项 —— 验收 ① 要求重启后完全一致）。
struct Shot {
    // —— ① 用户输入 ——
    std::string title;
    std::string prompt;                       // 支持 `@image:` / `@char:` / `{{Mixed N}}`（P5.2）
    ShotMode mode = ShotMode::Reference;
    std::string firstFramePath;               // 首帧图（fl2va 用；可为绝对路径或相对 mediaLibraryDir）
    std::vector<std::string> referenceImages; // 参考图（ref2va 用，上限 9）
    std::vector<std::string> characterAssetPaths; // 本分镜用到的角色资产（P5.2 顺序规则 ②）
    bool chainFromPrevious = false;           // 链式：接上一段末帧

    // —— ② 输出规格与采样参数 ——
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    int length = kDefaultLength;              // 帧数（不是秒）
    int steps = kDefaultSteps;
    double cfg = kDefaultCfg;
    double denoise = kDefaultDenoise;
    double shift = kDefaultShift;
    std::int64_t seed = -1;                   // -1 = 随机
    std::int64_t forcedSeed = -1;             // 角色 lockSeed 回填（P5.2 S4）；≥0 时优先于 seed

    // —— ③ 运行期回填（P5.5 写；一并存盘）——
    std::string lastPromptId;
    std::vector<std::string> lastOutputFiles;
    std::string lastError;

    // 能提交给 ComfyUI 的最小条件：有提示词 + 规格为正 + 参考图不超上限。
    // （不做模型名检查 —— 那是工程级的事，见 `VideoProject`；P5.4 S6 的"无可提交分镜"看这里）
    [[nodiscard]] bool IsSubmittable() const noexcept {
        return !prompt.empty() && width > 0 && height > 0 && length > 0 &&
               referenceImages.size() <= static_cast<std::size_t>(kMaxReferenceImagesPerShot);
    }

    // 实际生效的种子：有强制种子就用它，否则用 seed（-1 表示交给 ComfyUI 随机）
    [[nodiscard]] std::int64_t EffectiveSeed() const noexcept {
        return forcedSeed >= 0 ? forcedSeed : seed;
    }
};

[[nodiscard]] inline Shot MakeDefaultShot(std::string_view title = {}) {
    Shot shot;
    shot.title.assign(title);
    return shot;
}

} // namespace shine::video
