#include "novel/NovelImageGen.h"

#include "comfy/ComfyHttp.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/HttpClient.h"
#include "util/Encoding.h"
#include "video/SceneToImageBuilder.h"

#include <fmt/format.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <yyjson.h>

namespace shine::novelcore {
namespace {

// 1×1 红色 PNG（mock 占位；真图由 openai_images/comfy 写入）
constexpr unsigned char kTinyPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
    0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
    0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08,
    0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE,
    0xD4, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

[[nodiscard]] ImageGenError Err(std::string code, std::string message, int status = 0) {
    return ImageGenError{.http_status = status, .code = std::move(code), .message = std::move(message)};
}

[[nodiscard]] std::string JsonQuote(std::string_view s) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return "\"\"";
    }
    yyjson_mut_val* v = yyjson_mut_strncpy(doc, s.data(), s.size());
    if (!v) {
        yyjson_mut_doc_free(doc);
        return "\"\"";
    }
    size_t len = 0;
    char* text = yyjson_mut_val_write(v, 0, &len);
    yyjson_mut_doc_free(doc);
    if (!text) {
        return "\"\"";
    }
    std::string out{text, len};
    std::free(text);
    return out;
}

[[nodiscard]] std::string SizeString(int w, int h) {
    return fmt::format("{}x{}", w > 0 ? w : 1024, h > 0 ? h : 1024);
}

[[nodiscard]] std::string TrimSlash(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) {
        s.pop_back();
    }
    return s;
}

[[nodiscard]] std::string ResolveImageApiKey() {
    if (!Settings().imageApiKey.empty()) {
        return Settings().imageApiKey;
    }
    if (const char* env = std::getenv("OPENAI_API_KEY"); env != nullptr && *env != '\0') {
        return std::string{env};
    }
    return Settings().openaiApiKey;
}

[[nodiscard]] std::string ResolveImageBaseUrl() {
    if (!Settings().imageBaseUrl.empty()) {
        return TrimSlash(Settings().imageBaseUrl);
    }
    if (!Settings().openaiBaseUrl.empty()) {
        return TrimSlash(Settings().openaiBaseUrl);
    }
    return "https://api.openai.com/v1";
}

[[nodiscard]] std::expected<void, ImageGenError> EnsureParentDir(const std::filesystem::path& p) {
    const auto parent = p.parent_path();
    if (parent.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return std::unexpected(
            Err("io", fmt::format("创建输出目录失败：{}", util::PathToUtf8(parent))));
    }
    return {};
}

[[nodiscard]] std::expected<void, ImageGenError>
WriteFileBinary(const std::filesystem::path& path, std::string_view bytes) {
    if (auto d = EnsureParentDir(path); !d) {
        return d;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(Err("io", fmt::format("无法写入：{}", util::PathToUtf8(path))));
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return std::unexpected(Err("io", fmt::format("写入失败：{}", util::PathToUtf8(path))));
    }
    return {};
}

[[nodiscard]] std::expected<void, ImageGenError>
WriteTinyPng(const std::filesystem::path& path) {
    return WriteFileBinary(
        path, std::string_view{reinterpret_cast<const char*>(kTinyPng), sizeof(kTinyPng)});
}

// 标准 base64（含 data: 前缀剥离）；失败返回空
[[nodiscard]] std::string DecodeBase64(std::string_view in) {
    std::string_view s = in;
    if (const auto pos = s.find("base64,"); pos != std::string_view::npos) {
        s.remove_prefix(pos + 7);
    }
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(s.size() / 4 * 3 + 3);
    int buf = 0;
    int bits = 0;
    for (const char c : s) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            continue;
        }
        const int v = val(c);
        if (v < 0) {
            return {};
        }
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

struct ParsedImages {
    bool fromB64 = false;
    bool fromUrl = false;
    std::string payload; // b64 或 url
    std::string meta;
};

