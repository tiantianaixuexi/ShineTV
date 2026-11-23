---
name: memsearch
description: 用 memsearch 维护本仓库的"事件记忆"——把每次会话做过什么、为什么这么决定、踩了什么坑写成 markdown 并建语义索引，之后按自然语言召回。当用户说"我们之前/上次讨论过…""为什么这么改的""恢复一下进度""记一下这件事""以前踩过什么坑"，或你需要跨会话历史才能正确回答时使用。工程事实（代码在哪、怎么编、模块职责）不要走这里，那些看 KnowledgeBase/INDEX.md 与 refactor/。
---

# memsearch：会话记忆的写入与召回

本仓库直接用 [zilliztech/memsearch](https://github.com/zilliztech/memsearch) CLI（已在 PATH，`memsearch`）。
它**不是 MCP server**，就是 `CLI + markdown 目录 + 向量索引 + skill`。
（ShineEngine 仓库在 `.codebuddy/plugins/plugins/memsearch/` 有带 hook 的完整插件版；本仓库不带 hook，写入走 §3.3 手动流程。）

## 0. 分工（先读，否则会把东西记错地方）

| 内容类型 | 该放哪 | 例子 |
|---|---|---|
| **工程事实**（代码位置、构建方式、模块职责、架构结论） | `KnowledgeBase/`（薄层速查）+ `refactor/`（长文） | "分层门禁在 `tools/check-layers.ps1`" |
| **事件记忆**（何时做了何事、当时为什么、踩了什么坑） | **本技能管理的 `.memsearch/memory/*.md`** | "2026-09-30 号段没先清后置，viewer-full 判据假红" |
| 用户偏好 / 长期协作习惯 | `AGENTS.md`（常驻规则与地雷） | "构建一律 Build.bat，不要让 CMake 挑编译器" |

判据：**"下次还需要重新知道这件事吗？"** —— 需要静态查证的放文档，需要"回忆过程"的放 memsearch。

## 1. 目录与配置（已实测）

| 项 | 值 |
|---|---|
| 记忆正文（唯一真源，建议入库） | `<repo>/.memsearch/memory/YYYY-MM-DD.md` |
| 项目配置 | `<repo>/.memsearch.toml` → `collection = "shinetv_memory"` |
| 知识库文档索引 | `KnowledgeBase/` + `refactor/`（长文层）→ `collection = "shinetv_kb"`（索引时 `--collection` 指定） |
| 索引库（派生缓存，别备份） | `~/.memsearch/milvus.db`（**全局单文件**，多项目共用；隔离靠 collection） |
| embedding 模型 | `gpahal/bge-m3-onnx-int8`，缓存于 `~/.cache/huggingface/hub/`（首次约 560 MB） |

⚠️ 实测坑：项目级 `.memsearch.toml` 能覆盖 `collection`，**覆盖不了 `milvus.uri`**（换成绝对路径也不生效）。所以别指望"一项目一 db 文件"，只靠 collection 隔离。

## 2. 召回（read）

```bash
memsearch stats                                # 先确认不是空库；报 "Collection ... does not exist" 说明还没 index 过
memsearch search "上次 viewer-full 判据为什么假红" -k 5
memsearch search "拆分" --source-prefix .memsearch/memory/2026-10 -k 5
memsearch search "字形 0.76 倍" -k 5 --json-output
memsearch expand <chunk_hash>                  # 结果是被切的 chunk，需要上下文时展开
memsearch search "帧循环" -k 5 --collection shinetv_kb    # 查工程知识库（模糊定位）
```

**分数怎么读（实测，别信直觉）**：

| 语料 | 相关命中 | 无关提问 |
|---|---|---|
| 记忆库（小语料） | **0.98~1.0000** | 0.48~0.50 |
| 知识库（数百 chunk） | **0.5000** 附近（top-1 命中率实测 6/6） | 0.5000（"今天晚饭吃什么"也能拿 0.50） |

结论：score 是**分桶**的（实际只见 `1.0 / 0.984 / 0.50 / 0.4919 / 0.4841` 几个值）。
它能区分"强命中（≈1.0）"和"其它"，**但不能区分"相关"与"无关"**。所以：

- **不要用绝对阈值判断相关性**（`>0.5`、`>0.6`、`>0.8` 都会在某个语料上判错）；
- 用 **top-k 的相对排序**选候选，再 `memsearch expand <hash>` 看正文确认；
- top-1 是 0.50 时不要把"命中"说成"确定"，说"检索到的最接近的几条"。

中文 / 中英混合查询都可用。

## 3. 写入（write）

### 3.1 什么时候写

会话里出现**决策**、**踩坑与绕行**、**进度变更**时写。一次会话 **1~3 条**，只写"下次真会用到"的。

### 3.2 写之前先查重

```bash
memsearch search "<这次的主题>" -k 3
```
已有同主题段落 → 追加/修订那一段，不要新建重复文件。

### 3.3 手动写（本仓库的唯一写入方式）

追加到当天的文件 `.memsearch/memory/YYYY-MM-DD.md`：

```markdown
## 2026-11-23 接入 memsearch + KnowledgeBase 薄层

- 决定：事件记忆走 `.memsearch/memory/`，工程事实走 KnowledgeBase/（薄层）+ refactor/（长文）。
- 理由：记忆的价值在召回质量；两套工程文档必然漂移（ShineEngine 已吃过"文档两份真相"的亏）。
- 验证：index → search 端到端跑通，中文查询可命中。
```

然后增量索引：

```bash
memsearch index .memsearch/memory     # 正常输出 "Indexed N chunks."
```

### 3.4 自动落盘（本仓库未挂）

DSH 的 Stop hook 默认没挂，**不要假设会话结束会自动落盘**。
（要启用得在 profile 挂 `@deepseek-ai/dsh-hooks-claude-code` 并配 hooks 脚本，是用户级配置、不随仓库走。）
会话收尾按 §3.3 手动写一条即可。历史蒸馏基准见 `.memsearch/memory/` 既有文件的写法。

## 4. 维护

```bash
memsearch stats                       # 已索引 chunk 数
memsearch reset --yes                 # 丢索引（md 不动，安全，之后重跑 index）
memsearch index KnowledgeBase refactor --collection shinetv_kb   # 知识库/长文改动后刷新（增量）
memsearch compact                     # 用 LLM 压缩记忆（需配 key；本项目默认没配）
```

`.memsearch/memory/*.md` 是唯一真源 → **索引随时可重建**，不要为了"保护索引"不敢 reset。

## 5. 硬规则

1. **不要改 `.memsearch.toml` 的 `collection` 名**：改了等于把历史切到另一个 collection，召回立刻"失忆"。
2. **记忆里不写密钥/token/内网地址**。
3. **不复制工程文档正文，只写指针**（如"见 `refactor/记忆.md`『布局与度量』"）。
4. `memsearch` 不在 PATH 时：`uv tool list` 确认，再 `uv tool install "memsearch[onnx]"` 重装；**不要** `pip install --user` 混装出两份。

## 6. 排错

| 现象 | 原因 / 处理 |
|---|---|
| `Collection 'memsearch_chunks' does not exist` | 没 index 过，或没带 `.memsearch.toml`（在仓库根跑）；本项目应为 `shinetv_memory` |
| 召回"失忆"但 md 还在 | 换了 collection / 换了 `~/.memsearch/milvus.db` → `memsearch index .memsearch/memory` 重建 |
| 首次 index 卡很久 | 在下 560 MB 模型，等 `~/.cache/huggingface/hub` 出现 `models--gpahal--bge-m3-onnx-int8` |
| 索引里的内容过期 | 文档改了没重跑 index → `memsearch index <路径>` 增量刷新 |

CLI 全量参数与实测输出见 `references/cli.md`。
