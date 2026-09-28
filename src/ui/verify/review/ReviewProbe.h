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
#include "util/Encoding.h"
#include "util/File.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
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

// 抓一张并记一行 manifest（"<name> <bytes> saved|FAILED"）。
// manifest 收集到 std::vector<std::string>&；各评审文件自己拼报告。
inline void Grab(QWidget* widget, const std::filesystem::path& dir, const std::string& name,
                 std::vector<std::string>& manifest) {
    Pump();
    if (widget == nullptr) {
        manifest.push_back(name + " 0 FAILED null-widget");
        return;
    }
    widget->repaint();
    const std::filesystem::path path = dir / (name + ".png");
    const bool ok = widget->grab().save(QString::fromStdString(shine::util::PathToUtf8(path)), "PNG");
    const auto bytes = shine::util::ReadFileBytes(path);
    manifest.push_back(name + " " + std::to_string(bytes.value_or(std::string{}).size()) +
                       (ok ? " saved" : " FAILED"));
}

} // namespace shine::app::review
