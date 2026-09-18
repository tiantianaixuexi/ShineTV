#include "comfy/ComfyWorkflows.h"

#include "comfy/ComfyHttp.h"
#include "comfy/ComfyTypes.h"
#include "core/Async.h"
#include "core/Log.h"
#include "util/Json.h"
#include "util/Strings.h"

#include <yyjson.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace shine::comfy {
namespace {

// URL 路径转义：RFC 3986 unreserved 之外全部转义，**`/` 必须变成 `%2F`**。
// 依据（`ComfyUI/app/user_manager.py:88-91`）：服务端拿到含 `%` 的 `file` 会 `unquote` 一次；
// 而 aiohttp 的 `{file}` 通配**不跨 `/`** → 目录分隔符不转义就会 404（前端也是转义后发的）。
[[nodiscard]] std::string PercentEncode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// 中文可操作错误：`resp.error` 已是中文（网络层）/`HTTP <code>`，别再拼英文原文
[[nodiscard]] std::string HttpFailText(const HttpResponse& resp, std::string_view url) {
    return fmt::format("{} → {}", url, resp.error.empty() ? std::string_view{"请求失败"} : std::string_view{resp.error});
}

// 取路径最后一段并去掉 `.json`（用户工作流的显示名）
[[nodiscard]] std::string BaseNameNoExt(std::string_view rel) {
    const std::size_t slash = rel.rfind('/');
    std::string_view base = (slash == std::string_view::npos) ? rel : rel.substr(slash + 1);
    if (util::EndsWithNoCase(base, ".json")) {
        base = base.substr(0, base.size() - 5);
    }
    return std::string{base};
}

// `yyjson_get_str` 的 `const char* + len` → std::string（JSON 文本里可能有非 ASCII/内嵌 NUL）
[[nodiscard]] std::string StrOf(yyjson_val* v) {
    return std::string{yyjson_get_str(v), yyjson_get_len(v)};
}

[[nodiscard]] bool NameTaken(const std::vector<RemoteWorkflow>& items, std::string_view name) {
    return std::ranges::any_of(items, [name](const RemoteWorkflow& w) { return w.name == name; });
}

} // namespace

const char* WorkflowSourceLabel(WorkflowSource s) noexcept {
    switch (s) {
    case WorkflowSource::Template: return "内置模板";
    case WorkflowSource::CustomNode: return "节点包示例";
    case WorkflowSource::User: return "我保存的";
    }
    return "未知来源";
}

std::string BuildUserWorkflowFetchPath(std::string_view relativeUtf8) {
    return fmt::format("/userdata/{}", PercentEncode(fmt::format("workflows/{}", relativeUtf8)));
}

bool ParseTemplateIndex(std::string_view json, std::vector<RemoteWorkflow>& out, std::string& error) {
    out.clear();
    error.clear();
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        error = "模板目录不是合法 JSON";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        error = "模板目录的结构不认识（顶层应是分组数组）";
        yyjson_doc_free(doc);
        return false;
    }
    const std::size_t groups = yyjson_arr_size(root);
    for (std::size_t g = 0; g < groups; ++g) {
        yyjson_val* group = yyjson_arr_get(root, g);
        if (!yyjson_is_obj(group)) {
            continue;
        }
        // 分组名优先用 `title`（如 "Video"），没有就退 `category`（如 "GENERATION TYPE"）
        std::string groupName = util::json::GetStrCopy(group, "title");
        if (groupName.empty()) {
            groupName = util::json::GetStrCopy(group, "category");
        }
        yyjson_val* templates = util::json::GetArr(group, "templates");
        if (templates == nullptr) {
            continue;
        }
        const std::size_t count = yyjson_arr_size(templates);
        for (std::size_t i = 0; i < count; ++i) {
            yyjson_val* entry = yyjson_arr_get(templates, i);
            if (!yyjson_is_obj(entry)) {
                continue;
            }
            const std::string name = util::json::GetStrCopy(entry, "name");
            if (name.empty() || NameTaken(out, name)) { // 目录里可能有重名，先到先得
                continue;
            }
            RemoteWorkflow w;
            w.source = WorkflowSource::Template;
            w.name = name;
            w.title = util::json::GetStrCopy(entry, "title");
            if (w.title.empty()) {
                w.title = name;
            }
            w.category = groupName;
            // 文件名 = `name` + `.json`（本机实测 `/templates/video_minimax_h3_r2v.json` → 200）
            w.fetchPath = fmt::format("/templates/{}.json", PercentEncode(name));
            w.description = util::json::GetStrCopy(entry, "description");
            out.push_back(std::move(w));
        }
    }
    yyjson_doc_free(doc);
    return true;
}

