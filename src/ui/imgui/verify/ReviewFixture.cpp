#include "ui/imgui/verify/ReviewFixture.h"

#include "paint/PngCodec.h"
#include "util/File.h"

#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

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

    // ---- 两张**真 PNG**：设定图链路的唯一可验证输入 ----
    //
    // ⚠️ 原来这里根本没有这两个文件：`visual_assets.sheet_rel_path` 指向
    //    `assets/linwan_base.png` / `assets/zhouyi_base.png`，而 fixture 只写了
    //    `work/` 与 `output/`。于是「界面到底能不能显示真实设定图」这条链在取证里
    //    **从来没有输入** —— 它报红还是报绿，都跟链子通没通无关。
    //
    // ⚠️ 也不写文本桩（`output/*.png` 里那几个就是文本桩：后缀 .png、内容是
    //    `"not-a-real-png-fixture"`）。文本桩的后果是「解码失败」与「压根没画」
    //    在截图上长得一模一样，而这正是这一族最会骗人的地方。
    //    这里用工程自带的 `paint::EncodePng`（libpng 封装）造**合法**的 PNG。
    //
    // 为什么是纯色：取证的像素计数可以直接判 —— 颜色出现在截图里 = 解码 + 上传 +
    // 绘制整条链通了；颜色不在 = 链子断了。两个颜色刻意选在**五套主题的调色板之外**
    // （#FF2D55 / #7CFF00），免得和别的控件撞色导致计数分不清来源。
    struct SheetSeed {
        const char* rel;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
    };
    // 颜色从 ReviewFixture.h 的常量来（取真的像素判据按同一份值数）——
    // 这里再写一遍字面量就多了一个「两处要对上」的同步点。
    const SheetSeed sheets[] = {
        {"assets/linwan_base.png", static_cast<std::uint8_t>(kSheetColorA >> 16),
         static_cast<std::uint8_t>(kSheetColorA >> 8), static_cast<std::uint8_t>(kSheetColorA)},
        {"assets/zhouyi_base.png", static_cast<std::uint8_t>(kSheetColorB >> 16),
         static_cast<std::uint8_t>(kSheetColorB >> 8), static_cast<std::uint8_t>(kSheetColorB)},
    };
    for (const SheetSeed& sheet : sheets) {
        constexpr std::uint32_t kSide = 64;
        std::vector<std::byte> rgba(static_cast<std::size_t>(kSide) * kSide * 4);
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            rgba[i] = static_cast<std::byte>(sheet.r);
            rgba[i + 1] = static_cast<std::byte>(sheet.g);
            rgba[i + 2] = static_cast<std::byte>(sheet.b);
            rgba[i + 3] = static_cast<std::byte>(0xFF);
        }
        const auto png = paint::EncodePng(kSide, kSide, rgba);
        if (!png.has_value()) {
            return false;
        }
        const auto* raw = reinterpret_cast<const char*>(png->data());
        if (!util::WriteFileEnsuredDir(root / std::filesystem::path(sheet.rel),
                                      std::string_view(raw, png->size()))) {
            return false;
        }
    }
    return true;
}

} // namespace shine::imguiverify::detail
