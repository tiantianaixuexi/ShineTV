#include "gallery/FileActions.h"

#include "core/Log.h"
#include "util/Encoding.h"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>

namespace shine::gallery::file_actions {
namespace {

void SetClipboardUtf8(std::string_view utf8) {
    if (!OpenClipboard(nullptr)) {
        log::Warn("图库：打开剪贴板失败");
        return;
    }
    EmptyClipboard();
    const std::wstring wide = util::Utf8ToUtf16(utf8);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) {
        CloseClipboard();
        log::Warn("图库：剪贴板分配失败");
        return;
    }
    void* ptr = GlobalLock(mem);
    if (ptr == nullptr) {
        GlobalFree(mem);
        CloseClipboard();
        return;
    }
    memcpy(ptr, wide.c_str(), bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
}

} // namespace

void CopyText(std::string_view utf8) { SetClipboardUtf8(utf8); }

void CopyPaths(const std::vector<std::filesystem::path>& paths) {
    std::string joined;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            joined += "\n";
        }
        joined += util::PathToUtf8(paths[i]);
    }
    SetClipboardUtf8(joined);
    log::Info("图库：已复制 {} 个路径到剪贴板", paths.size());
}

void CopyNames(const std::vector<std::filesystem::path>& paths) {
    std::string joined;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            joined += "\n";
        }
        joined += util::FileNameToUtf8(paths[i]);
    }
    SetClipboardUtf8(joined);
    log::Info("图库：已复制 {} 个文件名到剪贴板", paths.size());
}

bool RevealInExplorer(const std::filesystem::path& path, std::string* error) {
    if (path.empty()) {
        if (error) {
            *error = "路径为空";
        }
        return false;
    }
    const std::wstring wpath = path.wstring();
    std::wstring arg = L"/select,\"" + wpath + L"\"";
    const HINSTANCE hi = ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<std::intptr_t>(hi);
    if (code <= 32) {
        if (error) {
            *error = "ShellExecuteW 失败 code=" + std::to_string(code);
        }
        log::Warn("图库：资源管理器定位失败 {} code={}", util::PathToUtf8(path), code);
        return false;
    }
    log::Info("图库：已在资源管理器中定位 {}", util::PathToUtf8(path));
    return true;
}

bool Recycle(const std::vector<std::filesystem::path>& paths, std::string* error) {
    if (paths.empty()) {
        if (error) {
            *error = "没有要删除的文件";
        }
        return false;
    }

    // IFileOperation：支持回收站撤销
    IFileOperation* op = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&op));
    if (SUCCEEDED(hr) && op != nullptr) {
        op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI);
        int okCount = 0;
        for (const auto& p : paths) {
            IShellItem* item = nullptr;
            const HRESULT hs = SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item));
            if (FAILED(hr = hs) || item == nullptr) {
                log::Warn("图库：回收站项创建失败 {}", util::PathToUtf8(p));
                continue;
            }
            op->DeleteItem(item, nullptr);
            item->Release();
            ++okCount;
        }
        if (okCount == 0) {
            op->Release();
            if (error) {
                *error = "没有可删除的项";
            }
            return false;
        }
        const HRESULT hp = op->PerformOperations();
        op->Release();
        if (FAILED(hp)) {
            if (error) {
                *error = "IFileOperation 失败 hr=" + std::to_string(static_cast<unsigned long>(hp));
            }
            log::Warn("图库：回收站删除失败 hr={}", static_cast<unsigned long>(hp));
            return false;
        }
        log::Info("图库：已将 {} 个文件删除到回收站", okCount);
        return true;
    }

    // 回退：SHFileOperation（双 NUL 路径列表）
    std::wstring multi;
    for (const auto& p : paths) {
        multi += p.wstring();
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    SHFILEOPSTRUCTW opw{};
    opw.wFunc = FO_DELETE;
    opw.pFrom = multi.c_str();
    opw.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&opw);
    if (rc != 0 || opw.fAnyOperationsAborted) {
        if (error) {
            *error = "SHFileOperationW rc=" + std::to_string(rc);
        }
        log::Warn("图库：回收站删除失败 rc={}", rc);
        return false;
    }
    log::Info("图库：已将 {} 个文件删除到回收站（SHFileOperation）", paths.size());
    return true;
}

} // namespace shine::gallery::file_actions
