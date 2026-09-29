#pragma once
// ui/verify/review —— 多模态评审取证脚手架（P0x Review 共用）
//
// 事实依据：P05–P10 六个评审文件此前各自抄了一份**逐字相同**的 Pump() 与
// Grab()（不同点只有排版），P03 另有一份等价实现。取证链一旦要改
// （比如 repaint 时机、manifest 记法），六处要同步改且极易漏 —— 收敛到这里。
//
// 语义与原实现完全一致，不新增行为：
//   * Pump：排干 UI 队列 + 处理事件 + 处理 DeferredDelete（截图前必须，
//     P02-S7 教训：grab 前不 repaint 会抓到空帧）；
//   * Grab：Pump → 空指针记 FAILED → repaint → grab → 落盘 → 记 manifest 行。
//
// 各评审文件仍保留自己的 State 结构体（字段不同），只共用这两个动作。
#include "core/Async.h"
#include "ui/kit/qml/QuickHost.h"
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QQuickWidget>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace shine::app::review {

// 截图前把 UI 队列与事件排干，保证 grab 拿到的是最新一帧。
inline void Pump() {
    shine::async::DrainUiQueue();
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

// QQuickWidget 的画面**不在 Widgets 的绘制链上**：repaint() + QWidget::grab()
// 抓的是「上一次场景图渲染留下的那一帧」，两者之间没有任何同步。
// 症状极隐蔽 —— 换实体后抓出来的图与换之前**逐字节相同**（md5 一致），
// manifest 照样记 "saved"，overall 照样 PASS，而那张图根本没反映这次选择。
// （本轮实测：assets-empty / assets-card-states / sheet-full 三张在 5054c7d
//  与本分支上都是同一个 md5。）
//
// 正确取法是走场景图自己的路径：QuickHost::GrabToImage（QQuickItem::grabToImage，
//  内部会先 Pump 一次再等 ready）。宿主本身不是 QuickHost 时（例如页面是个
//  装着 QQuickWidget 的普通 QWidget 容器），先 Pump 它的子宿主把场景图推一帧，
//  再退回 Widgets 抓图 —— 否则容器 grab 拿到的还是那一帧旧画面。
[[nodiscard]] inline QImage GrabWidgetImage(QWidget* widget) {
    if (auto* host = qobject_cast<shine::qml::QuickHost*>(widget); host != nullptr) {
        QImage captured = host->GrabBlocking(2000, widget->size());
        if (!captured.isNull()) {
            return captured;
        }
        widget->repaint();
        return widget->grab().toImage();
    }
    // 容器：先把内部每个 QuickHost 的场景图推到最新一帧，再抓容器。
    const auto hosts = widget->findChildren<shine::qml::QuickHost*>();
    for (shine::qml::QuickHost* host : hosts) {
        (void)host->GrabBlocking(2000, host->size());
    }
    if (!hosts.isEmpty()) {
        // GrabBlocking 已经让场景图重绘过一次，容器 grab 会读到新画面。
        widget->repaint();
        QApplication::processEvents();
    }
    widget->repaint();
    return widget->grab().toImage();
}

// 落盘 + 记一行 manifest（"<name> <bytes> saved|FAILED"）。
// Grab() 与 GrabImage() 共用这一段：两条路径的字节数口径必须一致，
// 否则同一页的两类图会算出两套「小于下限」的判据。
inline void SaveShot(const QImage& shot, const std::filesystem::path& dir, const std::string& name,
                     std::vector<std::string>& manifest) {
    const std::filesystem::path path = dir / (name + ".png");
    const bool ok =
        !shot.isNull() && shot.save(QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
    const auto bytes = shine::util::ReadFileBytes(path);
    manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) +
                       (ok ? " saved" : " FAILED"));
}

// 抓一张并记一行 manifest。
// manifest 收集到 std::vector<std::string>&；各评审文件自己拼报告。
inline void Grab(QWidget* widget, const std::filesystem::path& dir, const std::string& name,
                 std::vector<std::string>& manifest) {
    Pump();
    if (widget == nullptr) {
        manifest.push_back(name + " 0 FAILED null-widget");
        return;
    }
    SaveShot(GrabWidgetImage(widget), dir, name, manifest);
}

// 记一张**已经裁好**的图。
//
// 为什么需要：QML 页面整页只有一个 QQuickWidget，拿不到「子控件」可抓
// （P08 迁移前是分别抓 Canvas / Chain / Tasks / Final 四个 QWidget）。
// 现在的做法是「整页抓一次 + 按 QML 报出来的几何裁一块」——像素仍来自同一次
// 真实抓帧（QuickHost::GrabBlocking），裁剪不换视口、不把宿主拉高。
// 判据与 Grab 完全一致：仍走 EvaluateShots 的存在 / 字节数 / 逐字节重复三条。
inline void GrabImage(const QImage& shot, const std::filesystem::path& dir, const std::string& name,
                      std::vector<std::string>& manifest) {
    Pump();
    SaveShot(shot, dir, name, manifest);
}

// ─────────────────── 收尾判据（本轮新增：六份 Review 之前各写各的） ───────────────────
//
// **为什么收敛到这里**：P04 / P05 两份写对了（有 `overall=` 且退出码跟着判据走），
// P03 / P06 / P07 / P08 / P09 / P10 六份是「写完 manifest 就 `std::_Exit(0)`」——
// 缺图、超时、空图全都报成功。那种跑法既骗退出码也骗按 `overall:` 解析的 harness
// （读出来是 MISSING，像「没跑」而不是「跑挂了」）。
//
// 判据只有三条，缺一即 FAIL：
//   1. expected 里每个文件都存在；
//   2. 且字节数 ≥ min_bytes（一张几百字节的 PNG 基本等于没拍到东西）；
//   3. 可选：两两**逐字节相同**即 FAIL —— 两个图名共用一份像素 = 有一张没拍到
//      它该拍的状态（实测过三例：死属性导致切页失效 / 动作语义层级错 / 等待结果被丢）。
//      默认只报告不失败，因为同主题下的合法重复是可能的，由调用方按页面语义决定。
struct FinishOptions {
    std::filesystem::path dir;
    std::string header;                   // 报告首行，如 "P05-S9 visual review"
    std::vector<std::string> expected;    // 相对 dir 的文件名（含扩展名）
    const std::vector<std::string>* manifest = nullptr;
    const std::vector<std::string>* not_covered = nullptr;
    std::size_t min_bytes = 100;
    bool fail_on_duplicate = false;
    std::string report_name = "shots-manifest.txt";
    // 额外判据（如 P04 的 agent 自检结果）。空 vector = 不看。
    std::vector<std::pair<std::string, bool>> extra;
};

struct FinishResult {
    bool ok = true;
    std::string report;
    std::vector<std::string> missing;      // 文件不存在
    std::vector<std::string> undersized;   // 存在但小于 min_bytes
    std::vector<std::string> duplicates;
};

inline FinishResult EvaluateShots(const FinishOptions& opt) {
    FinishResult result;
    std::vector<std::pair<std::string, std::string>> digests; // (md5, name)
    for (const std::string& name : opt.expected) {
        const auto bytes = shine::util::ReadFileBytes(opt.dir / name);
        if (!bytes) {
            result.ok = false;
            result.missing.push_back(name);
            continue;
        }
        // ⚠️ 「太小」与「缺」分开报。一张 40 字节的 PNG 读成 MISSING 会把排障方向
        //    指到「抓图没跑」，而真实原因是「跑了但内容是空的」。
        if (bytes->size() < opt.min_bytes) {
            result.ok = false;
            result.undersized.push_back(name + " " + std::to_string(bytes->size()) + "B < " +
                                        std::to_string(opt.min_bytes) + "B");
            continue;
        }
        QByteArray raw(reinterpret_cast<const char*>(bytes->data()),
                       static_cast<qsizetype>(bytes->size()));
        digests.emplace_back(
            QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex())
                .toStdString(),
            name);
    }
    for (std::size_t i = 0; i < digests.size(); ++i) {
        for (std::size_t j = i + 1; j < digests.size(); ++j) {
            // 守卫比的是**名字 vs 名字**。（原先误写成 md5 vs 名字，靠「md5 是 32 位
            // 十六进制、文件名以 .png 结尾所以永远不相等」才恒真 —— 一旦调用方在
            // expected 里重复列了同一个名字，就会报出 `a.png == a.png` 的自重复。）
            if (digests[i].second == digests[j].second) {
                continue;
            }
            if (digests[i].first == digests[j].first) {
                result.duplicates.push_back(digests[i].second + " == " + digests[j].second);
            }
        }
    }
    if (opt.fail_on_duplicate && !result.duplicates.empty()) {
        result.ok = false;
    }
    for (const auto& [name, pass] : opt.extra) {
        if (!pass) {
            result.ok = false;
        }
    }

    std::string report = opt.header + "\n";
    if (opt.manifest != nullptr) {
        for (const std::string& line : *opt.manifest) {
            report += line + "\n";
        }
    }
    for (const std::string& name : result.missing) {
        report += name + " MISSING\n";
    }
    for (const std::string& name : result.undersized) {
        report += name + " UNDERSIZED\n";
    }
    if (!result.duplicates.empty()) {
        report += "--- duplicate shots (逐字节相同 = 有一张没拍到它该拍的) ---\n";
        for (const std::string& pair : result.duplicates) {
            report += pair + "\n";
        }
    }
    if (opt.not_covered != nullptr && !opt.not_covered->empty()) {
        report += "--- not covered (不计入 overall) ---\n";
        for (const std::string& line : *opt.not_covered) {
            report += line + "\n";
        }
    }
    for (const auto& [name, pass] : opt.extra) {
        report += name + " " + (pass ? "PASS" : "FAIL") + "\n";
    }
    report += "overall=" + std::string(result.ok ? "PASS" : "FAIL") + "\n";
    result.report = std::move(report);
    return result;
}

// 写报告 → 刷缓冲 → 按判据退出。**退出码必须跟着 ok 走** —— 恒 0 的验收等于没验收。
inline FinishResult WriteAndExit(const FinishOptions& opt) {
    const FinishResult result = EvaluateShots(opt);
    (void)shine::util::WriteFileBytes(opt.dir / opt.report_name, result.report);
    std::fflush(nullptr);
    std::_Exit(result.ok ? 0 : 1);
}

} // namespace shine::app::review
