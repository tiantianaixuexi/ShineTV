#pragma once

#include "CoreMinimal.h"

/**
 * 视频工作台的无 UI 入口（控制台命令）。
 *
 * 为什么要有它：整条链路（解析引用 → 显存门禁 → 上传 → 编译图 → 提交 → 逐节点进度 →
 * 回读 → 落盘）必须在**没有任何面板**的情况下能跑通并验收。面板只是这条链路的一个消费者，
 * 有了这个入口，"是链路坏了还是界面坏了"永远分得清。
 *
 * 暴露的命令：
 *
 *   Shine.H3.CreateTestProject <资产路径> <图1> [图2 ...]
 *       建一个两段链式的测试项目（第二段接第一段末帧），第二段引用 `<Picture 3>` 指代末帧。
 *
 *   Shine.H3.RunProject <资产路径> [-url=http://127.0.0.1:8188] [-python=<python.exe>] [-dryrun]
 *       跑一个项目；结束后把编译出的 API 图写到 Saved/ShineH3/<项目名>_graph.json，
 *       并在能找到 python 时自动调用 Scripts/H3/MediaInfo.py 核对音轨。
 *       加 -dryrun 则只解析引用 + 编译节点图就收工（不碰网络、不花 GPU 时间），
 *       配合 Scripts/H3/DiffProbeGraph.py 就能先核对图接得对不对，再决定要不要真跑。
 */
class SHINEEDITOR_API FShineVideoTaskConsole
{
public:
    /** 模块启动时调。 */
    static void Register();

    /** 模块卸载时调。 */
    static void Unregister();
};
