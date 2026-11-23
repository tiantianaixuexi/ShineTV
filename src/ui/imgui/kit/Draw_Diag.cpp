// kit 取证自检计数（自 Draw.cpp 拆出，第二十轮）：反向矩形 / 悬停探针 /
// 同帧重复命中三族，都是「只记状态、不画东西」的纯诊断逻辑。
//
// 拆分理由：Draw.cpp 里它们夹在绘制原语中间，而它们的读者是取证层
// （Review* / manifest），不是绘制调用点 —— 按读者分文件与 Shell_Sidebar /
// Shell_Inspector 的拆法同一道理。
//
// ⚠️ 这三族的状态语义（唯一一份、按帧分桶、只比同 draw list、豁免一次性）
//    全部留在原注释里随代码一起搬，不改一行逻辑。
#include "ui/imgui/kit/Draw.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace shine::kit {

namespace {
// 反向矩形自检的**唯一一份**状态。三个对外函数共用它 —— 早先各写一个函数内
// static，结果 count 和 last 互不相干，Reset 也是空壳，等于没记。
struct InvertedRectState {
    int count = 0;
    char last[128] = {};
};
InvertedRectState& InvertedState() {
    static InvertedRectState state;
    return state;
}
} // namespace

void NoteInvertedRect(float minX, float minY, float maxX, float maxY, const char* where) {
    InvertedRectState& state = InvertedState();
    ++state.count;
    std::snprintf(state.last, sizeof(state.last), "%s min=(%.0f,%.0f) max=(%.0f,%.0f)", where,
                  minX, minY, maxX, maxY);
    // 只报前 8 次：同一个控件每帧都会触发一次，不限量的话日志会被刷爆，
    // 真正的「唯一一处」反而被埋掉。判据用的是计数，不是条数。
    if (state.count <= 8) {
        shine::log::Error("kit: 反向矩形 —— {} 会被整块丢弃（画不出、也点不到）", state.last);
    }
}
int InvertedRectCount() { return InvertedState().count; }
void ResetInvertedRectCount() { InvertedState() = InvertedRectState{}; }
const char* LastInvertedRect() { return InvertedState().last; }

namespace {
// 命中自检的**唯一一份**状态，理由与 InvertedState 相同：共用才有意义。
struct HoverProbeState {
    int count = 0;
    char last[128] = {};
};
HoverProbeState& HoverState() {
    static HoverProbeState state;
    return state;
}
} // namespace

void NoteHoveredItem(const char* id) {
    HoverProbeState& state = HoverState();
    ++state.count;
    std::snprintf(state.last, sizeof(state.last), "%s", id != nullptr ? id : "(null)");
}
int HoveredItemCount() { return HoverState().count; }
void ResetHoveredItemCount() { HoverState() = HoverProbeState{}; }
const char* LastHoveredItem() { return HoverState().last; }

namespace {
// 重复命中的状态。与 InvertedState 不同的两点：
//   1. 按**帧**分桶（ImGui::GetFrameCount）—— 同一矩形每帧都命中是正常的，
//      跨帧不清空的话计数会涨到几万，判据就废了。
//   2. 只比对**矩形**不看 id：漏写的是「同一个矩形上的第二次命中」，而调用点
//      通常还换了个 id（`dock-report-3` vs `dock-report-hit-3`），比 id 抓不到。
//
// ⚠️ 这里用 4 个 float 而不是 `Rect`：那个类型声明在 Widgets.h，而 Widgets.h
//    依赖本文件（Draw.h），反向包含就成环。Draw.h 里的 NoteInvertedRect 当年
//    也是同样原因收四个裸 float。
struct HitRect {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    // 诊断用：登记这个热区的 id。**不参与判据** —— 判据只比矩形（理由见上面第 2 条），
    // 这里纯粹是为了「报出来的时候能说出对方是谁」。没有它就得拿坐标去反推，
    // 实测反推过一轮：`(296,742)-(538,934)` 这个 242×192 的盒子光看数字认不出来。
    std::string id;
    // 登记时所在的 draw list（一个窗口一个，指针一一对应）。**参与判据** —— 见
    // `NoteDuplicateHit` 里那段（跨 list 不算）。用 draw list 而不是 ImGuiWindow：
    // 后者是内部类型（要 imgui_internal.h），而**裁剪区本来就挂在 draw list 上**——
    // 拿它判同一性，语义比「同一个窗口」还贴。
    const ImDrawList* list = nullptr;
};
struct DuplicateHitBucket {
    int frame = -1;
    std::vector<HitRect> rects;
    int overlaps = 0;   // 本帧累计的重叠次数（只数**没声明过**的）
    char last[160] = {};
    // 本帧内被 DeclareNestedHit 声明过的子项 id。只对**紧接着的一次**同 id 命中生效。
    std::string declaredChild;
};
DuplicateHitBucket& DuplicateBucket() {
    static DuplicateHitBucket bucket;
    return bucket;
}

// a 是否把 b 整个盖住（或反之）。要求**严格包含**至少一条边：
// 两个完全一样大的矩形也算包含 —— 那正是「同一矩形注册两次」的典型形态，
// 但它只该在**声明过**的情况下被豁免，否则照样报（见 DeclareNestedHit）。
bool ContainsEitherWay(const HitRect& a, const HitRect& b) {
    const bool aWrapsB = a.minX <= b.minX && a.minY <= b.minY && a.maxX >= b.maxX && a.maxY >= b.maxY;
    const bool bWrapsA = b.minX <= a.minX && b.minY <= a.minY && b.maxX >= a.maxX && b.maxY >= a.maxY;
    return aWrapsB || bWrapsA;
}
} // namespace

