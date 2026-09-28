#include "flow/VideoChain.h"

#include <algorithm>

namespace shine::flow {

std::size_t VideoChain::BrokenCount() const {
    return static_cast<std::size_t>(std::count_if(links.begin(), links.end(), [](const auto& link) {
        return !link.connected;
    }));
}

std::string VideoChain::Describe() const {
    return "镜头 " + std::to_string(shots.size()) + " · 断链 " + std::to_string(BrokenCount());
}

VideoChain BuildVideoChain(std::vector<VideoFrameState> shots) {
    VideoChain chain;
    chain.shots = std::move(shots);
    for (std::size_t i = 0; i + 1 < chain.shots.size(); ++i) {
        VideoChainLink link;
        link.from_shot = chain.shots[i].shot_id;
        link.to_shot = chain.shots[i + 1].shot_id;
        const auto& previous = chain.shots[i];
        const auto& next = chain.shots[i + 1];
        if (!previous.last_frame.empty() && previous.last_frame == next.first_frame) {
            link.connected = true;
            link.strategy = "chain";
            link.detail = "上一镜尾帧 = 下一镜首帧";
        } else {
            link.connected = false;
            link.strategy = "chain_ignored";
            link.detail = "首尾帧不一致；可跳过该镜、指定参考帧或仅用首帧重跑";
        }
        chain.links.push_back(std::move(link));
    }
    return chain;
}

} // namespace shine::flow
