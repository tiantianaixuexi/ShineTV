#include "app/FileDialog.h"
#include "util/Encoding.h"

#include "core/Log.h"

#include <windows.h>

#include <shobjidl.h> // IFileOpenDialog / IFileSaveDialog / IShellItem
#include <shlobj.h>

#include <string>
#include <vector>

namespace shine::app {
namespace {

// 转换统一走 `util/Encoding.h`（本项目编码/路径唯一入口），这里只保留短名字方便调用
[[nodiscard]] std::wstring ToWide(std::string_view utf8) { return util::Utf8ToUtf16(utf8); }
[[nodiscard]] std::string ToUtf8(const wchar_t* wide) {
    return wide == nullptr ? std::string{} : util::Utf16ToUtf8(std::wstring_view{wide});
}

// 让对话框定位到 initialDir（不存在/非法时忽略，不影响弹出）
void ApplyInitialDir(IFileDialog* dialog, std::string_view initialDir) {
    if (initialDir.empty()) {
        return;
    }
    const std::wstring wide = ToWide(initialDir);
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&item))) && item != nullptr) {
        dialog->SetFolder(item);
        item->Release();
    }
}

[[nodiscard]] std::string ShowDialog(bool save, bool folders, std::string_view title, std::string_view defaultName,
                                     const std::vector<FileFilter>& filters, std::string_view initialDir) {
    // COM 可能已被初始化（ImGui 后端/其它模块）；RPC_E_CHANGED_MODE 时继续用
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needUninit = SUCCEEDED(coHr);

    const bool useSaveDialog = save && !folders;
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(useSaveDialog ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    std::string result;
    if (SUCCEEDED(hr) && dialog != nullptr) {
        const std::wstring wtitle = ToWide(title);
        if (!wtitle.empty()) {
            dialog->SetTitle(wtitle.c_str());
        }
        if (folders) {
            FILEOPENDIALOGOPTIONS options{};
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        std::vector<std::wstring> names;
        std::vector<std::wstring> patterns;
        std::vector<COMDLG_FILTERSPEC> specs;
        names.reserve(filters.size());
        patterns.reserve(filters.size());
        specs.reserve(filters.size());
        for (const FileFilter& f : filters) {
            names.push_back(ToWide(f.first));
            patterns.push_back(ToWide(f.second));
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            specs.push_back(COMDLG_FILTERSPEC{names[i].c_str(), patterns[i].c_str()});
        }
        if (!specs.empty() && !folders) {
            dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        }
        if (useSaveDialog && !defaultName.empty()) {
            const std::wstring wname = ToWide(defaultName);
            dialog->SetFileName(wname.c_str());
        }
        ApplyInitialDir(dialog, initialDir);
        if (dialog->Show(nullptr) == S_OK) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = ToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    } else {
        log::Warn("文件对话框创建失败（hr=0x{:08X}）", static_cast<unsigned>(hr));
    }
    if (needUninit) {
        CoUninitialize();
    }
    return result;
}

} // namespace

std::string OpenFileDialog(std::string_view title, const std::vector<FileFilter>& filters,
                           std::string_view initialDir) {
    return ShowDialog(/*save=*/false, /*folders=*/false, title, {}, filters, initialDir);
}

std::string SaveFileDialog(std::string_view title, std::string_view defaultName, const std::vector<FileFilter>& filters,
                           std::string_view initialDir) {
    return ShowDialog(/*save=*/true, /*folders=*/false, title, defaultName, filters, initialDir);
}

std::string SelectFolderDialog(std::string_view title, std::string_view initialDir) {
    return ShowDialog(/*save=*/false, /*folders=*/true, title, {}, {}, initialDir);
}

} // namespace shine::app