bool ParseCustomNodeTemplateList(std::string_view json, std::vector<RemoteWorkflow>& out, std::string& error) {
    out.clear();
    error.clear();
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        error = "节点包示例清单不是合法 JSON";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        error = "节点包示例清单的结构不认识（顶层应是「模块名 → 模板名数组」）";
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    // ⚠️ `yyjson_obj_iter_next()` 返回的是 **key**，值要 `yyjson_obj_iter_get_val(key)`（见 MEMORY §6）
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
        const std::string module = StrOf(key);
        yyjson_val* names = yyjson_obj_iter_get_val(key);
        if (!yyjson_is_arr(names)) {
            continue;
        }
        const std::size_t count = yyjson_arr_size(names);
        for (std::size_t i = 0; i < count; ++i) {
            yyjson_val* v = yyjson_arr_get(names, i);
            if (!yyjson_is_str(v)) {
                continue;
            }
            const std::string name = StrOf(v);
            RemoteWorkflow w;
            w.source = WorkflowSource::CustomNode;
            w.name = fmt::format("{}/{}", module, name);
            w.title = name;
            w.category = module;
            // `custom_node_manager.py:132-138` 把示例目录挂在 `/api/workflow_templates/<模块>` 上
            w.fetchPath = fmt::format("/api/workflow_templates/{}/{}", PercentEncode(module),
                                      PercentEncode(fmt::format("{}.json", name)));
            out.push_back(std::move(w));
        }
    }
    yyjson_doc_free(doc);
    // 稳定顺序：模块名 → 模板名（对象键顺序不可控，见 MEMORY §6）
    std::ranges::sort(out, [](const RemoteWorkflow& a, const RemoteWorkflow& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.title < b.title;
    });
    return true;
}

bool ParseUserWorkflowList(std::string_view json, std::vector<RemoteWorkflow>& out, std::string& error) {
    out.clear();
    error.clear();
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (doc == nullptr) {
        error = "工作流清单不是合法 JSON";
        return false;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        error = "工作流清单的结构不认识（顶层应是相对路径数组）";
        yyjson_doc_free(doc);
        return false;
    }
    const std::size_t count = yyjson_arr_size(root);
    for (std::size_t i = 0; i < count; ++i) {
        yyjson_val* v = yyjson_arr_get(root, i);
        if (!yyjson_is_str(v)) {
            continue;
        }
        const std::string rel = StrOf(v); // 相对 `user/…/workflows` 的路径，如 "sub/a b.json"
        if (rel.empty()) {
            continue;
        }
        RemoteWorkflow w;
        w.source = WorkflowSource::User;
        w.name = fmt::format("workflows/{}", rel);
        w.title = BaseNameNoExt(rel);
        w.category = "我保存的";
        w.fetchPath = BuildUserWorkflowFetchPath(rel);
        out.push_back(std::move(w));
    }
    yyjson_doc_free(doc);
    std::ranges::sort(out, [](const RemoteWorkflow& a, const RemoteWorkflow& b) { return a.title < b.title; });
    return true;
}

void FetchWorkflowAsync(std::string_view baseUrl, const RemoteWorkflow& ref, RemoteWorkflowTextCb cb) {
    const std::string url = BuildApiUrl(baseUrl, ref.fetchPath);
    async::RunOnWorker([url, cb = std::move(cb)]() mutable {
        RemoteWorkflowText out;
        if (url.empty()) {
            out.error = "ComfyUI 地址为空：先在侧栏「Comfy」里填地址并连接";
        } else {
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{30});
            out.ok = resp.ok && !resp.body.empty();
            if (!out.ok) {
                out.error = resp.body.empty() ? HttpFailText(resp, url)
                                              : fmt::format("{}：{}", HttpFailText(resp, url), resp.body.substr(0, 200));
            }
            out.text = resp.ok ? resp.body : std::string{};
        }
        async::PostToUi([out = std::move(out), cb = std::move(cb)]() mutable { cb(std::move(out)); });
    });
}