[[nodiscard]] std::expected<ParsedImages, ImageGenError>
ParseImagesResponse(std::string_view body) {
    yyjson_doc* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        return std::unexpected(Err("json", "出图响应不是合法 JSON"));
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    ParsedImages out;
    size_t metaLen = 0;
    if (char* metaText = yyjson_val_write(root, 0, &metaLen); metaText != nullptr) {
        out.meta.assign(metaText, metaLen);
        std::free(metaText);
    } else {
        out.meta = "{}";
    }

    if (yyjson_val* data = yyjson_obj_get(root, "data");
        yyjson_is_arr(data) && yyjson_arr_size(data) > 0) {
        yyjson_val* item = yyjson_arr_get_first(data);
        if (yyjson_val* b = yyjson_obj_get(item, "b64_json"); yyjson_is_str(b)) {
            out.fromB64 = true;
            out.payload = yyjson_get_str(b);
        } else if (yyjson_val* u = yyjson_obj_get(item, "url"); yyjson_is_str(u)) {
            out.fromUrl = true;
            out.payload = yyjson_get_str(u);
        }
    }
    if (!out.fromB64 && !out.fromUrl) {
        if (yyjson_val* images = yyjson_obj_get(root, "images");
            yyjson_is_arr(images) && yyjson_arr_size(images) > 0) {
            yyjson_val* first = yyjson_arr_get_first(images);
            if (yyjson_is_str(first)) {
                out.fromB64 = true;
                out.payload = yyjson_get_str(first);
            }
        }
    }
    yyjson_doc_free(doc);
    if (!out.fromB64 && !out.fromUrl) {
        return std::unexpected(Err("json", "出图响应中未找到 b64_json / images / url 字段"));
    }
    return out;
}

// —— mock ——
class MockBackend final : public ImageBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "mock"; }

    [[nodiscard]] std::expected<ImageGenResult, ImageGenError>
    Generate(const ImageGenRequest& req) override {
        if (req.outputPath.empty()) {
            return std::unexpected(Err("io", "未指定输出路径"));
        }
        if (auto w = WriteTinyPng(req.outputPath); !w) {
            return std::unexpected(w.error());
        }
        const std::string model =
            req.model.empty() ? Settings().imageModel : req.model;
        const std::string meta = fmt::format(
            "{{\"backend\":\"mock\",\"model\":{},\"w\":{},\"h\":{},\"prompt\":{}}}",
            JsonQuote(model), req.width, req.height, JsonQuote(req.prompt));
        ImageGenResult out;
        out.path = req.outputPath;
        out.raw_meta = meta;
        return out;
    }
};

