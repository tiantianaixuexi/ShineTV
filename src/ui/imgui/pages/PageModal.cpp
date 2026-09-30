#include "ui/imgui/pages/PageModal.h"

#include "ui/imgui/pages/PageCommon.h"

#include <cstring>

namespace shine::pages {

using namespace shine::kit;

ModalBox DrawModal(ImDrawList* draw, Rect area, std::string_view title, std::string_view icon,
                   float width, float height, bool* dismiss, int footerButtons) {
    const kit::ModalFrame mf =
        kit::ModalFrameRect(draw, area, title, icon, width, height, footerButtons);
    // 点遮罩关闭：遮罩**不注册命中**（见 Overlays.cpp 的说明 —— 注册会抢
    // HoveredId，模态里每个按钮就永远点不动），所以在这里手算「点击是否落在
    // 面板外」。这与报告模态同一做法，两处共用的是同一份 kit 外壳。
    // ⚠️ 同样不能用 ImGui::IsMouseHoveringRect —— 在**本工程**它会 0xC0000005。
    if (dismiss != nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !mf.frame.contains(ImGui::GetIO().MousePos)) {
        *dismiss = true;
    }
    ModalBox box;
    box.frame = mf.frame;
    box.header = mf.header;
    box.body = mf.body;
    box.footer = mf.footer;
    return box;
}

std::vector<int> DrawModalFooter(ImDrawList* draw, Rect footer,
                                 const std::vector<ModalAction>& actions) {
    std::vector<int> hit(actions.size(), 0);
    float x = footer.max.x - 20.0f;
    for (std::size_t i = actions.size(); i-- > 0;) {
        const ModalAction& action = actions[i];
        const std::string label(action.label);
        const float textSize = action.spec.size == ButtonSize::Small ? 12.0f : 13.0f;
        ImFont* font = action.spec.variant == ButtonVariant::Primary ? FontBoldAt(textSize)
                                                                    : FontAt(textSize);
        const float iconW = action.spec.icon.empty()
                                ? 0.0f
                                : (action.spec.size == ButtonSize::Small ? 13.0f : 15.0f);
        const float width =
            ButtonWidth(action.spec.size, iconW, LabelWidth(font, textSize, label.c_str()));
        const float height = ButtonHeight(action.spec.size);
        const Rect bounds{x - width, footer.center().y - height * 0.5f, x,
                          footer.center().y + height * 0.5f};
        hit[i] = Button(draw, bounds, label, action.spec, action.id) ? 1 : 0;
        x -= width + 8.0f;
    }
    return hit;
}

void ModalFooterHint(ImDrawList* draw, Rect footer, std::string_view text) {
    draw->AddText(FontAt(11.0f), 11.0f, ImVec2(footer.min.x + 20.0f, footer.center().y - 5.5f),
                  ColorTextMuted(), text.data(), text.data() + text.size());
}

} // namespace shine::pages
