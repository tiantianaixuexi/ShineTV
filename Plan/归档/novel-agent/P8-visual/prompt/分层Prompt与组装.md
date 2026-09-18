# 分层 Prompt 与 Final 组装

不要只存一条字符串 Prompt。分层存储，改一层只重拼。

## 层

| 层 | 内容来源 |
|----|----------|
| Base | visual_assets.base_desc + 永久特征 |
| Stage | visual_states.appearance 等 |
| Scene | scene_visuals / scene_layouts 环境 |
| Action | 本 shot/场景动作与表情 |
| Camera | camera_defs |
| Composition | composition_defs |
| Lighting | lighting_defs |
| Style | 全书 visual_styles |
| Quality | 画质词（可配置模板） |
| Negative | 负面词模板 + 剧情禁忌（未登场角色等） |

```
FinalPrompt = join(Base, Stage, Scene, Action, Camera, Composition, Lighting, Style, Quality)
NegativePrompt = join(neg_base, neg_consistency, neg_scene)
```

## 表

### visual_styles（全书单行或预设）

id, name, payload_json（媒介/笔触/年代感/参考画师风格词（无版权演员名）…）, note

### prompt_layers

| 列 | 说明 |
|----|------|
| id | |
| owner_kind | asset / state / scene / shot / preset |
| owner_id | |
| layer | base/stage/scene/action/camera/composition/lighting/style/quality/negative |
| text | |
| model_hint | 可选：适配模型（sd/flux/mj…）的措辞差异 |
| version | |
| canon_status | |

### prompt_finals（可选缓存）

id, shot_id 或 request_id, final_text, negative_text, model, params_json, created, canon_status

## 组装 API

```cpp
struct AssemblePromptInput {
  int64_t chapter_id;
  int64_t scene_id;       // 或 layout
  std::optional<int64_t> character_id;
  std::optional<int64_t> asset_id;      // 人物主资产或物品
  std::optional<int64_t> shot_id;       // 有分镜则优先
};
struct AssemblePromptOutput {
  std::string final_prompt;
  std::string negative_prompt;
  yyjson_val* model_params; // steps/size/sampler… 可空
  std::vector<int64_t> used_layer_ids;
};
std::expected<AssemblePromptOutput, DbError> Assemble(const AssemblePromptInput&);
```

## 画面描述中间产物（可选写入 memories 或 shot）

结构化中文/英文描述块，便于人审：

主体/人物/服装/动作/表情/道具/环境/背景/光影/构图/镜头/氛围/色彩/风格

## 任务

- [ ] Q8.1 prompt_layers + visual_styles
- [ ] Q8.2 Assemble() 九层拼接与截断
- [ ] Q8.3 Negative 组装（一致性：多余人物/错发色等）
- [ ] Q8.4 只改 Lighting 层后 Final 变化、其余层 id 不变
