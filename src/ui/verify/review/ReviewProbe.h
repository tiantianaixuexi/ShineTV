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
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QQuickWidget>
#include <QWidget>

#include <filesystem>
#include <string>
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

// 抓一张并记一行 manifest（"<name> <bytes> saved|FAILED"）。
// manifest 收集到 std::vector<std::string>&；各评审文件自己拼报告。
inline void Grab(QWidget* widget, const std::filesystem::path& dir, const std::string& name,
                 std::vector<std::string>& manifest) {
    Pump();
    if (widget == nullptr) {
        manifest.push_back(name + " 0 FAILED null-widget");
        return;
    }
    const QImage shot = GrabWidgetImage(widget);
    const std::filesystem::path path = dir / (name + ".png");
    const bool ok =
        !shot.isNull() && shot.save(QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
    const auto bytes = shine::util::ReadFileBytes(path);
    manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) +
                       (ok ? " saved" : " FAILED"));
}

} // namespace shine::app::review
