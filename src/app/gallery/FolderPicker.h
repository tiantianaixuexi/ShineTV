#pragma once
// shine::app::gallery —— 系统「选择文件夹」对话框（G-S4 S3；自 gallery/ui 迁入 app）
//
// 只做一件事：弹出 Win32 目录选择框并返回 **UTF-8 绝对路径**。
// COM 初始化 / `FOS_PICKFOLDERS` / 初始目录定位都在 `app::SelectFolderDialog()` 里
// （那是本仓库目录对话框的**唯一实现**，这里不重复造一份 COM 代码）。
//
// 与任务书 `Plan/任务/G-图片库.md` 的签名差异（有意的）：
//   任务书写的是 `bool PickFolder(std::string& outUtf8Path)`，属 `Doc/RULES-LANG.md` §13.3
//   明令禁止的 `bool + 出参` 形状；这里改用本仓库既有约定 **"返回空串 = 取消/失败"**
//   （与 `app::OpenFileDialog/SaveFileDialog/SelectFolderDialog` 完全一致）。
//
// **只能在 UI 线程调用**（会弹模态框）。
#include <string>
#include <string_view>

namespace shine::app::gallery {

// 取消 / 失败 → 返回空串
[[nodiscard]] std::string PickFolder(std::string_view title, std::string_view initialDir = {});

// 图库专用入口：「选择图片文件夹」，自动带**上次目录记忆**
// （读 `Settings::galleryLocalDir` 作初始目录；选择成功后写回并落盘）
[[nodiscard]] std::string PickImageFolderForGallery();

// 弹目录框选本地文件夹：成功后 `gallery::RequestScan(Local)`；取消写 `gallery::State().message`。
// 原 `gallery::PickAndScanLocalFolder` 下沉到本层（业务层不再弹 UI）。
bool PickAndScanLocalFolder();

} // namespace shine::app::gallery