// —— OpenAI Images / 兼容 HTTP ——
class OpenAiImagesBackend final : public ImageBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "openai_images"; }

    [[nodiscard]] std::expected<ImageGenResult, ImageGenError>
    Generate(const ImageGenRequest& req) override {
        const std::string key = ResolveImageApiKey();
        if (key.empty()) {
            return std::unexpected(
                Err("no_key", "未配置出图 API Key（设置 imageApiKey，或 OPENAI_API_KEY）"));
        }
        if (req.prompt.empty()) {
            return std::unexpected(Err("io", "prompt 为空，无法出图"));
        }
        if (req.outputPath.empty()) {
            return std::unexpected(Err("io", "未指定输出路径"));
        }
        const std::string base = ResolveImageBaseUrl();
        const std::string url = base + "/images/generations";
        std::string model = req.model.empty() ? Settings().imageModel : req.model;
        if (model.empty()) {
            model = "dall-e-3";
        }
        const std::string size = SizeString(req.width, req.height);
        std::string prompt = req.prompt;
        if (!req.negative.empty()) {
            prompt += "\nNegative: ";
            prompt += req.negative;
        }

        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            return std::unexpected(Err("json", "构造出图请求失败"));
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_strncpy(doc, root, "model", model.data(), model.size());
        yyjson_mut_obj_add_strncpy(doc, root, "prompt", prompt.data(), prompt.size());
        yyjson_mut_obj_add_int(doc, root, "n", 1);
        yyjson_mut_obj_add_strncpy(doc, root, "size", size.data(), size.size());
        yyjson_mut_obj_add_strcpy(doc, root, "response_format", "b64_json");
        size_t len = 0;
        char* bodyText = yyjson_mut_val_write(root, 0, &len);
        yyjson_mut_doc_free(doc);
        if (!bodyText) {
            return std::unexpected(Err("json", "序列化出图请求失败"));
        }
        const std::string body(bodyText, len);
        std::free(bodyText);

        net::Request r;
        r.method = "POST";
        r.url = url;
        r.body = body;
        r.timeout = std::chrono::seconds{180};
        r.headers.emplace("Content-Type", "application/json");
        r.headers.emplace("Authorization", "Bearer " + key);

        const auto resp = net::Send(r);
        if (!resp.ok) {
            const std::string code = resp.status >= 400
                                         ? "http_" + std::to_string(resp.status)
                                         : "network";
            const std::string msg =
                resp.error.empty() ? "出图请求失败" : resp.error;
            return std::unexpected(Err(code, msg, resp.status));
        }
        if (resp.status >= 400) {
            std::string msg = "出图 HTTP " + std::to_string(resp.status);
            yyjson_doc* errDoc = yyjson_read(resp.body.data(), resp.body.size(), 0);
            if (errDoc != nullptr) {
                if (yyjson_val* e = yyjson_obj_get(yyjson_doc_get_root(errDoc), "error");
                    yyjson_is_obj(e)) {
                    if (yyjson_val* m = yyjson_obj_get(e, "message"); yyjson_is_str(m)) {
                        msg = yyjson_get_str(m);
                    }
                }
                yyjson_doc_free(errDoc);
            }
            return std::unexpected(Err("http_" + std::to_string(resp.status), msg, resp.status));
        }

        auto parsed = ParseImagesResponse(resp.body);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        ImageGenResult out;
        out.raw_meta = parsed->meta;
        out.path = req.outputPath;
        if (parsed->fromB64) {
            const std::string bytes = DecodeBase64(parsed->payload);
            if (bytes.empty()) {
                return std::unexpected(Err("json", "base64 解码失败"));
            }
            if (auto w = WriteFileBinary(req.outputPath, bytes); !w) {
                return std::unexpected(w.error());
            }
            return out;
        }
        const auto dl = net::Download(parsed->payload, std::chrono::seconds{120});
        if (!dl) {
            return std::unexpected(Err("network", dl.error().message, dl.error().status));
        }
        if (auto w = WriteFileBinary(req.outputPath, *dl); !w) {
            return std::unexpected(w.error());
        }
        return out;
    }
};

// —— Comfy 后端的几个纯工具（不发网络）——
[[nodiscard]] std::string TruncForMsg(std::string_view s, std::size_t n) {
    return s.size() <= n ? std::string{s} : std::string{s.substr(0, n)} + "…";
}

// `{"prompt_id":"abc"}` → "abc"（取不到 → 空）
[[nodiscard]] std::string JsonTopStr(std::string_view json, const char* key) {
    yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
    if (d == nullptr) {
        return {};
    }
    std::string out;
    yyjson_val* root = yyjson_doc_get_root(d);
    if (yyjson_is_obj(root)) {
        const yyjson_val* v = yyjson_obj_get(root, key);
        if (yyjson_is_str(v)) {
            out = yyjson_get_str(v);
        }
    }
    yyjson_doc_free(d);
    return out;
}

