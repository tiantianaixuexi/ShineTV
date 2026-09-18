#include "paint/PaintService.h"

#include "comfy/ComfyClient.h"
#include "comfy/ComfyHttp.h"
#include "comfy/ComfySession.h"
#include "comfy/ComfyTypes.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "paint/PngCodec.h"
#include "util/Encoding.h"
#include "util/File.h"
#include "util/Strings.h"

#include <fmt/format.h>
#include <yyjson.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace shine::paint {
namespace {

InpaintRunState g_state;
std::atomic<bool> g_busy{false};
std::atomic<bool> g_cancel{false};

[[nodiscard]] yyjson_mut_val* JStr(yyjson_mut_doc* doc, std::string_view t) {
    return yyjson_mut_strncpy(doc, t.data(), t.size());
}
[[nodiscard]] yyjson_mut_val* JSint(yyjson_mut_doc* doc, std::int64_t v) { return yyjson_mut_sint(doc, v); }
[[nodiscard]] yyjson_mut_val* JReal(yyjson_mut_doc* doc, double v) { return yyjson_mut_real(doc, v); }
[[nodiscard]] yyjson_mut_val* JLink(yyjson_mut_doc* doc, std::string_view id, int slot) {
    yyjson_mut_val* a = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_val(a, JStr(doc, id));
    yyjson_mut_arr_add_val(a, JSint(doc, slot));
    return a;
}

void SetPhase(InpaintPhase p, std::string detail) {
    g_state.phase = p;
    g_state.detail = std::move(detail);
    if (p != InpaintPhase::Failed) {
        g_state.error.clear();
    }
}

void FailUi(std::string error) {
    g_state.phase = InpaintPhase::Failed;
    g_state.error = std::move(error);
    g_state.detail = g_state.error;
    g_busy = false;
    log::Error("inpaint 失败：{}", g_state.error);
}

[[nodiscard]] std::filesystem::path PaintOutputDir() {
    if (!Settings().paintOutputDir.empty()) {
        return util::PathFromUtf8(Settings().paintOutputDir);
    }
    return util::PathFromUtf8(SettingsPath()).parent_path() / L"paint";
}

void InvertMask(std::vector<std::uint8_t>& mask) {
    for (std::uint8_t& v : mask) {
        v = v >= 128 ? 0 : 255;
    }
}

} // namespace

const char* InpaintPhaseLabel(InpaintPhase p) noexcept {
    switch (p) {
    case InpaintPhase::Idle: return "空闲";
    case InpaintPhase::Encoding: return "编码中";
    case InpaintPhase::Uploading: return "上传中";
    case InpaintPhase::Submitting: return "提交中";
    case InpaintPhase::Running: return "生成中";
    case InpaintPhase::Downloading: return "下载结果";
    case InpaintPhase::Done: return "完成";
    case InpaintPhase::Failed: return "失败";
    }
    return "";
}

