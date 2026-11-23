#include "ui/imgui/verify/ReviewFixture.h"

#include "paint/PngCodec.h"
#include "util/Encoding.h" // PathFromUtf8：中文文件名那条分支要走它
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
        std::uint32_t rgb;  // 0xRRGGBB
        const char* covers; // 覆盖的是哪一条分支（写清楚，别让它变成随手加的素材）
    };
    // 颜色从 ReviewFixture.h 的常量来（取真的像素判据按同一份值数）——
    // 这里再写一遍字面量就多了一个「两处要对上」的同步点。
    //
    // ⚠️ 每一张都必须**只被一个位点用**，且颜色在五套主题调色板之外：取真的判据是
    //    「数到某个色值有几个像素」，两张图共色就分不清是谁画上来的。
    const SheetSeed sheets[] = {
        {"assets/linwan_base.png", kSheetColorA, "资产网格卡缩略图 + 详情页基线图（实体 1）"},
        {"assets/zhouyi_base.png", kSheetColorB, "同上（实体 2，验切选中之后图跟着换）"},
        // 下面几张是 2026-09-30 扩充数据面时补的：`visual_assets` 的 8 种状态里
        // 「有真图」的那几种各给一张。
        {"assets/shenwan_ref.png", 0x00A3FFu, "REF_READY（只有正脸，没有整版设定图）"},
        {"assets/youju_sheet.png", 0xF97316u, "SHEET_READY"},
        // ⚠️ **中文文件名**：验「UTF-8 字符串 → std::filesystem::path」这一趟往返。
        //    这条路必须走 `util::PathFromUtf8`（用窄字符串直接构造 path 会按系统 ANSI
        //    码页解释那些 UTF-8 字节，中文就成了乱码、文件打不开）。
        {"assets/阁楼层_基线.png", 0xEC4899u, "非 ASCII 路径（UTF-8 往返）"},
        // 派生链四层（`visual_artifacts` 挂在资产 1 上，四层全 DONE）。
        {"assets/chain/front.png", kChainColorFront, "派生链 · front"},
        {"assets/chain/turnaround.png", kChainColorTurn, "派生链 · turnaround"},
        {"assets/chain/base_body.png", kChainColorBody, "派生链 · base_body"},
        {"assets/chain/wardrobe.png", kChainColorWardrobe, "派生链 · wardrobe"},
        {"assets/chain/front_zhouyi.png", 0x0EA5E9u, "派生链 · front（资产 2）"},
        {"assets/chain/turnaround_zhouyi.png", 0xA855F7u, "派生链 · turnaround（资产 2）"},
        // 分镜页时间线卡片（`.tthumb`）的**逐镜成片图**：来源是 `generated_images` 的
        // `source_kind='shot'` + `source_id=shots.id`。`assets/shot/s4_missing.png` 这
        // 一条**刻意不造**（`generated_images` 里登记了、文件不在）⇒ 回退到设计稿的 `Art`。
        {"assets/shot/s1.png", kShotColor1, "分镜 · 第 1 镜成片图（也被成片条 / 出图结果页数）"},
        {"assets/shot/s2.png", kShotColor2, "分镜 · 第 2 镜成片图（成片条第 2 格）"},
        // `generated_images` 里**非 shot** 来源的一条（`source_kind='asset'`，见
        // `_fixture_seed.sql` 末尾那行）。总控「最近产物」卡读这张表时**不限** source_kind
        // —— 这一张就是那条查询的判据对象（同时它是 id 最大的那条，排在最前）。
        {"assets/gen_recent_ref.png", kRecentAssetColor, "总控 · 最近产物（非 shot 来源）"},
    };
    for (const SheetSeed& sheet : sheets) {
        constexpr std::uint32_t kSide = 64;
        std::vector<std::byte> rgba(static_cast<std::size_t>(kSide) * kSide * 4);
        const auto byteAt = [&sheet](int shift) {
            return static_cast<std::byte>((sheet.rgb >> shift) & 0xFFu);
        };
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            rgba[i] = byteAt(16);     // R
            rgba[i + 1] = byteAt(8);  // G
            rgba[i + 2] = byteAt(0);  // B
            rgba[i + 3] = std::byte{0xFF};
        }
        const auto png = paint::EncodePng(kSide, kSide, rgba);
        if (!png.has_value()) {
            return false;
        }
        const auto* raw = reinterpret_cast<const char*>(png->data());
        // 路径一律过 `util::PathFromUtf8`：上面那张中文名走的就是这条路。
        if (!util::WriteFileEnsuredDir(root / util::PathFromUtf8(sheet.rel),
                                      std::string_view(raw, png->size()))) {
            return false;
        }
    }

    // ---- 一个**刻意做坏**的输入：让「解码失败」这条分支真的被跑到 ----
    //
    // `assets/broken_sheet.png`：文件在盘上、后缀也对，但**内容不是 PNG**。
    // `visual_assets` #107（黑猫，status=FAILED）指着它 ⇒ 解码失败 ⇒ 回退占位画。
    // 另外 `visual_assets` #108（STALE）指着 `assets/missing_sheet.png`，那个文件
    // **刻意不造**（路径不存在）。
    //
    // 为什么非要造坏输入：「解码失败」「路径不存在」「压根没请求」「拍得太早」在截图里
    // **长得一模一样**（都是「这张图没画」）。这一族已经栽过一次（见 PROGRESS 的 R8），
    // 所以两种坏输入各有**一条真实数据行**指着 —— 这才谈得上「回退路径被验过」。
    if (!util::WriteFileEnsuredDir(root / "assets/broken_sheet.png",
                                   std::string_view("this-file-is-not-a-png\n"))) {
        return false;
    }
    return true;
}

} // namespace shine::imguiverify::detail