// `/history/{id}` 里取**第一张**输出图：
// `{"<prompt_id>":{"outputs":{"<node>":{"images":[{"filename","subfolder","type"}]}}}}`
[[nodiscard]] bool ParseFirstHistoryImage(std::string_view json, std::string* filename,
                                          std::string* subfolder, std::string* type) {
    yyjson_doc* d = yyjson_read(json.data(), json.size(), 0);
    if (d == nullptr) {
        return false;
    }
    bool found = false;
    yyjson_val* root = yyjson_doc_get_root(d);
    if (yyjson_is_obj(root)) {
        std::size_t i = 0;
        std::size_t max = 0;
        yyjson_val* key = nullptr;
        yyjson_val* entry = nullptr;
        yyjson_obj_foreach(root, i, max, key, entry) {
            const yyjson_val* outputs = yyjson_obj_get(entry, "outputs");
            if (!yyjson_is_obj(outputs)) {
                continue;
            }
            std::size_t j = 0;
            std::size_t jmax = 0;
            yyjson_val* nkey = nullptr;
            yyjson_val* node = nullptr;
            yyjson_obj_foreach(outputs, j, jmax, nkey, node) {
                const yyjson_val* images = yyjson_obj_get(node, "images");
                if (!yyjson_is_arr(images) || yyjson_arr_size(images) == 0) {
                    continue;
                }
                const yyjson_val* img = yyjson_arr_get(images, 0);
                if (!yyjson_is_obj(img)) {
                    continue;
                }
                const yyjson_val* fn = yyjson_obj_get(img, "filename");
                if (!yyjson_is_str(fn)) {
                    continue;
                }
                *filename = yyjson_get_str(fn);
                const yyjson_val* sub = yyjson_obj_get(img, "subfolder");
                *subfolder = yyjson_is_str(sub) ? yyjson_get_str(sub) : "";
                const yyjson_val* tp = yyjson_obj_get(img, "type");
                *type = yyjson_is_str(tp) ? yyjson_get_str(tp) : "output";
                found = true;
                break;
            }
            if (found) {
                break;
            }
        }
    }
    yyjson_doc_free(d);
    return found;
}

