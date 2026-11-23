# memsearch CLI 参考（实测口径）

> 版本：`memsearch 0.4.23`（uv tool）。命令与输出在本机实际跑过；
> 标注「实测」的条目有真实输出为证，「未验证」的不要当结论用。

## 1. 子命令全集（`memsearch --help`，实测）

```
compact     Compress stored memories into a summary.
config      Manage memsearch configuration.
expand      Expand a memory chunk to show full context.
index       Index markdown files from PATHS.
reset       Drop all indexed data.
search      Search indexed memory for QUERY.
skills      Distill, capture, list, and install candidate skills from...
stats       Show statistics about the index.
summarize   Summarize stdin using a configured memsearch-managed LLM...
transcript  Show original conversation turns, with tool calls, from a...
watch       Watch PATHS for markdown changes and auto-index.
```

## 2. 常用命令

### index

```bash
memsearch index <PATH>... [--force] [--collection NAME] [--provider P] [-m MODEL]
                          [--exclude PATTERN] [--ignore-file NAME] [--max-chunk-size N]
```
- 实测输出：`Indexed N chunks.`；新增文件后增量累加。
- 增量语义：内容哈希去重，未改动的文件不重复 embedding；`--force` 才全量重嵌。
- 实测：`--collection` 可在命令行临时改名，优先于 `.memsearch.toml`。

### search

```bash
memsearch search "<QUERY>" [-k N] [--source-prefix PATH] [-j|--json-output]
                           [--reranker-model M] [--collection NAME]
```
- 默认人类可读：`--- Result 1 (score: 0.5000) ---` + `Source:` + `Heading:` + 正文；超长会截断并提示 `run 'memsearch expand <hash>'`。
- `--json-output` 返回数组，实测字段：`chunk_hash`、`content`、`source`、`heading`、`heading_level`、`start_line`、`end_line`、`score`。
- **分数是分桶的，不能当绝对阈值**：实测只出现 `1.0 / 0.984 / 0.50 / 0.4919 / 0.4841`。
  小记忆库强命中 0.98~1.0；数百 chunk 的知识库相关 top-1 只有 0.50，而**无关提问也是 0.50**。
  → 只能用相对排序 + `expand` 人工确认；`0.5 / 0.6 / 0.8` 任何固定阈值都会在某个语料上判错。
- 中文查询实测可用（bge-m3 多语）。

### expand / transcript

```bash
memsearch expand <chunk_hash>     # chunk → 完整小节
memsearch transcript              # 原始对话轮次（含工具调用）
```

### config

```bash
memsearch config list             # 打印 resolved 配置（多来源合并后）
memsearch config get milvus.uri
memsearch config set embedding.provider onnx
memsearch config init             # 交互向导
```

实测的 resolved 结构：

```toml
[milvus]      uri / token / collection
[embedding]   provider / model / batch_size / base_url / api_key
[compact]     llm_provider / llm_model / prompt_file / base_url / api_key
[chunking]    max_chunk_size(1500) / overlap_lines(2)
```

**配置优先级实测**：`~/.memsearch/config.toml` < `<repo>/.memsearch.toml` < 命令行 `--collection`。
但 `.memsearch.toml` 里的 **`milvus.uri` 不生效**（换绝对路径仍 resolve 成 `~/.memsearch/milvus.db`），
`milvus.collection` 生效 → 项目隔离只能靠 collection。

### stats / reset

```bash
memsearch stats [--collection X]  # Total indexed chunks: N
memsearch reset --yes             # Dropped collection.（实测）
```
- 空库时 `stats` 报：`Collection '...' does not exist at '<db>'` → 是"还没 index"，不是坏了。

### summarize（本项目不用）

```bash
memsearch summarize --plugin <claude-code|codex|opencode|openclaw>   # 读 stdin
```
- `--plugin` 合法值不含 codebuddy；摘要由 agent 自己写进 md，不走这条命令。

## 3. 本机环境实测值

| 项 | 值 |
|---|---|
| `memsearch --version` | `memsearch, version 0.4.23` |
| 安装方式 | `uv tool install "memsearch[onnx]"`（`uv tool list` 可见） |
| 默认 embedding | `gpahal/bge-m3-onnx-int8`（onnx，本地 CPU，无需 key） |
| 模型缓存 | `~/.cache/huggingface/hub/models--gpahal--bge-m3-onnx-int8`（约 560 MB） |
| 索引库 | `~/.memsearch/milvus.db`（Milvus Lite，目录式，多 collection 共用） |
| 本项目 collection | `shinetv_memory`（会话记忆，来自 `.memsearch.toml`）、`shinetv_kb`（知识库文档，命令行指定） |
