# P9 · 图片生成接入

依赖：P8 Assemble + generated_images  
**不在 P8 内做供应商绑定**；本阶段接可配置后端。

## 范围

| 做 | 不做（首期） |
|----|----------------|
| 生成任务队列（worker） | 视频/3D |
| 保存 generated_images | 强制某一家云 API |
| 进度/错误到 UI | 自动训练 LoRA |
| 结果 PROPOSED | 无审核自动 CANON |

## 后端抽象

```cpp
struct ImageGenRequest {
  std::string prompt, negative;
  int w, h, steps;
  std::string model;
  yyjson_val* extra;
};
struct ImageGenResult { std::filesystem::path path; std::string raw_meta; };

class ImageBackend {
public:
  virtual ~ImageBackend() = default;
  virtual std::expected<ImageGenResult, ImageGenError>
  Generate(const ImageGenRequest&) = 0;
};
```

首期可先：

1. **本地/自托管**（ComfyUI 已有链路可复用 `src/comfy` 模式，注意不硬耦合业务）
2. 或 HTTP 可配置 endpoint

设置：`imageBackend` / baseUrl / model / 默认分辨率。

## 流程

```
AssemblePrompt → 入队 Job → Backend.Generate → 落盘 visual/…
  → 写 generated_images(PROPOSED) → 可选 VisualCritic → UI 预览
```

线程：只在 worker；UI 只收 PostToUi。

## 与 novel-studio

- 人物/物品详情：「生成立绘」按钮（需已有 stage+prompt）
- 仍保留手绘/上传 sheet 为 CANON 参考

## 任务

- [x] I9.1 ImageBackend + 设置（= PROGRESS P9.1）
  - `src/novel/NovelImageGen.h/.cpp`：`ImageBackend` + `MakeImageBackend()`
  - 后端：`mock`（离线占位 PNG）/ `openai_images`（`net::HttpClient` POST `/images/generations`，b64/url）/ `comfy`（占位，待接 workflow）
  - Settings：`imageBackend/BaseUrl/ApiKey/Model/Width/Height/Steps/OutputRelDir`
  - 设置窗「出图（小说视觉）」段 + 离线自检 `RunImageGenSelfCheck`
  - HTTP 不进 `src/novel` 直接摸 libhv，走 `src/net/HttpClient`
- [x] I9.2 队列与落盘（= PROGRESS P9.2）
  - novel.db **schema v6**：`generated_images`（status 默认 PROPOSED）
  - `src/novel/NovelImageStore.*`：`RunImageJob` = Assemble/prompt → Backend.Generate → 写盘 `visual/gen/<job_id>.png` → 行状态 PROPOSED/FAILED
  - 失败也落 FAILED 行 + 中文 error；`RunImageQueueSelfCheck`
- [x] I9.3 UI 预览 + 标记 CANON/丢弃（= PROGRESS P9.3）
  - NovelView「出图」段：Entity/Asset/章/场/Shot + Prompt/Negative + 「生成图片」
  - 列表（status/prompt）→ 详情 + GPU 缩略图（worker 解码，UI 上传）
  - 按钮：标 CANON / NON_CANON / 丢弃（`SetGeneratedImageStatus` + `visual_canon_logs`）
- [x] I9.4（可选）简单启发式 Critic 或人工 checklist（= PROGRESS P9.4）
  - `EvaluateImageChecklist`：prompt 非空 / 文件落盘 / 尺寸 / 多人提示 / negative
  - JSON 写入 `generated_images.checklist_json`

## 流程（已实现）

```
AssemblePrompt 或手写 prompt
  → worker RunImageJob（入 generated_images RUNNING）
  → ImageBackend.Generate
  → visual/gen/*.png + PROPOSED（或 FAILED）
  → checklist_json
  → UI 列表/预览 → 作者标 CANON / NON_CANON / DISCARDED
```

线程：Generate/解码在 worker；纹理上传与状态按钮在 UI 线程。

