#pragma once
// shine::motion::MotionScope —— 动效批的 RAII 托管（P02-S4）
//
// 作用域结束（页面关闭/切换）时把托管的 Tween 全部 Settle 到终态并回收，
// 防动效悬挂/泄漏（判据 S4：RAII 自动销毁）。
#include <cstddef>
#include <vector>

namespace shine::motion {

class Tween;

class MotionScope {
  public:
    MotionScope() = default;
    ~MotionScope(); // Settle + stop + deleteLater 全部托管动效

    MotionScope(const MotionScope&) = delete;
    MotionScope& operator=(const MotionScope&) = delete;

    void Adopt(Tween* t); // 移交所有权（nullptr 忽略）
    [[nodiscard]] std::size_t Count() const;

  private:
    std::vector<Tween*> owned_;
};

} // namespace shine::motion
