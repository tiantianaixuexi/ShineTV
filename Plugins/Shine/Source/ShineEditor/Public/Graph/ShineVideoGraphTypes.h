#pragma once

#include "CoreMinimal.h"
#include "ShineVideoGraphTypes.generated.h"

/**
 * 视频工作台画布上 pin 的类别。
 *
 * 之所以单独列一套而不是复用 `Shine.Image` / `Shine.Conditioning`：
 * 视频域的连线语义是"素材 → 分镜 → 分镜图 → 视频组"，和 Comfy 的执行图完全不同，
 * 混用会让 `CanCreateConnection` 的规则没法表达（比如分镜的参考图槽位只收
 * 图片和角色、不收 Latent）。
 *
 * ⚠️ 分镜节点的参考图槽位类别是 `Picture` 而不是 `Image`：槽位要同时收
 * "一张图片"（Image）和"一个角色（会展开成多张图）"（Character），
 * 所以输出侧有两种类别、输入侧只有一种，兼容规则写在 `ShineVideoPin::CanConnect`。
 * 槽位的顺序就是 H3 的 `<Picture 1..N>` 编号顺序（见 H3-SPEC.md 坑 5）。
 */
namespace ShineVideoPin
{
    /** 剧本节点 → 分镜节点：一段可注入的全局设定/风格文本。 */
    static const TCHAR* const Script = TEXT("Shine.Video.Script");

    /** 角色节点 → 分镜节点的参考图槽位：会展开成该角色资产的全部参考图。 */
    static const TCHAR* const Character = TEXT("Shine.Video.Character");

    /** 图片节点 / 分镜图节点 → 分镜的参考图槽位、视频组的段输入。 */
    static const TCHAR* const Image = TEXT("Shine.Video.Image");

    /** 分镜节点上的参考图槽位（收 Image 或 Character）。 */
    static const TCHAR* const Picture = TEXT("Shine.Video.Picture");

    /** 分镜节点 → 分镜图节点：一份"提示词 + 采样参数"的分镜定义。 */
    static const TCHAR* const Shot = TEXT("Shine.Video.Shot");

    /** 视频组的段输出 → 下一段的"链式"输入。 */
    static const TCHAR* const Video = TEXT("Shine.Video.Video");

    /**
     * 这条连线是否合法。
     *
     * 规则很短，但每一条都对应一个必须落在 UI 上的语义，不要随手放宽：
     *   - 同类别永远可以接；
     *   - 图片/角色都能进分镜的参考图槽位（槽位会把它编号成 `<Picture N>`）；
     *   - 别的跨类别一律不许（例如角色不能直接当视频组的段输入——角色不是成品图）。
     */
    inline bool CanConnect(const FString& OutputCategory, const FString& InputCategory)
    {
        if (OutputCategory == InputCategory)
        {
            return true;
        }

        const bool bIsPictureSource =
            OutputCategory == Image ||
            OutputCategory == Character;

        return bIsPictureSource && InputCategory == Picture;
    }
}

/**
 * 画布节点上的运行期状态。
 *
 * 它只影响画布怎么画（状态色块、步进数字），不参与编译，也不写进资产
 * （节点上的对应字段都标了 Transient）。一次任务结束后画布上留着上次的
 * 结果色是好事——用户一眼就知道哪一段跑过。
 */
UENUM()
enum class EShineVideoNodeRuntimeState : uint8
{
    /** 还没跑过。 */
    Idle,

    /** 本次任务里排在后面，等前面的段。 */
    Pending,

    /** 正在跑（带步进时节点上会显示 n/m）。 */
    Running,

    /** 跑完了。 */
    Finished,

    /** 出错了。 */
    Failed
};
