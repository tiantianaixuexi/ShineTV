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
