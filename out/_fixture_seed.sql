-- 取证 fixture 的补充行（卷 + 伏笔 + 场↔伏笔关联 + 视觉资产）。
--
-- 为什么要单独一个文件：`_fixture_data.sql` 与 `_fixture_schema.sql` 是上一轮留档的，
-- 里面的中文是 UTF-8。**不要用 PowerShell 的 Set-Content / Out-File 改它们** —— 那会按
-- 系统 ANSI 码页（中文 Windows = GBK/936）重编码，把「阁楼路问答」这类字静默换成乱码，
-- 而且脚本照跑成功。改 fixture 一律新建文件（本文件），别动老文件。
--
-- 走的是业务层**已有的**两个接口，没改任何 C++：
--   novelcore::NovelGraph::ListOpenForeshadows()  只返回 PLANNED|PLANTED|DEVELOPING
--   novelcore::NovelGraph::ListSceneForeshadows(sceneId)
-- 所以这里的 status 必须落在这三个值里，否则 UI 侧按口径取不到（那是真实的库行为，
-- 不是 UI 漏了 —— REVEALED / RESOLVED 的伏笔本来就查不到标题）。

-- 叙事结构：卷。
--
-- 为什么要补这个：`volumes` 表在 schema 里一直存在（`out/_fixture_schema.sql` 的「叙事结构」段），
-- 但三份 SQL 里**一条 volumes 都没有**；`_fixture_data.sql` 的 chapters 全部写死 volume_id=1，
-- 指向一个根本不存在的卷。UI 侧按「书 / 卷 / 章」三层渲染时，卷那一层永远是空的 ——
-- 页面拍到了，那一层没跑到，和下面 visual_assets 是同一类覆盖洞，只是更隐蔽。
--
-- 分配口径：第 1 章 → 卷 1，第 2–3 章 → 卷 2。**刻意分成两组**：全落同一个卷的话，
-- 卷层级的分组分支在取证里永远跑不出来，截图也分不出效果。
-- chapters 的行是留档文件 `_fixture_data.sql` 建的（那一轮定的规矩：老文件不动，
-- 改动一律追加到本文件），所以这里用 UPDATE 改 volume_id，而不是重写那条 INSERT。
-- chapters 用的是显式 id（1/2/3），故 UPDATE 按 id 定位。
INSERT INTO volumes(id,title,ord,summary) VALUES
 (1,'第一卷 · 雨夜来信',1,'信件、回声与第一次失约'),
 (2,'第二卷 · 断链',2,'连续性开始出现裂缝的两章');

UPDATE chapters SET volume_id=1 WHERE id=1;  -- 第 1 章《雨夜里的第七封来信》
UPDATE chapters SET volume_id=2 WHERE id=2;  -- 第 2 章《旧仓库的钥匙》
UPDATE chapters SET volume_id=2 WHERE id=3;  -- 第 3 章《尚未展开的一章》

INSERT INTO foreshadowings(id,title,content,status,setup_ch,payoff_ch,importance,truth,entity_ids_json) VALUES
  (1,'第七封信的邮戳','信封上的邮戳日期比信件内容晚三天。','PLANTED',1,3,80,'信件是林昭自己寄的，收信人也是他。','[1]'),
  (2,'阁楼里多出的鞋印','二楼阁楼地板上有第三个人的鞋印，尺码比周姨小两号。','DEVELOPING',1,3,65,'来访者是林昭的旧同事。','[1,2]'),
  -- 已回收的伏笔：故意留一行在库里，用来验「取不到就不出 tag」这个诚实空态
  -- （ListOpenForeshadows 不含 REVEALED/RESOLVED）。
  (3,'门后的旧报纸','门后压着的一叠旧报纸，最后一张日期是三年前。','REVEALED',1,2,40,'报纸上登着失踪案的第一则消息。','[]');

INSERT INTO scene_foreshadows(id,scene_id,foreshadowing_id,action) VALUES
  (1,1,1,'plant'),    -- 第 1 章第 1 场：埋邮戳
  (2,2,2,'develop'),  -- 第 1 章第 2 场：推进鞋印
  (3,2,1,'develop');  -- 同一场再推一次邮戳

