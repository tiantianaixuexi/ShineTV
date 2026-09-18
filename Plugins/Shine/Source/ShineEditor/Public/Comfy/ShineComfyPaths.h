#pragma once

#include "CoreMinimal.h"
#include "Comfy/ShineComfyTypes.h"

/**
 * ComfyUI 产出的本地路径解析。
 *
 * history 里给的只有 filename / subfolder（加一个 type），这里把它们拼成磁盘上的
 * 绝对路径，让 Shine 直接读 ComfyUI 写出的原始文件（不复制、不导入资产）。
 *
 * 视频和图片分属不同目录：SaveVideo 的 filename_prefix 通常带 "video/"，
 * 产物会落在 output/video 下，而视频往往要放到容量更大的盘上单独管理，
 * 所以视频目录允许独立配置（留空才退回 output 目录）。
 */
namespace ShineComfyPaths
{
    /** 归一化目录串：反斜杠转正斜杠、去掉尾斜杠。空串原样返回。 */
    SHINEEDITOR_API FString NormalizeDirectory(const FString& Directory);

    /** 配置里的 ComfyUI output 目录（已归一化）；没配就返回空串。 */
    SHINEEDITOR_API FString GetConfiguredOutputDirectory();

    /** 配置里的视频目录；没配就退回 output 目录。 */
    SHINEEDITOR_API FString GetConfiguredVideoDirectory();

    /** 素材库根目录（@image: 引用的解析基准）；没配就退回 <项目>/Saved/ShineMedia。 */
    SHINEEDITOR_API FString GetConfiguredMediaLibraryDirectory();

    /**
     * 拼出图片的本地绝对路径。
     * @param Subfolder history 里的 subfolder（ComfyUI 用正斜杠）
     * @param Filename  history 里的 filename
     */
    SHINEEDITOR_API FString MakeLocalImagePath(const FString& Subfolder, const FString& Filename);

    /** 配置的 output 目录里是否真的能找到这张图。 */
    SHINEEDITOR_API bool LocalImageExists(const FString& Subfolder, const FString& Filename);

    /** 按媒体种类挑目录（视频走视频目录，其余走 output 目录）拼出绝对路径。 */
    SHINEEDITOR_API FString MakeLocalMediaPath(const FShineComfyHistoryMedia& Media);

    /** 媒体文件是否真的已经落盘。 */
    SHINEEDITOR_API bool LocalMediaExists(const FShineComfyHistoryMedia& Media);

    /**
     * 找一张颜色图的兄弟文件（`Scene_xxx_Color.png` → `Scene_xxx_Depth.png`）。
     *
     * 场景捕获（`ShineSceneToImage` 那条链路）写出来的就是"颜色 / 深度 / 法线"三件套，
     * 而两支 ControlNet 正好吃 `_Depth` / `_Normal`——这是一组现成、免费、
     * 不需要用户额外准备的对应关系，所以两条出图链路都按这个约定自动找。
     *
     * @param ChannelSuffix `_Depth` 或 `_Normal`。
     * @return 磁盘绝对路径；找不到返回空串。
     */
    SHINEEDITOR_API FString FindSiblingImage(const FString& ColorImagePath, const TCHAR* ChannelSuffix);

    /**
     * 上传一张本地图时写给 ComfyUI 的文件名。
     *
     * 确定性命名（同一路径永远同一个名字）+ 路径哈希后缀（同名不同目录的两张图不互踩）。
     * 名字里只留 ASCII 安全字符：中文/空格的文件名走 multipart 容易出意外，而 ComfyUI
     * 只把这个名字当 key 用，不需要可读性。
     *
     * 视频链路（`FShineVideoTaskRunner`）与出图链路（`FShineImageTaskRunner`）共用这一份：
     * 两条链路要是各写一份，"上传过的图能不能被另一条链路复用"这种事就会悄悄分叉。
     */
    SHINEEDITOR_API FString MakeUploadFileName(const FString& LocalPath);
}
