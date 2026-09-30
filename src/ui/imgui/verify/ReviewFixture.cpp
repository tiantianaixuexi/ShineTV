#include "ui/imgui/verify/ReviewFixture.h"

#include "util/File.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace shine::imguiverify::detail {

// 造一个**格式与 NovelContinuity.cpp:467 完全一致**的报告集合，只为取证流水线有东西可拍。
//
// ⚠️ 这是**取证流水线的输入 fixture**，写在输出目录下的临时工程里，由本文件自己造。
//    应用侧绝不造报告 —— DrawDockReports 只读 business 层落盘的文件。
//    三章各给一种结论（全过 / 有未过 / 有未核对），列表与模态才拍得出三态的区别：
//    只给一种的话，「有未核对」那条分支在证据图里等于没被覆盖。
bool SeedReportFixture(const std::filesystem::path& root) {
    struct Seed {
        int ord;
        const char* json;
    };
    const Seed seeds[] = {
        {1,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":1,"shots":13,"pairs":12,)"
         R"("rules_checked":12,"failed":0,"unverified":0,"issues":[],"notes":[]})"},
        {2,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":2,"shots":10,"pairs":9,)"
         R"("rules_checked":12,"failed":2,"unverified":0,"issues":[)"
         R"({"code":"C1","severity":"high","detail":"主角外套在第 2 场与第 5 场之间由深灰变为藏青"},)"
         R"({"code":"C7","severity":"low","detail":"第 3 镜缺 prev_shot_id，跨镜比较只用了单侧状态"}],)"
         R"("notes":[]})"},
        {3,
         R"({"stage":"V8","stage_name":"CONTINUITY","chapter_id":3,"shots":8,"pairs":7,)"
         R"("rules_checked":12,"failed":0,"unverified":2,"issues":[],)"
         R"("notes":["C4：场 2 未登记起止状态，时间线无从比较",)"
         R"("C9：第 8 镜没有关联实体，服装一致性无从核对"]})"},
    };
    for (const Seed& seed : seeds) {
        std::ostringstream dir;
        dir << "ch" << std::setw(3) << std::setfill('0') << seed.ord;
        if (!util::WriteFileEnsuredDir(root / "work" / dir.str() / "v08_continuity.json",
                                       seed.json)) {
            return false;
        }
    }
    // 产物页（底栏第 3 个页签）扫的是 `<root>/output`。
    //
    // ⚠️ 以前这个目录是空的，于是那一页只拍到「output/ 目录是空的」——**页面拍到了，
    //    列表分支从来没被执行过**。和「图评审」那轮同一类洞：fixture 缺数据 = 覆盖洞。
    //    造几个**不同扩展名、不同大小**的文件，好让「kind 标签 / 字节数 / 点击打开」
    //    这几处都真的有值可显示。
    struct ArtifactSeed {
        const char* rel;
        const char* body;
    };
    const ArtifactSeed artifacts[] = {
        {"output/ch001_S001.png", "not-a-real-png-fixture"},
        {"output/ch001_S002.png", "not-a-real-png-fixture-2"},
        {"output/ch002_S011.png", "not-a-real-png-fixture-3"},
        {"output/linwan_base_v2.png", "not-a-real-png-fixture-4"},
        {"output/ep001_take01.mp4", "not-a-real-mp4-fixture"},
        {"output/sheet.json", R"({"layout":"1x4","panels":4})"},
    };
    for (const ArtifactSeed& art : artifacts) {
        if (!util::WriteFileEnsuredDir(root / std::filesystem::path(art.rel), art.body)) {
            return false;
        }
    }
    return true;
}

} // namespace shine::imguiverify::detail