-- 视觉资产：故意给两个实体各一条，且**状态取两个不同的值**。
--
-- 为什么加这个：出图页「图评审」页签改成「只列真有视觉资产的实体」之后，fixture 里
-- 一条 visual_assets 都没有，于是那一页**只跑得到诚实空态**、列表分支从来没被执行过 ——
-- 和「多态视图进不去」是同一类覆盖洞，只是更隐蔽：页面拍到了，分支没跑到。
--
-- status 取 visual_assets 的八值原文（src/novel/NovelTypes.h 的 visual 状态集）：
--   READY  → 生产状态映射到「就绪」那一档
--   STALE  → 映射到「已过期」那一档
-- 两条一起放，是为了让同一张列表里同时出现两种色调 —— 只放一条的话另一种映射同样没被跑到。
INSERT INTO visual_assets(id,entity_id,kind,name,base_desc,materials_colors,permanent_tags_json,sheet_rel_path,canon_status,status,note) VALUES
  (1,1,'character','林昭 · 外观基线','雨夜里的邮差，右手常搭在帽檐上。','藏青呢大衣 / 灰呢围巾 / 油布帽','["邮差","雨夜"]','assets/linwan_base.png','APPROVED','READY','T4 角色正面图'),
  (2,2,'character','周姨 · 阁楼照','阁楼里的守夜人，左耳戴银环。','暗红针织衫 / 灰白围裙','["守夜人"]','assets/zhouyi_base.png','PROPOSED','STALE','T4 出图后被 T6 改稿，需重出');

