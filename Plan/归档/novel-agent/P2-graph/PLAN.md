# P2 · 叙事知识图谱 · 分册总览

库：`novel.db` v3  
模块：`src/novel/NovelDb.*` `NovelGraph.*`  
契约：`docs/compose/spec/novel-agent.md` [S2.3]

## 分册（每域一文件，禁止合并压缩）

| 分册 | 文件 | 内容 |
|------|------|------|
| 世界 | `world/世界基础.md` | 宇宙/规则/历史/社会/科技/文化/语言/宗教/经济/纪元 |
| 人物 | `character/人物.md` | 人设全字段/弧光/状态/情绪/关系/对话/知情 |
| 实体 | `entity/物品地点势力能力.md` | 物品细分与持有、地点与距离、势力与成员、能力与克制、资源 |
| 叙事 | `story/叙事结构与冲突因果.md` | 卷章场拍、剧情线、冲突、因果链、主题母题、文风 POV |
| 谜 | `mystery/谜伏笔秘密知情.md` | 伏笔状态机、秘密、信息节奏、知情矩阵 |
| 系统 | `system/系统层.md` | Canon、版本、依赖、作者规则、审计、记忆表结构 |

## 迁移顺序

```
B0  NovelDb 打开 + meta.schemaVersion
B1  entities 骨架 + kinds 全量
B2  人物域
B3  世界域
B4  地点/势力/能力/物品/资源
B5  卷章场拍 + 剧情
B6  冲突 + 因果
B7  谜/伏笔/秘密/知情
B8  系统层（canon/version/dep/rules/audit）
B9  NovelGraph 查询 API 封装（给 P3/P4 用）
```

每步：建表 SQL → 迁移函数 → 最小 insert/select 自检 → build。

## 全局约定

- 新表必须 `IF NOT EXISTS`；升级用 `schemaVersion` 分支
- 外键：能声明则声明；跨 kind 用应用层校验（如 owner 必须是 person/location）
- 禁止把 UI 文案写进业务表
- 图片仍见 novel-studio：`assets/<id>/sheet.png`

## 交付给下游的 API（P2.9 摘要）

```cpp
namespace shine::novel {
  // 打开当前工程库；迁移
  std::expected<void, DbError> OpenProjectDb(const std::filesystem::path& dbPath);
  // 实体
  std::expected<int64_t, DbError> UpsertEntity(const EntityRow&);
  std::expected<EntityRow, DbError> GetEntity(int64_t id);
  std::vector<EntityRow> ListEntities(std::string_view kind, std::string_view nameFilter);
  // 关系 / 事件 / 因果 / 持有
  // 场景 / 伏笔 / 秘密知情 / mystery
  // WorldSlice / CharacterSlice 供 ContextBuilder
}
```

细节以各分册字段表为准。