InpaintGraphResult BuildInpaintGraph(const InpaintParams& params) {
    InpaintGraphResult out;
    if (params.checkpoint.empty()) {
        out.error = "缺少 Checkpoint（请在画布侧栏/设置填写 SD 模型名）";
        return out;
    }
    if (params.prompt.empty()) {
        out.error = "提示词为空";
        return out;
    }
    const std::string imageName =
        params.uploadedImageName.empty() ? std::string("canvas_input.png") : params.uploadedImageName;
    const std::string maskName =
        params.uploadedMaskName.empty() ? std::string("canvas_mask.png") : params.uploadedMaskName;

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    std::size_t next = 0;
    const auto nextId = [&next] { return util::FromInt(++next); };
    const auto add = [&](std::string& outId, std::string_view classType,
                         std::vector<std::pair<std::string, yyjson_mut_val*>> inputs) {
        outId = nextId();
        yyjson_mut_val* node = yyjson_mut_obj(doc);
        yyjson_mut_val* in = yyjson_mut_obj(doc);
        for (auto& kv : inputs) {
            yyjson_mut_obj_add(in, JStr(doc, kv.first), kv.second);
        }
        yyjson_mut_obj_add(node, JStr(doc, "class_type"), JStr(doc, classType));
        yyjson_mut_obj_add(node, JStr(doc, "inputs"), in);
        yyjson_mut_obj_add(root, JStr(doc, outId), node);
        ++out.nodeCount;
    };

    std::string ckpt, loadImg, loadMask, vaeEnc, clipPos, clipNeg, ks, dec, save;
    add(ckpt, "CheckpointLoaderSimple", {{"ckpt_name", JStr(doc, params.checkpoint)}});
    add(loadImg, "LoadImage", {{"image", JStr(doc, imageName)}});
    add(loadMask, "LoadImageMask",
        {{"image", JStr(doc, maskName)}, {"channel", JStr(doc, "red")}, {"upload", JStr(doc, "image")}});
    add(vaeEnc, "VAEEncodeForInpaint",
        {{"pixels", JLink(doc, loadImg, 0)},
         {"vae", JLink(doc, ckpt, 2)},
         {"mask", JLink(doc, loadMask, 0)},
         {"grow_mask_by", JReal(doc, params.growMaskBy)}});
    add(clipPos, "CLIPTextEncode", {{"text", JStr(doc, params.prompt)}, {"clip", JLink(doc, ckpt, 1)}});
    add(clipNeg, "CLIPTextEncode", {{"text", JStr(doc, params.negative)}, {"clip", JLink(doc, ckpt, 1)}});
    const std::int64_t seed = params.seed >= 0 ? params.seed : 42;
    add(ks, "KSampler",
        {{"model", JLink(doc, ckpt, 0)},
         {"positive", JLink(doc, clipPos, 0)},
         {"negative", JLink(doc, clipNeg, 0)},
         {"latent_image", JLink(doc, vaeEnc, 0)},
         {"seed", JSint(doc, seed)},
         {"steps", JSint(doc, params.steps)},
         {"cfg", JReal(doc, params.cfg)},
         {"sampler_name", JStr(doc, "dpmpp_2m")},
         {"scheduler", JStr(doc, "karras")},
         {"denoise", JReal(doc, params.denoise)}});
    add(dec, "VAEDecode", {{"samples", JLink(doc, ks, 0)}, {"vae", JLink(doc, ckpt, 2)}});
    add(save, "SaveImage",
        {{"filename_prefix", JStr(doc, params.outputPrefix)}, {"images", JLink(doc, dec, 0)}});

    const char* json = yyjson_mut_write(doc, 0, nullptr);
    if (json == nullptr) {
        yyjson_mut_doc_free(doc);
        out.error = "API JSON 序列化失败";
        return out;
    }
    out.apiJson = json;
    std::free(const_cast<char*>(json));
    yyjson_mut_doc_free(doc);
    out.ok = true;
    return out;
}

bool InpaintBusy() { return g_busy.load(); }
const InpaintRunState& InpaintState() { return g_state; }

void CancelInpaint() {
    if (!g_busy.load()) {
        return;
    }
    g_cancel = true;
    comfy::ComfySession::Instance().Interrupt([](comfy::OperationResult) {});
    SetPhase(InpaintPhase::Idle, "已请求中断");
}

