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