// URL 查询参数编码（Comfy 的文件名可能含中文 / 空格）
[[nodiscard]] std::string UrlEncodeQuery(std::string_view s) {
    static constexpr const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// 确定性种子（`11` §2.5 的精神：**禁纯随机**）—— 同 prompt/尺寸/steps 恒同种子，便于复现与对齐
[[nodiscard]] std::int64_t StableSeedOf(const ImageGenRequest& req) {
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a 64
    const auto mix = [&h](std::string_view s) {
        for (const unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
    };
    mix(req.prompt);
    mix(req.negative);
    mix(fmt::format("{}x{}x{}", req.width, req.height, req.steps));
    return static_cast<std::int64_t>(h & 0x7FFFFFFFFFFFFFFFULL);
}

// —— Comfy（`13` §1.2 的 G13：**真实实现**，此前是"P9.2 接入"的 stub）——
// 同步短事务（`Generate` 本就在 worker 线程阻塞跑，`comfy::ComfyHttp` 的同步 HTTP 正合）：
//   ① 编 workflow —— `video::BuildSceneToImageWorkflow`（SD1.5 线的**唯一来源**，本层不重写规则）
//   ② `POST /prompt` 提交 → `prompt_id`
//   ③ 轮询 `GET /history/{id}` 直到出现输出（间隔 500ms、总超时 300s —— **有界**，不无限等）
//   ④ `GET /view?...` 下载首图 → 写 `req.outputPath`
// ⚠️ **不建常驻会话**（那是 GUI 用的 `ComfySession`）：本后端一次调用一提交，于是 **headless（CLI）
//    也能用**，不依赖 GUI 事件循环。
// ⚠️ 首版**不上传参考图**（走纯文生图；`BuildSceneToImageWorkflow` 会记"无颜色图"降级）——
//    上传（`CollectSceneUploads` + `/upload/image`）留作下一步，账里如实说明。
class ComfyBackend final : public ImageBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "comfy"; }

    [[nodiscard]] std::expected<ImageGenResult, ImageGenError>
    Generate(const ImageGenRequest& req) override {
        if (req.outputPath.empty()) {
            return std::unexpected(Err("io", "未指定输出路径"));
        }
        std::string base{Settings().comfyBaseUrl};
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        if (base.empty()) {
            return std::unexpected(
                Err("unsupported", "未配置 ComfyUI 服务地址（设置 → Comfy → 服务地址）"));
        }
        // ① workflow（缺 checkpoint → 中文错误；宽高不对齐 → 内部纠正 + 记降级）
        video::VideoProject proj = video::VideoProject::MakeDefault();
        if (!req.negative.empty()) {
            proj.sceneNegativePrompt = req.negative;
        }
        video::Shot shot;
        shot.prompt = req.prompt;
        shot.width = req.width;
        shot.height = req.height;
        shot.steps = req.steps > 0 ? req.steps : proj.sceneSteps;
        shot.seed = StableSeedOf(req);
        video::SceneToImageOptions opt;
        opt.shot = shot;
        opt.project = proj;
        const video::SceneToImageResult wf = video::BuildSceneToImageWorkflow(opt);
        if (!wf.ok) {
            return std::unexpected(Err("unsupported", wf.error));
        }
        for (const video::H3BuildWarning& w : wf.warnings) {
            log::Warn("Comfy 出图降级：{}", w.text);
        }
        // ② 提交（`/prompt` 的 body = {"prompt": <graph>}）
        const auto post = comfy::HttpPostJson(base + "/prompt",
                                              fmt::format("{{\"prompt\":{}}}", wf.apiJson),
                                              std::chrono::seconds{60});
        if (!post.ok) {
            return std::unexpected(Err(
                "network",
                fmt::format("提交 Comfy 任务失败（HTTP {}）：{}", post.status,
                            post.error.empty() ? TruncForMsg(post.body, 200) : post.error),
                post.status));
        }
        const std::string promptId = JsonTopStr(post.body, "prompt_id");
        if (promptId.empty()) {
            return std::unexpected(Err(
                "json", fmt::format("Comfy 未返回 prompt_id：{}", TruncForMsg(post.body, 200))));
        }
        // ③ 轮询（有界）
        std::string filename;
        std::string subfolder;
        std::string type = "output";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{300};
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
            const auto hist = comfy::HttpGet(base + "/history/" + promptId);
            if (!hist.ok) {
                continue; // 轮询中的瞬时错误不立即判死，下一轮再试
            }
            if (ParseFirstHistoryImage(hist.body, &filename, &subfolder, &type)) {
                break;
            }
        }
        if (filename.empty()) {
            return std::unexpected(Err(
                "network", fmt::format("Comfy 任务 {} 在 300s 内未产出图片（已提交，可在 Comfy 队列里查看）",
                                       promptId)));
        }
        // ④ 下载
        const std::string url =
            fmt::format("{}/view?filename={}&subfolder={}&type={}", base, UrlEncodeQuery(filename),
                        UrlEncodeQuery(subfolder), UrlEncodeQuery(type));
        auto bytes = comfy::HttpDownloadBinary(url, std::chrono::seconds{120});
        if (!bytes) {
            return std::unexpected(Err("network",
                                       fmt::format("下载 Comfy 图片失败：{}", bytes.error().message),
                                       bytes.error().status));
        }
        if (auto w = WriteFileBinary(req.outputPath, *bytes); !w) {
            return std::unexpected(w.error());
        }
        ImageGenResult out;
        out.path = req.outputPath;
        out.raw_meta = fmt::format(
            "{{\"backend\":\"comfy\",\"prompt_id\":{},\"file\":{},\"w\":{},\"h\":{},\"seed\":{}}}",
            JsonQuote(promptId), JsonQuote(filename), wf.width, wf.height, shot.seed);
        return out;
    }
};

} // namespace

std::unique_ptr<ImageBackend> MakeImageBackend() {
    const std::string& kind = Settings().imageBackend;
    if (kind == "openai_images" || kind == "openai") {
        return std::make_unique<OpenAiImagesBackend>();
    }
    if (kind == "comfy") {
        return std::make_unique<ComfyBackend>();
    }
    return std::make_unique<MockBackend>();
}

