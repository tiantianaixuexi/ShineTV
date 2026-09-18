# P8 · 视觉资产与 Prompt 体系 · 分册总览

> 文本 Canon → 视觉 Canon → 剧情状态 → 视觉状态 → 分层 Prompt →（P9）出图  
> **生成图不得未经审核反向改写 Canon。**

## 与 P2 图谱关系

| 已有（文本图谱） | 本阶段（视觉） |
|------------------|----------------|
| entities / character_status / item_details | visual_assets / visual_states |
| scenes（叙事） | scene_layouts / shots（镜头） |
| entity_images.sheet（一张四视图） | 复用并扩展：sheet 属 reference；生成图另表 |
| canon_logs | 视觉同用；visual 亦有 DRAFT/PROPOSED/CANON/ARCHIVED |

磁盘：`工程/assets/<entity_id>/…` 与 `工程/visual/<asset_id>/…`  
库内相对路径；大图异步解码（同 gallery 纪律）。

## 分册

| 目录 | 文件 | 内容 |
|------|------|------|
| `assets/` | `视觉分类与主档.md` | 人物/服装/物品视觉字段、主档表 |
| `stage/` | `阶段与状态机.md` | VisualStage、与 CharacterState/ItemState 绑定 |
| `scene/` | `场景与布局.md` | 场景视觉、空间布局、坐标 |
| `camera/` | `镜头构图光影.md` | Camera / Composition / Lighting |
| `prompt/` | `分层Prompt与组装.md` | Base/Stage/…/Negative + Final 组装 |
| `pipeline/` | `生产链与分镜.md` | 工作流、Shot、Critic、一致性、Canon |
| — | `PLAN.md` | 本总览与任务门禁 |

## 依赖

```
P2 图谱核心表 ──► P8 视觉表与组装 ──► P9 出图接入
                     │
P5 Agent（可选）◄────┘  分镜/画评可挂 Agent 角色
```

P8 可与 P3–P6 部分并行，但 **P9 依赖 P8**。

## 全局原则（写进实现）

1. **文本 Canon 决定视觉 Canon**
2. **剧情状态决定视觉状态**（读 chapter 的 character/item status，不靠模型猜）
3. **视觉状态决定 Prompt**
4. **生成结果默认 PROPOSED**，审核后才能升 CANON / 成为 Reference
5. 修改单层 Prompt 只重拼 Final，不整库重写

## 任务门禁（详见各分册）

| ID | 门禁 |
|----|------|
| V8.1 | visual_assets + 关联 entity |
| V8.2 | visual_states 阶段机 |
| V8.3 | scene_layouts |
| V8.4 | camera/composition/lighting 表 |
| V8.5 | prompt_layers + Final 组装函数 |
| V8.6 | shots 分镜 |
| V8.7 | 一致性检查规则引擎（文本侧） |
| V8.8 | visual canon / audit |