-- 阶段产物（`stage_artifacts`）：「章节 × 阶段」双轴甘特图的**数据底座**。
--
-- 为什么要补这个：只读接口 `novel::ListStageArtifacts(chapterId, stage)`
-- （`src/novel/NovelVisual.cpp:605`，`ORDER BY scene_ord,shot_ord`）早就存在，`stage_artifacts`
-- 的建表语句也一直在 schema 里 —— 但三份 SQL 里**一条 stage_artifacts 都没有**，表是 0 行。
-- 于是甘特图整片空白：不是图没画，是**没数据可画**。和上面 visual_assets / volumes
-- 是同一类覆盖洞 —— 页面拍到了，数据分支从来没被执行过。
--
-- 口径（照真实写入方反推，不另立一套）：
--   写入方 `src/novel/NovelVisualStages.cpp:144-156` 的键是 `(chapter_id, stage, scene_ord, shot_ord)`，
--   `scene_ord` 取自故事板骨架的 `scene_ord`、`shot_ord` 取自骨架的 `ord` —— **都是 1-based**，
--   与 `scenes.ord` / `shots.ord` 同一口径，故下面直接照抄库里那两列的真实值。
--   读方 `src/ui/imgui/pages/WorkspaceB.cpp:671-676` 逐阶段 `for v in 1..7` 查 `"V" + v`，**有行即 done**。
--
-- 覆盖矩阵（按库里真实的章/场/镜数，不是凭空拍的）：
--   第 1 章《雨夜里的第七封来信》  场1 3 镜 + 场2 2 镜 = 5 镜 → V1–V7 **全跑满**（35 行）
--   第 2 章《旧仓库的钥匙》        场1 2 镜             = 2 镜 → V1–V5，**缺 V6/V7**（10 行）
--   第 3 章《尚未展开的一章》      0 场 0 镜            → 一行都没有（见下面「刻意留的缺口」）
-- 合计 45 行。
--
-- ⚠️ payload_json 一律 `'{}'`：这个字段的真实值是分镜骨架 / 提示词 JSON，我们手上**没有**，
-- 编一个出来就是伪造取证证据。甘特图画的是**进度**（哪些阶段跑过），不读 payload 内容，
-- 留空对象不影响这张图 —— 真要验 payload 形态的用例得另造数据。
--
-- ⚠️ input_state_hash 写成 `seed-<阶段>-ch<章序>-sc<场序>-sh<镜序>`（如 `seed-V3-ch1-sc1-sh2`），
-- 而不是 16 位十六进制：真实值是内容哈希，长成 `a3f1...` 会**看着像实测值**，拿它当基线的
-- 人会以为能复现。这里一眼看得出是 fixture。比口述示例多带**章序 + 场序**两个维度：
--   ① shot_ord=1 在每章每场都重新数，只写镜序会让很多个不同的镜共用同一个 hash；
--   ② 带上章序后 45 行的 hash **全互不相同**（已自检 count(DISTINCT)=45）。少带章序时
--      两章会共用同一批字符串，甘特图若按 hash 分组就会把两章的镜并到一起，看着像 bug。
--   该列没有 UNIQUE 约束，不加也不报错 —— 所以这条是**自检发现的**，不是 schema 报的。
--
-- created：Unix 秒，基线 1750000000，按**章 / 场 / 阶段 / 镜**层层递进：
--   章 +86400（第 1 章 D1、第 2 章 D2，两章各占一条时间带）
--   阶段 +3600（V1 最早、V7 最晚；3600 > 单章内最大散差 1560 ⇒ 阶段先后压得住散差）
--   场 +600、镜 +120（同一阶段内也有先后，甘特图才是真时间轴，而不是七条竖线）
--
-- ⚠️ 刻意留的缺口（**故意的，别补**）—— 甘特图画的是**进度**，全绿就看不出它真的按数据画：
--   ① 第 2 章只跑到 V5：V6 / V7 **刻意一行都不给**。用来验「章 × 阶段」那根轴上
--      「后半段没跑」显示成进度缺口，而不是被悄悄补成全绿。
--   ② 第 3 章一行都没有：该章在库里本就 0 场 0 镜（《尚未展开的一章》），按「每镜每阶段一行」
--      的口径自然产出 0 行。用来验「整章没进度」这条诚实空态。
--   两处缺口都保留 —— 补齐了就把「进度」验没了。
INSERT INTO stage_artifacts(chapter_id,stage,scene_ord,shot_ord,payload_json,input_state_hash,created) VALUES
 -- 第 1 章：5 镜 × V1–V7 = 35 行（跑满）
 -- V1
 (1,'V1',1,1,'{}','seed-V1-ch1-sc1-sh1',1750000120),
 (1,'V1',1,2,'{}','seed-V1-ch1-sc1-sh2',1750000240),
 (1,'V1',1,3,'{}','seed-V1-ch1-sc1-sh3',1750000360),
 (1,'V1',2,1,'{}','seed-V1-ch1-sc2-sh1',1750000720),
 (1,'V1',2,2,'{}','seed-V1-ch1-sc2-sh2',1750000840),
 -- V2
 (1,'V2',1,1,'{}','seed-V2-ch1-sc1-sh1',1750003720),
 (1,'V2',1,2,'{}','seed-V2-ch1-sc1-sh2',1750003840),
 (1,'V2',1,3,'{}','seed-V2-ch1-sc1-sh3',1750003960),
 (1,'V2',2,1,'{}','seed-V2-ch1-sc2-sh1',1750004320),
 (1,'V2',2,2,'{}','seed-V2-ch1-sc2-sh2',1750004440),
 -- V3
 (1,'V3',1,1,'{}','seed-V3-ch1-sc1-sh1',1750007320),
 (1,'V3',1,2,'{}','seed-V3-ch1-sc1-sh2',1750007440),
 (1,'V3',1,3,'{}','seed-V3-ch1-sc1-sh3',1750007560),
 (1,'V3',2,1,'{}','seed-V3-ch1-sc2-sh1',1750007920),
 (1,'V3',2,2,'{}','seed-V3-ch1-sc2-sh2',1750008040),
 -- V4
 (1,'V4',1,1,'{}','seed-V4-ch1-sc1-sh1',1750010920),
 (1,'V4',1,2,'{}','seed-V4-ch1-sc1-sh2',1750011040),
 (1,'V4',1,3,'{}','seed-V4-ch1-sc1-sh3',1750011160),
 (1,'V4',2,1,'{}','seed-V4-ch1-sc2-sh1',1750011520),
 (1,'V4',2,2,'{}','seed-V4-ch1-sc2-sh2',1750011640),
 -- V5
 (1,'V5',1,1,'{}','seed-V5-ch1-sc1-sh1',1750014520),
 (1,'V5',1,2,'{}','seed-V5-ch1-sc1-sh2',1750014640),
 (1,'V5',1,3,'{}','seed-V5-ch1-sc1-sh3',1750014760),
 (1,'V5',2,1,'{}','seed-V5-ch1-sc2-sh1',1750015120),
 (1,'V5',2,2,'{}','seed-V5-ch1-sc2-sh2',1750015240),
 -- V6
 (1,'V6',1,1,'{}','seed-V6-ch1-sc1-sh1',1750018120),
 (1,'V6',1,2,'{}','seed-V6-ch1-sc1-sh2',1750018240),
 (1,'V6',1,3,'{}','seed-V6-ch1-sc1-sh3',1750018360),
 (1,'V6',2,1,'{}','seed-V6-ch1-sc2-sh1',1750018720),
 (1,'V6',2,2,'{}','seed-V6-ch1-sc2-sh2',1750018840),
 -- V7
 (1,'V7',1,1,'{}','seed-V7-ch1-sc1-sh1',1750021720),
 (1,'V7',1,2,'{}','seed-V7-ch1-sc1-sh2',1750021840),
 (1,'V7',1,3,'{}','seed-V7-ch1-sc1-sh3',1750021960),
 (1,'V7',2,1,'{}','seed-V7-ch1-sc2-sh1',1750022320),
 (1,'V7',2,2,'{}','seed-V7-ch1-sc2-sh2',1750022440),
 -- 第 2 章：2 镜 × V1–V5 = 10 行。⚠️ V6 / V7 刻意不给（上面「刻意留的缺口 ①」）。
 -- V1
 (2,'V1',1,1,'{}','seed-V1-ch2-sc1-sh1',1750086520),
 (2,'V1',1,2,'{}','seed-V1-ch2-sc1-sh2',1750086640),
 -- V2
 (2,'V2',1,1,'{}','seed-V2-ch2-sc1-sh1',1750090120),
 (2,'V2',1,2,'{}','seed-V2-ch2-sc1-sh2',1750090240),
 -- V3
 (2,'V3',1,1,'{}','seed-V3-ch2-sc1-sh1',1750093720),
 (2,'V3',1,2,'{}','seed-V3-ch2-sc1-sh2',1750093840),
 -- V4
 (2,'V4',1,1,'{}','seed-V4-ch2-sc1-sh1',1750097320),
 (2,'V4',1,2,'{}','seed-V4-ch2-sc1-sh2',1750097440),
 -- V5
 (2,'V5',1,1,'{}','seed-V5-ch2-sc1-sh1',1750100920),
 (2,'V5',1,2,'{}','seed-V5-ch2-sc1-sh2',1750101040);
 -- 第 3 章《尚未展开的一章》：0 场 0 镜 ⇒ 按「每镜每阶段一行」自然产出 0 行，不插。
 -- 这里不补占位行：UNIQUE 键是 (chapter_id,stage,scene_ord,shot_ord)，插 scene_ord=0 的
 -- 假行会让读方 `ORDER BY scene_ord,shot_ord` 把它排在真实场之前，是假数据。

 -- ============================================================================
 -- 第六轮扩充：**把数据面铺开**（2026-09-30）
 -- ============================================================================
 -- 为什么：接「图片位点 / 派生链 / 状态机」这类改动时，一处接线会牵到若干分支，而 fixture
 -- 的数据面太窄 —— 窄到「页面拍到了」不等于「分支跑到了」。扩充前**数出来**的实际覆盖：
 --
 --   entities              3   （person×2 + item×1 ⇒ 只跑过 2/31 种 kind）
 --   chapters/scenes/shots 3 / 3 / 7
 --   volumes               2
 --   foreshadowings        3 · scene_foreshadows 3 · stage_artifacts 45
 --   visual_assets         2   （READY + STALE ⇒ 只跑过 2/8 种状态）
 --   visual_artifacts      0   ← **一行都没有**
 --
 -- 由此产生的覆盖洞（都是「页面拍到了、分支没跑到」）：
 --   · `visual_artifacts` 0 行 ⇒ 派生链（front→turnaround→base_body→wardrobe）、
 --     `layers / layersDone / degraded`、以及「front+turnaround 都 DONE 才有一致性对比数据」
 --     这几块在证据图里一次都跑不到；
 --   · 资产状态只有 2/8 ⇒ 状态 Tag 的 8 种色调大部分没画过；
 --   · 层状态只可能是 DONE/PENDING（`NovelAssetPipeline` 只写这两个）⇒ RUNNING / FAILED
 --     两条分支在 UI 上等于死的；
 --   · `entities.kind` 只有 2 种 ⇒ kind 筛选 chip、分组树、`AssetKindLabel` 的
 --     **表外原样回显**那条分支没跑过。
 --
 -- 规矩照旧：`_fixture_data.sql` / `_fixture_schema.sql` 是留档，**不动**，一律追加到本文件。
 -- 新行一律用 **100+ 号段**，与前面几轮的 1..N 不冲突。

 -- ---- 1. 实体：把 kind 铺开 ----
 -- `AssetKindLabel` 认得 6 类共 9 种：person/character=角色、location=地点、
 -- item/prop/treasure=物品、clothing=服装、faction=势力、event=事件；
 -- 其余**原样回显**。所以下面刻意混入 `creature` / `secret` / `mystery` / `resource` ——
 -- 四个都在 `kind::` 的 31 个取值里（是合法产品数据），但**不在**那张映射表里，
 -- 正好用来验回退分支。
 -- 另外三条边界：空名字（UI 有 `name.empty() ? "—"` 这条分支）、重名（分组树两行同名）、
 -- 超长名（省略号截断）。
 INSERT INTO entities(id,kind,name,summary,status,created_chapter,updated) VALUES
   (101,'location','阁楼层','二楼尽头那间没有窗的房间。','active',1,1756000200),
   (102,'clothing','藏青呢大衣','林昭常穿的那件，右袖口有一道补线。','active',1,1756000250),
   (103,'prop','铜制门牌','门牌上的字被磨掉了一半。','active',2,1756000300),
   (104,'character','沈宛','邮局的夜班分拣员。','active',3,1756000350),
   (105,'faction','雨夜邮局','只在夜里派件的邮局。','active',3,1756000400),
   (106,'treasure','铁皮匣','匣子里装着前六封信。','active',2,1756000450),
   (107,'event','第七封来信送达','整本书的起点。','active',1,1756000500),
   (108,'creature','雨里的黑猫','总在门廊第三级台阶上。','active',2,1756000550),
   (109,'secret','那三天的去向','没人知道那三天他在哪。','active',4,1756000600),
   (110,'mystery','前六封信是谁寄的','贯穿全书的谜面。','active',1,1756000650),
   (111,'resource','阁楼的煤油','只够点七个晚上。','active',2,1756000700),
   (112,'person','','空名字：验 `name.empty()` 那条破折号分支。','active',6,1756000750),
   (113,'person','周姨','重名实体：与 #2 同名但不是同一个人，验分组树里两行同名都画得出。','active',6,1756000800),
   (114,'person','一个名字长到必须在界面上省略号截断的角色','验卡片与侧栏树的 ellipsis 截断。','active',7,1756000850);

 -- ---- 2. 视觉资产：把 8 种状态铺满 ----
 -- 状态机 8 值（`NovelVisual.h:27-30`）：PENDING | PROMPTING | REF_READY | SHEET_READY |
 -- WARDROBE_READY | READY | FAILED | STALE。
 -- `sheet_rel_path` 的几种情形各有：真文件 / **坏文件**（在盘上但不是 PNG ⇒ 解码失败）/
 -- **文件不存在** / **中文文件名**（验路径 UTF-8 往返）/ 空（还没出图）。
 -- ⚠️ 每张图都用**不同颜色**：取证的像素计数要求「数到某个色值只能来自某一张」。
 INSERT INTO visual_assets(id,entity_id,kind,name,base_desc,materials_colors,permanent_tags_json,sheet_rel_path,canon_status,status,note) VALUES
   (101,101,'location','阁楼层 · 场景基线','斜顶，唯一的窗朝内院。','木梁 / 煤油灯','["夜"]','assets/阁楼层_基线.png','DRAFT','READY',''),
   (102,102,'clothing','藏青呢大衣 · 服装基线','右袖口补线，领口磨白。','厚呢 / 铜扣','["常服"]','','DRAFT','PENDING','还没开始'),
   (103,103,'prop','铜制门牌 · 器物基线','铜绿斑驳。','铜','[]','','DRAFT','PROMPTING','提示词已发，等回图'),
   (104,104,'character','沈宛 · 外观基线','邮局夜班，袖套上有墨点。','墨绿工装 / 白衬衫','["配角"]','assets/shenwan_ref.png','DRAFT','REF_READY','只有正脸'),
   (105,105,'location','雨夜邮局 · 外景','只在夜里开的那扇小窗。','青砖 / 铁栅','["夜"]','assets/youju_sheet.png','DRAFT','SHEET_READY',''),
   (106,106,'item','铁皮匣 · 器物基线','匣盖有六道划痕。','铁皮 / 锡','[]','','DRAFT','WARDROBE_READY',''),
   (107,108,'character','雨里的黑猫 · 外观基线','湿透的黑毛，右耳缺一角。','黑毛 / 黄眼','[]','assets/broken_sheet.png','DRAFT','FAILED','文件在盘上，但不是 PNG ⇒ 解码失败'),
   (108,114,'character','超长名角色 · 外观基线','用来验长名与失败回退。','——','[]','assets/missing_sheet.png','DRAFT','STALE','路径指向一个**不存在**的文件');

 -- ---- 3. 派生链：`visual_artifacts`（扩充前**一行都没有**）----
 -- 层的名字与顺序来自 `NovelAssetPipeline.cpp` 的 `kChain`
 -- （front → turnaround → base_body → wardrobe；四层全 DONE 才是 READY）。
 -- 层状态 4 值：PENDING | RUNNING | DONE | FAILED。
 -- 资产 1（林昭，READY）：四层全 DONE ⇒ 与 `RecomputeAssetStatus` 的口径自洽
 --                       （其中 wardrobe 带 `degraded=1`，验 K28「降级必须可见」的显示）
 -- 资产 2（周姨，STALE）：DONE / RUNNING / PENDING 各一条 ⇒ 验「部分就绪」
 -- 资产 107（黑猫，FAILED）：DONE + FAILED 各一条 ⇒ 验失败层
 INSERT INTO visual_artifacts(id,asset_id,layer,chapter_scope,chapter_to,rel_path,parent_artifact_id,prompt_artifact_id,job_id,status,degraded,note,created,updated) VALUES
   (101,1,'front',0,0,'assets/chain/front.png',0,0,'seed-job-A1-front','DONE',0,'',1750100000,1750100100),
   (102,1,'turnaround',0,0,'assets/chain/turnaround.png',101,0,'seed-job-A1-turn','DONE',0,'',1750100200,1750100300),
   (103,1,'base_body',0,0,'assets/chain/base_body.png',102,0,'seed-job-A1-body','DONE',0,'',1750100400,1750100500),
   (104,1,'wardrobe',0,0,'assets/chain/wardrobe.png',103,0,'seed-job-A1-ward','DONE',1,'降级：超期未就绪，先用上一版服装出图（K28）',1750100600,1750100700),
   (105,2,'front',0,0,'assets/chain/front_zhouyi.png',0,0,'seed-job-A2-front','DONE',0,'',1750101000,1750101100),
   (106,2,'turnaround',0,0,'assets/chain/turnaround_zhouyi.png',105,0,'seed-job-A2-turn','DONE',0,'',1750101200,1750101300),
   (107,2,'base_body',0,0,'',106,0,'seed-job-A2-body','RUNNING',0,'正在出图',1750101400,1750101500),
   (108,2,'wardrobe',0,0,'',107,0,'','PENDING',0,'等 base_body',1750101600,1750101700),
   (109,107,'front',0,0,'assets/chain/front.png',0,0,'seed-job-A7-front','DONE',0,'',1750102000,1750102100),
   (110,107,'turnaround',0,0,'',109,0,'seed-job-A7-turn','FAILED',1,'降级也失败了：模型超时',1750102200,1750102300);

 -- ---- 4. 规模：第 3 卷 + 第 4–12 章 ----
 -- 为什么：3 章 7 镜时「章节选择条 / 侧栏树 / 时间线横滚」都**撑不满一屏**，
 -- 于是滚动、溢出、分组这些分支跑不到。补到 12 章，其中第 7–12 章**刻意没有场**
 -- （只验章节点本身），第 4–6 章各有场与镜。
 INSERT INTO volumes(id,title,ord,summary) VALUES
   (3,'第三卷 · 回声',3,'把前两卷的线索收进来的一卷');

 INSERT INTO chapters(id,volume_id,ord,title,status,summary,body,words,updated) VALUES
   (101,2,4,'门廊第三级台阶','draft','黑猫第一次出现。','正文省略',5200,1756001000),
   (102,2,5,'分拣间的夜班','revised','沈宛交代了邮戳的事。','正文省略',8800,1756001100),
   (103,3,6,'空名字的那一夜','draft','','',0,1756001200),
   (104,3,7,'六个划痕','draft','铁皮匣被打开。','正文省略',7300,1756001300),
   (105,3,8,'点灯的七晚','draft','煤油见底。','正文省略',4100,1756001400),
   (106,3,9,'尚未展开的一章','draft','','',0,1756001500),
   (107,3,10,'尚未展开的一章','draft','','',0,1756001600),
   (108,3,11,'尚未展开的一章','draft','','',0,1756001700),
   (109,3,12,'一个标题长到必须在章节条上省略号截断的收束章','draft','','',0,1756001800);

 INSERT INTO scenes(id,chapter_id,ord,title,time_label) VALUES
   (101,101,1,'门廊·第三级台阶','夜'),
   (102,101,2,'门廊·雨停','夜'),
   (103,102,1,'邮局·分拣间','夜'),
   (104,104,1,'阁楼·铁皮匣','夜'),
   (105,105,1,'阁楼·灯下','夜');

 INSERT INTO shots(scene_id,ord,action,expression,mood,dialogue,narration,canon_status,timeline_json) VALUES
   (101,1,'黑猫从台阶下探出头','警惕','压抑','','它右耳缺了一角。','APPROVED','{"duration_s":3.0}'),
   (101,2,'他蹲下去伸手','迟疑','克制','你也在等第七封？','猫没有躲。','APPROVED','{"duration_s":4.0}'),
   (101,3,'雨停了，水滴从檐角落下','平静','压抑','','屋檐把夜空切成一长条。','PROPOSED','{"duration_s":2.0}'),
   (102,1,'他直起身看向邮局方向','警觉','紧绷','','远处有一盏灯还亮着。','PROPOSED','{"duration_s":3.5}'),
   (103,1,'沈宛把一叠信推过来','疲惫','冷静','邮戳是三天后盖的。','分拣间的灯管在闪。','APPROVED','{"duration_s":5.0}'),
   (103,2,'他一张一张翻信封','专注','紧绷','','编号是连续的，日期不是。','APPROVED','{"duration_s":6.5}'),
   (103,3,'她把最后一封按住','犹豫','压抑','这封不该在这儿。','她的手停在信封上。','APPROVED','{"duration_s":4.5}'),
   (103,4,'两个人同时沉默','复杂','压抑','','窗外有车经过，灯扫过墙面。','PROPOSED','{"duration_s":3.0}'),
   (104,1,'匣盖被抬开一条缝','紧张','紧绷','','六道划痕对应六封信。','APPROVED','{"duration_s":4.0}'),
   (104,2,'他从匣里取出第一封','克制','压抑','第一封是我写给自己的。','纸已经发脆。','APPROVED','{"duration_s":5.5}'),
   (105,1,'煤油灯的火苗缩了一截','疲惫','压抑','','灯芯只剩最后一段。','PROPOSED','{"duration_s":3.0}'),
   (105,2,'他把灯吹灭','决然','紧绷','不能再点了。','黑暗里只剩雨声。','PROPOSED','{"duration_s":2.5}');
