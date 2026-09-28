#pragma once
// shine::video —— 节点发号器（P5.4 S1）
//
// ComfyUI API JSON 的节点 id 是**字符串**（`"1"`、`"2"`…）。发号规则只有一条：**按调用顺序从 1 递增**。
// 这样"同一输入编译两次"才会**逐字节一致**（验收 S7 的核心判据）——任何"按地址/时间/随机"发号都会破坏它。
#include <cstddef>
#include <string>

#include "util/Strings.h"

namespace shine::video {

class NodeIdPool {
public:
    [[nodiscard]] std::string Next() { return util::FromInt(++next_); }

    [[nodiscard]] std::size_t Count() const noexcept { return next_; }

    void Reset() noexcept { next_ = 0; }

private:
    std::size_t next_ = 0;
};

} // namespace shine::video
