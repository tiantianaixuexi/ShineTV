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

- [ ] I9.1 ImageBackend + 设置
- [ ] I9.2 队列与落盘
- [ ] I9.3 UI 预览 + 标记 CANON/丢弃
- [ ] I9.4（可选）简单启发式 Critic 或人工 checklist