std::filesystem::path ImageRelDir() {
    const auto& rel = Settings().imageOutputRelDir;
    if (rel.empty()) {
        return std::filesystem::path{"visual"} / "gen";
    }
    return std::filesystem::path{rel};
}

std::filesystem::path ProjectImageDir(const std::filesystem::path& projectDir) {
    return projectDir / ImageRelDir();
}

bool RunImageGenSelfCheck() {
    const std::filesystem::path outDir =
        std::filesystem::temp_directory_path() / "shine_p9_imagegen_check";
    std::error_code ec;
    std::filesystem::remove_all(outDir, ec);
    std::filesystem::create_directories(outDir, ec);

    // 1) mock 落盘
    {
        const AppSettings saved = Settings();
        Settings().imageBackend = "mock";
        auto backend = MakeImageBackend();
        if (!backend || backend->name() != "mock") {
            log::Error("P9.1 自检：工厂未返回 mock");
            Settings() = saved;
            return false;
        }
        ImageGenRequest req;
        req.prompt = "young man, black hair, dark forest";
        req.width = 64;
        req.height = 64;
        req.outputPath = outDir / "mock.png";
        auto r = backend->Generate(req);
        Settings() = saved;
        if (!r) {
            log::Error("P9.1 自检：mock 生成失败 {}", r.error().message);
            return false;
        }
        if (!std::filesystem::exists(r->path) || std::filesystem::file_size(r->path) < 20) {
            log::Error("P9.1 自检：mock 未写出有效文件");
            return false;
        }
        if (r->raw_meta.find("mock") == std::string::npos) {
            log::Error("P9.1 自检：mock meta 缺 backend 字段");
            return false;
        }
    }

    // 2) openai_images + 无 key → no_key（不发网）
    {
        const bool envHasKey = [] {
            const char* e = std::getenv("OPENAI_API_KEY");
            return e != nullptr && *e != '\0';
        }();
        const AppSettings saved = Settings();
        Settings().imageBackend = "openai_images";
        Settings().imageApiKey.clear();
        Settings().openaiApiKey.clear();
        auto backend = MakeImageBackend();
        if (!backend || backend->name() != "openai_images") {
            log::Error("P9.1 自检：工厂未返回 openai_images");
            Settings() = saved;
            return false;
        }
        ImageGenRequest req;
        req.prompt = "test";
        req.outputPath = outDir / "should_not_exist.png";
        auto r = backend->Generate(req);
        Settings() = saved;
        if (!envHasKey) {
            if (r || r.error().code != "no_key") {
                log::Error("P9.1 自检：无 key 时应返回 no_key，实际 {}",
                           r ? "成功(错误)" : r.error().code);
                return false;
            }
        }
        if (std::filesystem::exists(req.outputPath)) {
            log::Error("P9.1 自检：no_key 路径不应写出文件");
            return false;
        }
    }

    // 3) comfy 占位
    {
        const AppSettings saved = Settings();
        Settings().imageBackend = "comfy";
        auto backend = MakeImageBackend();
        Settings() = saved;
        if (!backend || backend->name() != "comfy") {
            log::Error("P9.1 自检：工厂未返回 comfy");
            return false;
        }
        auto r = backend->Generate({.prompt = "x", .outputPath = outDir / "c.png"});
        if (r || r.error().code != "unsupported") {
            log::Error("P9.1 自检：comfy 首期应 unsupported");
            return false;
        }
    }

    // 4) 默认相对目录
    {
        const AppSettings saved = Settings();
        Settings().imageOutputRelDir.clear();
        const auto rel = ImageRelDir();
        Settings() = saved;
        if (util::PathToUtf8(rel) != "visual/gen" &&
            util::PathToUtf8(rel) != "visual\\gen") {
            log::Error("P9.1 自检：默认输出目录应为 visual/gen，实际 {}",
                       util::PathToUtf8(rel));
            return false;
        }
    }

    log::Info("P9.1 ImageGen 自检通过（mock 落盘 / no_key / comfy 占位）");
    return true;
}

} // namespace shine::novelcore
