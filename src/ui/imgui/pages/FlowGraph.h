#pragma once
// shine::pages —— 出图 / 出片两页共用的**节点图内容**（FlowGraph.cpp）
//
// 为什么单独一个模块：两张流程页的节点表结构不同、副行数据源也不同，但**「数据变没变
// 要不要重建」「首帧要不要 fit」「队列跑没跑」这三件事是同一份逻辑**。原先它们是
// WorkspaceB.cpp 顶部一个匿名命名空间里的全局量，两页的 Draw 各自直接摸。
// 拆成匿名命名空间之后跨 .cpp 够不着，于是这里开一个**只读 + 一个待适配标志**的窄接口：
//
//   * 只导出页面真的要用到的东西（DataKey / ComfyRunningForBadge / 两个 fit 标志）；
//   * Make*FlowNodes / Make*FlowLinks / StageStateFromStatus 一律留在 .cpp 内部 ——
//     节点表是实现细节，导出就等于允许第二处定义它。
//
// ⚠️ 不导出 ComfyHasRunning()：它与 ComfyRunningForBadge() 口径必须一致（见 .cpp 里
//    两条注释），面板头要的是**便宜**的那条（CountsSnapshot），而 ComfyHasRunning()
//    要整表 vector 拷贝。混用两个名字会让人以为可以随便换。
#include "ui/imgui/pages/WorkspacePages.h"

#include <cstdint>

namespace shine::pages {

// 建图时的**数据指纹**。页面每帧拿它和自己的 `flowDataKey_` 比一次，不等就 BuildGraph()。
//
// ⚠️ 不重建的后果不是「图不刷新」这么轻：novel.db 快照是 `async::RunOnWorker` +
//    `PostToUi` **异步**回投的，而 BuildGraph() 原来只在节点表为空时跑一次 ——
//    也就是工程刚打开、快照还在 worker 上跑的那一刻。于是节点图被**永久**冻结在
//    「— / 本章 0 镜 / 还没有实体出图」，哪怕数据两秒后就到了。
//    这正是「假数据」的另一种形态：不是编的，是**永远停在最空的那一刻**。
//
// ⚠️ 重建会重跑 FlowLayoutNodes ⇒ 布局归位、用户拖过的节点位置丢失。所以指纹里
//    **不放**缩放 / 选中 / 画布尺寸 —— 只有真正影响节点内容的那几项。
std::uint64_t FlowDataKey();

// Comfy 队列里有没有正在跑的任务（面板头「运行中」/「出片中」的唯一判据）。
// 口径与节点图用的 ComfyHasRunning() **完全一致**，走的却是更便宜的那条读数。
bool ComfyRunningForBadge();

// 首帧 fit 标志：建图 / 重建之后都要 fit 一次，而 fit 需要画布尺寸，只有 Draw 时才有。
// 所以标志由 BuildGraph() 置位、由 Draw 消费（读并清零）。
bool& ImageFlowNeedsFit();
bool& VideoFlowNeedsFit();

} // namespace shine::pages