void DeclareNestedHit(std::string_view childId) {
    DuplicateHitBucket& bucket = DuplicateBucket();
    const int frame = ImGui::GetFrameCount();
    if (bucket.frame != frame) {
        bucket.frame = frame;
        bucket.rects.clear();
        bucket.declaredChild.clear();
    }
    bucket.declaredChild.assign(childId);
}

void NoteDuplicateHit(float minX, float minY, float maxX, float maxY, const char* id) {
    DuplicateHitBucket& bucket = DuplicateBucket();
    const int frame = ImGui::GetFrameCount();
    if (bucket.frame != frame) {
        bucket.frame = frame;
        bucket.rects.clear();
        // 声明**只在当帧有效**：跨帧留着的声明会去豁免一个跟它毫无关系的命中。
        bucket.declaredChild.clear();
    }
    // 声明是**一次性**的：取走就清空。这样同一次声明不可能连豁免两处，
    // 而且「声明了但没对上」时下一次会照常报。
    const bool declared = !bucket.declaredChild.empty() && bucket.declaredChild == id;
    if (declared) {
        bucket.declaredChild.clear();
    }
    const HitRect r{minX, minY, maxX, maxY, std::string(id), ImGui::GetWindowDrawList()};
    for (const HitRect& seen : bucket.rects) {
        // ⚠️ **只在同一个 draw list 里比**（= 同一个窗口）。跨 list 的「重叠」不是这个
        //    自检要抓的东西：裁剪区按 draw list 生效，被裁掉的 item **根本不参与
        //    hover**（`ItemHoverable` → `IsClippedEx`），所以「滚动区 / 子窗口里的内容
        //    溢出到浮层或底栏上」**抢不走** hover —— 那只是绘制被裁、热区登记照旧。
        //
        //    早先这里不分 list，于是这一族被逐帧计成冲突：把 fixture 从 3 章铺到 12 章
        //    之后一次报了 **425** 次，凶手两个**都是跨 list 的**：
        //      · `side-tree` 的章行（侧栏）压到底栏；
        //      · `asset-card-12`（资产网格的滚动内容）压到底栏报告行。
        //    后者本来就是**合法的**（工作区是可滚动的，内容当然比视口长），前者是真溢出
        //    （顺带修了，见 `Widget_Tree.cpp`）。判据把两种混在一起报，就会把「版式溢出」
        //    与「合法的滚动内容」都误判成「控件点不动」。
        if (seen.list != r.list) {
            continue;
        }
        // 有实际面积的交叠才算。相邻（贴边）或零面积不算。
        const float w = std::min(seen.maxX, r.maxX) - std::max(seen.minX, r.minX);
        const float h = std::min(seen.maxY, r.maxY) - std::max(seen.minY, r.minY);
        if (w > 0.0f && h > 0.0f) {
            // 设计性嵌套：调用方在注册子 item 前用 DeclareNestedHit 声明过，
            // **且**两个矩形确实是包含关系 ⇒ 这是 `.tree .node` 套 `.tw` 那一类，
            // 不算重复。豁免后仍把子矩形登记进本帧表，之后若有第三个无关控件
            // 压在它上面，照样会报。
            //
            // ⚠️ 早先这里只有「有面积就算」一条判据，而上面的注释却写着
            //    「父子结构里父矩形包含子矩形是设计如此，不算重复」——
            //    注释和代码说的不是一回事，于是小说侧栏那两个 `.tw` 箭头被
            //    逐帧计了一遍（815 次/轮），整个 regression 恒为 FAIL。
            if (declared && ContainsEitherWay(seen, r)) {
                bucket.rects.push_back(r);
                return;
            }
            // ⚠️ 把**对方**的矩形**和 id** 都打出来。只报自己的坐标时，「跟谁撞的」得靠猜：
            //    第一次实测报 `side-tree#t0##0 ... 交叠 14x1`（看不出对方是谁），
            //    补了矩形之后靠 y 认出了一处；第二次实测剩的是
            //    `(296,742)-(538,934)` 这个 242×192 的盒子 —— 光看数字认不出来，
            //    所以 id 也得带上。诊断信息不够，就要多花一整轮去反推。
            std::snprintf(bucket.last, sizeof(bucket.last),
                          "%s min=(%.0f,%.0f) max=(%.0f,%.0f) 与 %s min=(%.0f,%.0f) max=(%.0f,%.0f) "
                          "交叠 %.0fx%.0f",
                          id, minX, minY, maxX, maxY, seen.id.c_str(), seen.minX, seen.minY,
                          seen.maxX, seen.maxY, w, h);
            // 只报前 8 次：同一处每帧都会触发，不限量会把真正的「唯一一处」埋掉。
            // 判据读的是计数，不是条数。
            if (++bucket.overlaps <= 8) {
                shine::log::Error(
                    "kit: 同一帧内重叠热区 —— {} —— ImGui 先注册者独占，后一个永远 "
                    "clicked=false（控件画得出来但按不动）",
                    bucket.last);
            }
            return;
        }
    }
    bucket.rects.push_back(r);
}
int DuplicateHitCount() { return DuplicateBucket().overlaps; }
void ResetDuplicateHitCount() { DuplicateBucket() = DuplicateHitBucket{}; }
const char* LastDuplicateHit() { return DuplicateBucket().last; }

} // namespace shine::kit
