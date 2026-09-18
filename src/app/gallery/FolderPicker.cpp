#include "app/gallery/FolderPicker.h"

#include "app/FileDialog.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "gallery/Gallery.h"

namespace shine::app::gallery {

std::string PickFolder(std::string_view title, std::string_view initialDir) {
    return app::SelectFolderDialog(title, initialDir);
}

std::string PickImageFolderForGallery() {
    const std::string picked = PickFolder("选择图片文件夹", Settings().galleryLocalDir);
    if (picked.empty()) {
        return {}; // 用户取消：不动设置
    }
    if (picked != Settings().galleryLocalDir) {
        Settings().galleryLocalDir = picked;
        SaveSettings(); // 记住"上次目录"（G-S0 的字段，反射序列化自动读写）
    }
    log::Info("图库：已选择图片文件夹 {}", picked);
    return picked;
}

bool PickAndScanLocalFolder() {
    const std::string picked = PickImageFolderForGallery();
    if (picked.empty()) {
        ::shine::gallery::State().message = "已取消选择文件夹";
        return false;
    }
    return ::shine::gallery::RequestScan(::shine::gallery::SourceKind::Local);
}

} // namespace shine::app::gallery
