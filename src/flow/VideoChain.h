#pragma once
// P08-S4：相邻镜头首尾帧链式与断链记账。
#include <cstdint>
#include <string>
#include <vector>

namespace shine::flow {

struct VideoFrameState {
    std::int64_t shot_id = 0;
    std::string first_frame;
    std::string last_frame;
    std::string degradation;
};

struct VideoChainLink {
    std::int64_t from_shot = 0;
    std::int64_t to_shot = 0;
    bool connected = false;
    std::string strategy;
    std::string detail;
};

struct VideoChain {
    std::vector<VideoFrameState> shots;
    std::vector<VideoChainLink> links;
    [[nodiscard]] std::size_t BrokenCount() const;
    [[nodiscard]] std::string Describe() const;
};

[[nodiscard]] VideoChain BuildVideoChain(std::vector<VideoFrameState> shots);

} // namespace shine::flow
