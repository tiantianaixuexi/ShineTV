# ShineTV Studio — 视觉规范

> UI 取色的唯一依据。所有颜色必须走 `theme::Current()` 的 token，**禁止硬编码**。
> 章节号沿用拆分前 `PLAN.md` 的编号（本文件 §7）。

---

## 7. 默认视觉方向

| Token | Dark 默认 |
|-------|-----------|
| WindowBg | `#0E1116` |
| PanelBg | `#151A21` |
| TitleBar | `#0A0D12` |
| Border | `#2A3340` |
| Text | `#E6EDF3` |
| Accent | `#2EC4B6` |
| AccentAlt | `#FF9F1C` |
| Danger | `#E63946` |
| Success | `#3DDC97` |

活动栏背景用 TitleBar；选中项左侧 2.5px Accent 指示条。全部取色走 `theme::Current()` 的 token，**禁止硬编码颜色**。

---