void FetchWorkflowListAsync(std::string_view baseUrl, RemoteWorkflowListCb cb) {
    const std::string base{baseUrl};
    async::RunOnWorker([base, cb = std::move(cb)]() mutable {
        RemoteWorkflowList result;
        std::size_t templateCount = 0;
        std::size_t customCount = 0;
        std::size_t userCount = 0;
        std::size_t sourcesOk = 0;
        std::string firstError;

        const auto noteFail = [&](std::string_view url, const HttpResponse& resp) {
            const std::string text = HttpFailText(resp, url);
            if (firstError.empty()) {
                firstError = text;
            }
            return text;
        };

        // ① 内置模板（官方 H3 工作流在这里；目录 571 KB，给 30s）
        {
            const std::string url = BuildApiUrl(base, "/templates/index.json");
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{30});
            std::vector<RemoteWorkflow> items;
            std::string err;
            if (resp.ok && ParseTemplateIndex(resp.body, items, err)) {
                templateCount = items.size();
                ++sourcesOk;
                result.items.insert(result.items.end(), std::make_move_iterator(items.begin()),
                                    std::make_move_iterator(items.end()));
            } else {
                result.warnings.push_back(
                    resp.ok ? fmt::format("内置模板目录解析失败：{}", err)
                            : fmt::format("内置模板取不到（当前 ComfyUI 可能没装 workflow_templates 包）：{}",
                                          noteFail(url, resp)));
            }
        }

        // ② custom_nodes 示例工作流
        {
            const std::string url = BuildApiUrl(base, "/workflow_templates");
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{15});
            std::vector<RemoteWorkflow> items;
            std::string err;
            if (resp.ok && ParseCustomNodeTemplateList(resp.body, items, err)) {
                customCount = items.size();
                ++sourcesOk;
                result.items.insert(result.items.end(), std::make_move_iterator(items.begin()),
                                    std::make_move_iterator(items.end()));
            } else {
                result.warnings.push_back(resp.ok ? fmt::format("节点包示例清单解析失败：{}", err)
                                                  : fmt::format("节点包示例清单取不到：{}", noteFail(url, resp)));
            }
        }

        // ③ 用户保存的工作流
        {
            const std::string url = BuildApiUrl(base, "/userdata?dir=workflows&recurse=true");
            const HttpResponse resp = HttpGet(url, std::chrono::seconds{15});
            if (resp.status == 404) {
                // 从没保存过工作流时目录不存在 → **正常**，不算错误（本机实测现状）
                ++sourcesOk;
            } else if (resp.ok) {
                std::vector<RemoteWorkflow> items;
                std::string err;
                if (ParseUserWorkflowList(resp.body, items, err)) {
                    userCount = items.size();
                    ++sourcesOk;
                    result.items.insert(result.items.end(), std::make_move_iterator(items.begin()),
                                        std::make_move_iterator(items.end()));
                } else {
                    result.warnings.push_back(fmt::format("我保存的工作流清单解析失败：{}", err));
                }
            } else {
                result.warnings.push_back(fmt::format("我保存的工作流清单取不到：{}", noteFail(url, resp)));
            }
        }

        result.ok = sourcesOk > 0;
        if (!result.ok) {
            result.error = firstError.empty() ? std::string{"拉取工作流清单失败"} : firstError;
        } else {
            log::Info("工作流清单：内置模板 {} 个 / 节点包示例 {} 个 / 我保存的 {} 个（共 {} 项）", templateCount,
                      customCount, userCount, result.items.size());
        }
        for (const std::string& w : result.warnings) {
            log::Warn("工作流清单：{}", w);
        }

        async::PostToUi([result = std::move(result), cb = std::move(cb)]() mutable { cb(std::move(result)); });
    });
}

} // namespace shine::comfy
