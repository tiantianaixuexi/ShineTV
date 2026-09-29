-- 取证 fixture 的**数据**（表结构已由 out/_fixture_schema.sql 建好）
-- ⚠️ 只给 review 流水线用。应用本身不造任何库内容 —— 它只读业务层/库里已有的东西。
--    3 章；第 1 章 2 场 5 镜（镜序跨场连续），第 2 章 1 场 2 镜，第 3 章刻意留空，
--    这样「没取过这一章的镜」那条分支在证据图里也能被看见。

INSERT INTO entities(id,kind,name,summary,status,created_chapter,updated) VALUES
  (1,'person','林昭','第七封信的收信人。全程克制，不解释动机。','active',1,1756000000),
  (2,'person','周姨','阁楼的住户，线索的来源。','active',1,1756000000),
  (3,'item','黄铜钥匙','仓库内场封条上用的钥匙，来源不明。','active',2,1756000100);

INSERT INTO chapters(id,volume_id,ord,title,status,summary,body,words,updated) VALUES
  (1,1,1,'雨夜里的第七封来信','revised','取信人发现信里写着他自己刚做过的事。','正文省略',12480,1756000000),
  (2,1,2,'旧仓库的钥匙','draft','一把不该存在的钥匙出现在门后。','正文省略',6100,1756000100),
  (3,1,3,'尚未展开的一章','draft','','',0,0);

INSERT INTO scenes(id,chapter_id,ord,title,time_label) VALUES
  (1,1,1,'雨夜·门廊','夜'),
  (2,1,2,'雨夜·阁楼','夜'),
  (3,2,1,'仓库·内场','日');

-- 章 1：5 镜，字段填满以拍出检查器的九行属性。
INSERT INTO shots(scene_id,ord,action,expression,mood,dialogue,narration,canon_status,timeline_json) VALUES
  (1,1,'推门进屋，抖落外套上的雨','警惕','克制','这信是寄给谁的？','雨水顺着门缝淌进来。','APPROVED','{"duration_s":4.5}'),
  (1,2,'拆开信封，指腹压过纸纹','迟疑','紧绷','','纸面有折痕，像被人反复读过。','APPROVED','{"duration_s":3.0}'),
  (1,3,'抬头看向楼梯口','警觉','紧绷','谁在上面？','阁楼没有开灯。','APPROVED','{"duration_s":2.5}'),
  (2,4,'踩上第一级台阶','戒备','压抑','','木板在脚下发出轻响。','PROPOSED','{"duration_s":6.0}'),
  (2,5,'在阁楼窗前站定，侧脸被雨照亮','冷静','克制','第七封。前六封都在撒谎。','雨把窗框描成一圈亮边。','APPROVED','{"duration_s":5.0}');

-- 章 2：2 镜。
INSERT INTO shots(scene_id,ord,action,expression,mood,dialogue,narration,canon_status,timeline_json) VALUES
  (3,1,'用钥匙划开封条','平静','冷静','','封条比锁还旧。','PROPOSED','{"duration_s":3.5}'),
  (3,2,'推开内场大门','怔住','惊讶','它一直在这儿。','灰尘在光柱里翻滚。','PROPOSED','{"duration_s":4.0}');