bool StartInpaint(const PaintCanvas& canvas, const InpaintParams& params,
                  std::function<void(const InpaintRunState&)> onFinish) {
    if (g_busy.exchange(true)) {
        return false;
    }
    if (!canvas.valid()) {
        g_busy = false;
        FailUi("画布无效");
        return false;
    }
    if (params.checkpoint.empty()) {
        g_busy = false;
        FailUi("缺少 Checkpoint：本机可能没有 SD/SDXL 模型，请先在设置中填写");
        return false;
    }
    g_cancel = false;
    g_state = InpaintRunState{};
    SetPhase(InpaintPhase::Encoding, "编码底图与遮罩…");

    const std::uint32_t w = canvas.Width();
    const std::uint32_t h = canvas.Height();
    std::vector<std::byte> base(canvas.BasePixels().begin(), canvas.BasePixels().end());
    std::vector<std::uint8_t> mask(canvas.MaskPixels().begin(), canvas.MaskPixels().end());
    if (params.maskMode == MaskMode::Protect) {
        InvertMask(mask); // 白 = 可重绘（Comfy 语义）
    }
    InpaintParams p = params;
    const std::string baseUrl = comfy::ComfySession::Instance().BaseUrl();

    async::RunOnWorker([w, h, base = std::move(base), mask = std::move(mask), p, baseUrl,
                        onFinish = std::move(onFinish)]() mutable {
        auto report = [&](InpaintPhase phase, std::string detail) {
            async::PostToUi([phase, detail = std::move(detail), onFinish] {
                SetPhase(phase, detail);
                if (onFinish) {
                    onFinish(g_state);
                }
            });
        };
        if (g_cancel) {
            async::PostToUi([onFinish] {
                g_busy = false;
                SetPhase(InpaintPhase::Idle, "已取消");
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        auto pngBase = EncodePng(w, h, base);
        auto pngMask = EncodeMaskPng(w, h, mask);
        if (!pngBase || !pngMask) {
            async::PostToUi([onFinish] {
                FailUi("PNG 编码失败");
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        const auto stamp = util::FromInt(static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()));
        p.uploadedImageName = "paint_in_" + stamp + ".png";
        p.uploadedMaskName = "paint_mask_" + stamp + ".png";
        report(InpaintPhase::Uploading, "上传底图与遮罩…");

        const std::string uploadUrl = baseUrl + "/upload/image";
        const std::string baseBytes(reinterpret_cast<const char*>(pngBase->data()), pngBase->size());
        const std::string maskBytes(reinterpret_cast<const char*>(pngMask->data()), pngMask->size());
        auto up1 = comfy::HttpUploadImage(uploadUrl, p.uploadedImageName, baseBytes,
                                          {{"type", "input"}, {"overwrite", "true"}});
        auto up2 = comfy::HttpUploadImage(uploadUrl, p.uploadedMaskName, maskBytes,
                                          {{"type", "input"}, {"overwrite", "true"}});
        if (!up1.ok || !up2.ok) {
            async::PostToUi([e = up1.ok ? up2.error : up1.error, onFinish] {
                FailUi("上传失败：" + e);
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }

        const InpaintGraphResult graph = BuildInpaintGraph(p);
        if (!graph.ok) {
            async::PostToUi([e = graph.error, onFinish] {
                FailUi(e);
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        report(InpaintPhase::Submitting, "提交 /prompt…");
        auto sub = comfy::HttpPostJson(baseUrl + "/prompt", graph.apiJson, std::chrono::seconds{30});
        comfy::PromptSubmitResult pr;
        if (!comfy::ParsePromptSubmitJson(sub.body, pr) || !pr.ok || pr.promptId.empty()) {
            async::PostToUi([e = sub.error.empty() ? pr.error : sub.error, body = sub.body.substr(0, 200), onFinish] {
                FailUi(fmt::format("提交失败：{} {}", e, body));
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        const std::string promptId = pr.promptId;
        async::PostToUi([promptId, onFinish] {
            g_state.promptId = promptId;
            SetPhase(InpaintPhase::Running, "ComfyUI 生成中… prompt=" + promptId);
            if (onFinish) {
                onFinish(g_state);
            }
        });

        // 轮询 /history（worker 同步 HTTP；静默 20s 仍继续读一次）
        bool done = false;
        bool failed = false;
        std::string failMsg;
        comfy::HistoryMedia media;
        for (int i = 0; i < 60 && !g_cancel; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto hist = comfy::HttpGet(baseUrl + "/history/" + promptId, std::chrono::seconds{15});
            if (!hist.ok) {
                continue;
            }
            comfy::HistoryResult hr;
            // ParseHistoryJson expects array root of full /history; single-id endpoint is object
            // → 用整表 Fetch 语义：再拉 /history 一次
            auto all = comfy::HttpGet(baseUrl + "/history", std::chrono::seconds{20});
            if (comfy::ParseHistoryJson(all.body, 50, hr)) {
                for (const auto& e : hr.entries) {
                    if (e.promptId != promptId) {
                        continue;
                    }
                    if (e.failed) {
                        failed = true;
                        failMsg = e.statusText.empty() ? std::string{"服务端执行失败"} : e.statusText;
                        done = true;
                        break;
                    }
                    for (const auto& m : e.media) {
                        if (m.kind == "image") {
                            media = m;
                            done = true;
                            break;
                        }
                    }
                    if (done) {
                        break;
                    }
                    // 有记录但无 media：可能仍在写
                }
            }
            if (done) {
                break;
            }
        }
        if (g_cancel) {
            async::PostToUi([onFinish] {
                g_busy = false;
                SetPhase(InpaintPhase::Idle, "已中断");
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        if (failed) {
            async::PostToUi([failMsg, onFinish] {
                FailUi("生成失败：" + failMsg);
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        if (!done || media.fileName.empty()) {
            async::PostToUi([onFinish] {
                FailUi("超时：/history 未返回图片产物（检查模型/工作流或 Comfy 日志）");
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        report(InpaintPhase::Downloading, "下载 " + media.fileName + "…");
        const std::string viewUrl =
            comfy::BuildViewUrl(baseUrl, media.fileName, media.subfolder, media.type.empty() ? "output" : media.type);
        auto bin = comfy::HttpDownloadBinary(viewUrl, std::chrono::seconds{60});
        if (!bin) {
            async::PostToUi([e = bin.error().message, onFinish] {
                FailUi("下载失败：" + e);
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        std::filesystem::path dir = PaintOutputDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path path = dir / util::PathFromUtf8(media.fileName);
        if (!util::WriteFileBytes(path, *bin)) {
            async::PostToUi([onFinish] {
                FailUi("结果写盘失败");
                if (onFinish) {
                    onFinish(g_state);
                }
            });
            return;
        }
        const std::string pathUtf8 = util::PathToUtf8(path);
        async::PostToUi([pathUtf8, w, h, bin = std::string(*bin), onFinish] mutable {
            g_state.resultPath = pathUtf8;
            SetPhase(InpaintPhase::Done, "已生成 " + pathUtf8);
            g_busy = false;
            // 结果解码并回填画布由 UI 层 onFinish 处理（需要 canvas 可写）
            if (onFinish) {
                onFinish(g_state);
            }
        });
        log::Info("inpaint 完成：{} {}x{} → {}", media.fileName, w, h, pathUtf8);
    });
    return true;
}

bool ExportCanvasPng(const PaintCanvas& canvas, std::string_view utf8Path, std::string* error) {
    if (!canvas.valid()) {
        if (error != nullptr) {
            *error = "画布无效";
        }
        return false;
    }
    auto png = EncodePng(canvas.Width(), canvas.Height(), canvas.BasePixels());
    if (!png) {
        if (error != nullptr) {
            *error = PngCodecErrorText(png.error());
        }
        return false;
    }
    const std::string bytes(reinterpret_cast<const char*>(png->data()), png->size());
    if (!util::WriteFileBytes(util::PathFromUtf8(utf8Path), bytes)) {
        if (error != nullptr) {
            *error = "写文件失败";
        }
        return false;
    }
    return true;
}

int RunInpaintSelfCheck() {
    int fail = RunPngCodecSelfCheck();
    InpaintParams p;
    p.checkpoint = "sd15.safetensors";
    p.prompt = "a cat";
    p.uploadedImageName = "a.png";
    p.uploadedMaskName = "m.png";
    const InpaintGraphResult g = BuildInpaintGraph(p);
    if (g.ok && g.nodeCount == 9 && g.apiJson.find("VAEEncodeForInpaint") != std::string::npos &&
        g.apiJson.find("LoadImageMask") != std::string::npos && g.apiJson.find("KSampler") != std::string::npos) {
        log::Info("inpaint PASS graph 9 nodes");
    } else {
        ++fail;
        log::Error("inpaint FAIL graph ok={} nodes={} {}", g.ok, g.nodeCount, g.error);
    }
    InpaintParams bad;
    if (BuildInpaintGraph(bad).ok) {
        ++fail;
        log::Error("inpaint FAIL empty checkpoint should fail");
    } else {
        log::Info("inpaint PASS empty checkpoint rejected");
    }
    log::Info("INPAINT_SELF_CHECK {}", fail == 0 ? "PASS" : "FAIL");
    return fail;
}

} // namespace shine::paint
