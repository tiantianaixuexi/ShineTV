// 项目中心的**状态与 IO**（零绘制）
//
// ⚠️ I/O 口径：ProjectService::Recent() 每次调用都重读索引文件（Project.h 有意如此），
//    所以卡片只在首次进入 / 一次写操作之后刷新，不在每帧读盘。这是这一块全部
//    「什么时候调 Refresh」的依据 —— 把它挪到绘制侧就会变成每帧读盘。
#include "ui/imgui/pages/Hub.h"

#include "core/Log.h"
#include "project/ProjectIndex.h"
#include "project/ProjectTemplate.h"
#include "util/Encoding.h"
#include "util/Shell.h"
#include "util/Strings.h"

#include <objbase.h>  // 必须在 windows.h（util/Encoding.h 已经带进来）之后
#include <shlobj.h>   // SHBrowseForFolderW：向导第 2 步的「浏览…」

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>

namespace shine::pages {
namespace hub {

using namespace shine::kit;

HubState& S() {
    static HubState state;
    return state;
}

namespace {

// 封面种子跟着项目 id 走：跨帧、跨排序、跨过滤都是同一张封面。
int SeedOf(std::string_view id) {
    std::uint32_t hash = 2166136261u;
    for (const char ch : id) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    return static_cast<int>(hash % 12u); // Art 只有 12 组调色板
}

// project.json 的 templateId → 封面短标签（设计稿：小说 / 影视 / 空白）。
std::string TplLabel(std::string_view templateId) {
    if (templateId == "novel") {
        return "小说";
    }
    if (templateId == "film") {
        return "影视";
    }
    if (templateId == "blank") {
        return "空白";
    }
    return {};
}

// 民用日期 → 天序（Hinnant）。只为算「今天 / 昨天 / N 天前」的真实天数差。
long long DayNumber(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const int yoe = year - static_cast<int>(era * 400);
    const int mp = month + (month > 2 ? -3 : 9);
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// lastOpened 的相对时间：今天 HH:mm / 昨天 HH:mm / N 天前 / 更早给日期。
// 数据来自 RecentEntry::lastOpened（口径照 Qt 版 ProjectHubView.cpp:98）。
std::string RelativeTime(std::chrono::system_clock::time_point tp) {
    const std::time_t when = std::chrono::system_clock::to_time_t(tp);
    const std::tm* lt = std::localtime(&when);
    if (lt == nullptr) {
        return {};
    }
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::tm* ln = std::localtime(&now);
    const long long days =
        (ln != nullptr ? DayNumber(ln->tm_year + 1900, ln->tm_mon + 1, ln->tm_mday) : 0) -
        DayNumber(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    char buf[32] = {};
    if (days <= 0) {
        std::snprintf(buf, sizeof buf, "今天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days == 1) {
        std::snprintf(buf, sizeof buf, "昨天 %02d:%02d", lt->tm_hour, lt->tm_min);
    } else if (days < 7) {
        std::snprintf(buf, sizeof buf, "%lld 天前", days);
    } else {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1,
                      lt->tm_mday);
    }
    return buf;
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

} // namespace

void Refresh(HubState& hub) {
    hub.cards.clear();
    for (const project::RecentEntry& entry : hub.service.Recent()) {
        HubCard card;
        card.entry = entry;
        card.artSeed = SeedOf(entry.id);
        card.when = RelativeTime(entry.lastOpened);
        if (const std::expected<project::ProjectFile, project::Error> file =
                project::LoadProjectFile(entry.rootDir);
            file.has_value()) {
            card.readable = true;
            card.premise = std::string(util::Trim(file->premise));
            card.tplLabel = TplLabel(file->templateId);
            if (const project::ProjectTemplate* tpl = project::FindTemplate(file->templateId);
                tpl != nullptr) {
                card.tplName = tpl->name;
            }
        }
        hub.cards.push_back(std::move(card));
    }
    if (hub.sort == "n") { // 名称
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.name < b.entry.name;
        });
    } else { // 最近打开
        std::stable_sort(hub.cards.begin(), hub.cards.end(), [](const HubCard& a, const HubCard& b) {
            return a.entry.lastOpened > b.entry.lastOpened;
        });
    }
    hub.loaded = true;
}

void Refilter(HubState& hub) {
    const std::string key = LowerAscii(util::Trim(hub.query));
    hub.shown.clear();
    for (std::size_t i = 0; i < hub.cards.size(); ++i) {
        const HubCard& card = hub.cards[i];
        if (key.empty() || LowerAscii(card.entry.name).find(key) != std::string::npos ||
            LowerAscii(card.premise).find(key) != std::string::npos) {
            hub.shown.push_back(static_cast<int>(i));
        }
    }
}

void Open(HubState& hub, const HubCard& card) {
    const std::expected<project::ProjectRef, project::Error> ref =
        hub.service.Open(card.entry.rootDir);
    if (!ref) {
        hub.status = "打开项目失败：" + ref.error().message;
        hub.statusError = true;
        return;
    }
    hub.status = "已打开项目「" + ref->name + "」· " + util::PathToUtf8(ref->rootDir);
    hub.statusError = false;
    Refresh(hub);
}

void Remove(HubState& hub, const std::string& id) {
    project::ProjectIndex index;
    (void)index.Load(); // 坏文件也已回退空索引
    (void)index.Remove(id);
    if (const std::expected<void, project::Error> saved = index.Save(); !saved) {
        hub.status = "移除未落盘：" + saved.error().message;
        hub.statusError = true;
    } else {
        hub.status = "已从最近列表移除（项目文件未删除）";
        hub.statusError = false;
    }
    Refresh(hub);
}

void CycleTheme(HubState& hub) {
    const auto it =
        std::find(theme::kAllThemes.begin(), theme::kAllThemes.end(), theme::CurrentThemeId());
    const std::size_t index =
        (it == theme::kAllThemes.end()) ? 0 : static_cast<std::size_t>(it - theme::kAllThemes.begin());
    const theme::ThemeId next = theme::kAllThemes[(index + 1) % theme::kAllThemes.size()];
    theme::ApplyTheme(next);
    if (theme::ThemeUsesSerif(next)) {
        if (!BuildFontAtlas(/*serif=*/true)) {
            shine::log::Error("serif font atlas rebuild failed — falling back to sans, 文字可能缺字");
        }
        theme::ApplyCurrentTheme();
    }
    (void)theme::PersistTheme(theme::DefaultThemeFile());
    hub.status = "主题已切到「" + std::string(theme::ThemeDisplayName(next)) + "」";
    hub.statusError = false;
}

std::string PickFolder() {
    BROWSEINFOW info = {};
    info.hwndOwner = reinterpret_cast<HWND>(ImGui::GetMainViewport()->PlatformHandle);
    info.ulFlags = BIF_RETURNONLYFSDIRS;
    info.lpszTitle = L"选择项目位置";
    PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&info);
    if (picked == nullptr) {
        return {}; // 用户取消
    }
    wchar_t buffer[MAX_PATH] = {};
    const bool ok = SUCCEEDED(SHGetPathFromIDListW(picked, buffer));
    CoTaskMemFree(picked);
    return ok ? util::PathToUtf8(std::filesystem::path{buffer}) : std::string{};
}

std::filesystem::path TargetDir(const HubState& hub) {
    if (util::Trim(hub.dir).empty() || util::Trim(hub.name).empty()) {
        return {};
    }
    return util::PathFromUtf8(util::Trim(hub.dir)) / util::PathFromUtf8(util::Trim(hub.name));
}

const char* TemplateIcon(std::string_view templateId) {
    if (templateId == "novel") {
        return "book";
    }
    if (templateId == "film") {
        return "film";
    }
    return "folder";
}

std::size_t CharCount(std::string_view text) {
    std::size_t count = 0;
    for (const char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

} // namespace hub
} // namespace shine::pages
