#pragma once
// shine::video —— 视频工程（P5.1 S2）
//
// 工程 = 工程级字段（整片共用的模型段 / 输出帧率）+ 分镜列表（`Shot`，规格与采样参数逐分镜可不同）。
// 存盘走 C++26 静态反射（`util::reflect`，见 `Doc/RULES-LANG.md` §13.6）：
// 字段名即 JSON 键、**不写一行手写 yyjson 样板**；读取是**宽容**的（缺键/类型不符 → 取默认值）。
#include "video/VideoTypes.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace shine::video {

struct VideoProject {
    std::string name = "未命名工程";

    // —— 模型段（P5.4 S3：UNETLoader / CLIPLoader / VAELoader×2 / LoraLoader）——
    std::string unetName;      // 例：ltx-2-19b-dev-fp8.safetensors
    std::string clipName;      // H3 文本编码器
    std::string videoVaeName;  // 视频 VAE
    std::string audioVaeName;  // 音频 VAE（H3 同步出声）
    std::string loraName;      // 可空：不挂 LoRA
    double loraStrength = 1.0;
    double fps = kDefaultFps;  // 成片帧率（CreateVideo）

    // —— 分镜 ——
    std::vector<Shot> shots;

    [[nodiscard]] static VideoProject MakeDefault();

    // 自动纠正（P5.1 S3）：宽高 → 32 的倍数、帧数 → `n % 17 == 5`、参考图 ≤ 9。
    // 每处纠正都 `log::Warn` 打印**纠正前后值**；返回"是否有字段被改动"。
    // 幂等：对已被纠正过的工程再调一次，返回 false 且不打日志。
    bool Sanitize();

    // —— 存盘 ——
    [[nodiscard]] std::string ToJson(bool pretty = true) const;
    // 成功返回 true。**解析失败**（文件损坏 / 根不是对象）返回 false 且**保持原工程不变**；
    // 只是缺字段 → 返回 true，缺的部分取默认值。
    [[nodiscard]] bool LoadFromJson(std::string_view text);

    [[nodiscard]] bool SaveToFile(const std::filesystem::path& path) const;
    [[nodiscard]] bool LoadFromFile(const std::filesystem::path& path);

    // —— 便捷查询 / 修改（P5.4 S6"无可提交分镜"、P5.3 行操作用）——
    [[nodiscard]] std::size_t SubmittableCount() const noexcept;
    [[nodiscard]] Shot& AddShot(); // 追加一个默认分镜并返回引用（方便立刻选中/编辑）
    void RemoveShot(std::size_t index) noexcept;
};

// —— 目录（P5.3 起 UI / P5.4 编译器 / P5.5 执行器共用；**不要再各写一份回落逻辑**）——
// 口径：优先 `Settings()` 里用户填的目录；为空则回落到 `<设置目录>\video\…`
//（`<设置目录>` = `settings.json` 所在目录，通常是 `%APPDATA%\ShineTVStudio`）。
[[nodiscard]] std::filesystem::path VideoProjectDir();  // 工程 / 角色资产根
[[nodiscard]] std::filesystem::path VideoOutputDir();   // 成片落盘目录
[[nodiscard]] std::filesystem::path MediaLibraryDir();  // 分镜里**相对路径**的解析根

} // namespace shine::video
