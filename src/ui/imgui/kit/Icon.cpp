#include "ui/imgui/kit/Icon.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace shine::kit {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- 扫描器
// SVG 路径的 number 语法：符号可作为分隔符（"0-3.6" 是两个数），
// 所以不能简单地按空白切分。
class Scanner {
public:
    explicit Scanner(const char* text) : p_(text) {}

    void SkipSeparators() {
        while (*p_ == ' ' || *p_ == ',' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r') {
            ++p_;
        }
    }

    bool AtEnd() {
        SkipSeparators();
        return *p_ == '\0';
    }

    char PeekCommand() {
        SkipSeparators();
        return *p_;
    }

    char TakeCommand() {
        SkipSeparators();
        return *p_++;
    }

    bool NextNumber(float& out) {
        SkipSeparators();
        const char* start = p_;
        if (*p_ == '+' || *p_ == '-') {
            ++p_;
        }
        bool digits = false;
        while (std::isdigit(static_cast<unsigned char>(*p_)) != 0) {
            ++p_;
            digits = true;
        }
        if (*p_ == '.') {
            ++p_;
            while (std::isdigit(static_cast<unsigned char>(*p_)) != 0) {
                ++p_;
                digits = true;
            }
        }
        if (!digits) {
            p_ = start;
            return false;
        }
        if (*p_ == 'e' || *p_ == 'E') {
            const char* mark = p_;
            ++p_;
            if (*p_ == '+' || *p_ == '-') {
                ++p_;
            }
            if (std::isdigit(static_cast<unsigned char>(*p_)) != 0) {
                while (std::isdigit(static_cast<unsigned char>(*p_)) != 0) {
                    ++p_;
                }
            } else {
                p_ = mark; // 1e 这种残缺指数：回退，当作没有指数
            }
        }
        out = static_cast<float>(std::strtod(std::string(start, p_).c_str(), nullptr));
        return true;
    }

private:
    const char* p_;
};

ImVec2 Lerp(ImVec2 a, ImVec2 b, float t) {
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// 端点参数化 → 中心参数化，再按 ≤90° 切成三次贝塞尔（SVG 规范附录 F.6）。
void AppendArc(IconSubPath& sub, ImVec2 from, float rx, float ry, float rotationDeg, bool largeArc,
               bool sweep, ImVec2 to) {
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < 1e-6f || ry < 1e-6f) {
        IconSegment line{'L', {to.x, to.y, 0, 0, 0, 0}};
        sub.segments.push_back(line);
        return;
    }
    const float phi = rotationDeg * static_cast<float>(kPi) / 180.0f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    const float dx2 = (from.x - to.x) * 0.5f;
    const float dy2 = (from.y - to.y) * 0.5f;
    const float x1p = cosPhi * dx2 + sinPhi * dy2;
    const float y1p = -sinPhi * dx2 + cosPhi * dy2;

    // 半径太小按规范放大，保证端点在椭圆上
    const float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0f) {
        const float scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }

    const float rx2 = rx * rx;
    const float ry2 = ry * ry;
    const float x1p2 = x1p * x1p;
    const float y1p2 = y1p * y1p;
    const float numerator = std::fmax(0.0f, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2);
    const float denominator = rx2 * y1p2 + ry2 * x1p2;
    const float sign = (largeArc != sweep) ? 1.0f : -1.0f;
    const float coefficient =
        (denominator < 1e-9f) ? 0.0f : sign * std::sqrt(numerator / denominator);
    const float cxp = coefficient * ((rx * y1p) / ry);
    const float cyp = coefficient * ((-ry * x1p) / rx);

    const float cx = cosPhi * cxp - sinPhi * cyp + (from.x + to.x) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (from.y + to.y) * 0.5f;

    const auto angle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        float a = std::acos(len < 1e-9f ? 1.0f : std::clamp(dot / len, -1.0f, 1.0f));
        if (ux * vy - uy * vx < 0.0f) {
            a = -a;
        }
        return a;
    };

    const float ux = (x1p - cxp) / rx;
    const float uy = (y1p - cyp) / ry;
    const float vx = (-x1p - cxp) / rx;
    const float vy = (-y1p - cyp) / ry;
    float theta = angle(1.0f, 0.0f, ux, uy);
    float delta = angle(ux, uy, vx, vy);
    if (!sweep && delta > 0.0f) {
        delta -= static_cast<float>(2.0 * kPi);
    } else if (sweep && delta < 0.0f) {
        delta += static_cast<float>(2.0 * kPi);
    }

    const int pieces = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) /
                                                              (static_cast<float>(kPi) * 0.5f))));
    const float step = delta / static_cast<float>(pieces);
    // 圆弧逼近三次贝塞尔：alpha = 4/3 * tan(delta/4)
    const float alpha = (4.0f / 3.0f) * std::tan(step * 0.25f);

    ImVec2 point = from;
    for (int i = 0; i < pieces; ++i) {
        const float t1 = theta + step * static_cast<float>(i);
        const float t2 = t1 + step;
        const ImVec2 p1(cx + rx * std::cos(t1), cy + ry * std::sin(t1));
        const ImVec2 p2(cx + rx * std::cos(t2), cy + ry * std::sin(t2));
        const ImVec2 d1(-rx * std::sin(t1), ry * std::cos(t1));
        const ImVec2 d2(-rx * std::sin(t2), ry * std::cos(t2));
        const ImVec2 c1(p1.x + alpha * d1.x, p1.y + alpha * d1.y);
        const ImVec2 c2(p2.x - alpha * d2.x, p2.y - alpha * d2.y);
        sub.segments.push_back(
            IconSegment{'C', {c1.x, c1.y, c2.x, c2.y, p2.x, p2.y}});
        point = p2;
    }
    (void)point;
}

// 解析一条 d 属性。支持 M m L l H h V v C c S s Q q T t A a Z z。
void ParsePath(const char* d, IconGlyph& glyph) {
    Scanner scanner(d);
    ImVec2 cursor{0, 0};
    ImVec2 subStart{0, 0};
    ImVec2 lastCubicControl{0, 0};
    ImVec2 lastQuadControl{0, 0};
    char previous = '\0';
    IconSubPath* sub = nullptr;

    const auto beginSub = [&](ImVec2 at) {
        glyph.subPaths.push_back(IconSubPath{});
        sub = &glyph.subPaths.back();
        sub->start = at;
    };

    const auto number = [&](float& value) { return scanner.NextNumber(value); };

    while (!scanner.AtEnd()) {
        char command = scanner.PeekCommand();
        if (std::isalpha(static_cast<unsigned char>(command)) != 0) {
            scanner.TakeCommand();
        } else if (command == '\0') {
            break;
        } else if (previous == '\0') {
            break; // 路径必须以命令字母开头
        } else {
            // 隐式重复：M 后续组当 L，其余命令按自身重复
            command = (previous == 'M') ? 'L' : (previous == 'm') ? 'l' : previous;
        }

        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));

        switch (op) {
        case 'M': {
            float x = 0;
            float y = 0;
            if (!number(x) || !number(y)) {
                return;
            }
            const ImVec2 at = relative ? ImVec2(cursor.x + x, cursor.y + y) : ImVec2(x, y);
            beginSub(at);
            cursor = at;
            subStart = at;
            break;
        }
        case 'L': {
            float x = 0;
            float y = 0;
            if (!number(x) || !number(y)) {
                return;
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            const ImVec2 at = relative ? ImVec2(cursor.x + x, cursor.y + y) : ImVec2(x, y);
            sub->segments.push_back(IconSegment{'L', {at.x, at.y, 0, 0, 0, 0}});
            cursor = at;
            break;
        }
        case 'H': {
            float x = 0;
            if (!number(x)) {
                return;
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            const ImVec2 at{relative ? cursor.x + x : x, cursor.y};
            sub->segments.push_back(IconSegment{'L', {at.x, at.y, 0, 0, 0, 0}});
            cursor = at;
            break;
        }
        case 'V': {
            float y = 0;
            if (!number(y)) {
                return;
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            const ImVec2 at{cursor.x, relative ? cursor.y + y : y};
            sub->segments.push_back(IconSegment{'L', {at.x, at.y, 0, 0, 0, 0}});
            cursor = at;
            break;
        }
        case 'C':
        case 'S': {
            ImVec2 control1{0, 0};
            ImVec2 control2{0, 0};
            ImVec2 to{0, 0};
            if (op == 'C') {
                float v[6] = {0, 0, 0, 0, 0, 0};
                for (float& value : v) {
                    if (!number(value)) {
                        return;
                    }
                }
                control1 = relative ? ImVec2(cursor.x + v[0], cursor.y + v[1]) : ImVec2(v[0], v[1]);
                control2 = relative ? ImVec2(cursor.x + v[2], cursor.y + v[3]) : ImVec2(v[2], v[3]);
                to = relative ? ImVec2(cursor.x + v[4], cursor.y + v[5]) : ImVec2(v[4], v[5]);
            } else {
                // S：参数是 (x2,y2,x,y)；第一个控制点是上一个 C 的第二控制点的镜像
                float v[4] = {0, 0, 0, 0};
                for (float& value : v) {
                    if (!number(value)) {
                        return;
                    }
                }
                const bool mirror = (previous == 'C' || previous == 'c' || previous == 'S' ||
                                     previous == 's');
                const ImVec2 first = mirror ? ImVec2(2 * cursor.x - lastCubicControl.x,
                                                    2 * cursor.y - lastCubicControl.y)
                                           : cursor;
                control1 = first;
                control2 = relative ? ImVec2(cursor.x + v[0], cursor.y + v[1]) : ImVec2(v[0], v[1]);
                to = relative ? ImVec2(cursor.x + v[2], cursor.y + v[3]) : ImVec2(v[2], v[3]);
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            sub->segments.push_back(
                IconSegment{'C', {control1.x, control1.y, control2.x, control2.y, to.x, to.y}});
            lastCubicControl = control2;
            cursor = to;
            break;
        }
        case 'Q':
        case 'T': {
            ImVec2 control{0, 0};
            ImVec2 to{0, 0};
            if (op == 'Q') {
                float v[4] = {0, 0, 0, 0};
                for (float& value : v) {
                    if (!number(value)) {
                        return;
                    }
                }
                control = relative ? ImVec2(cursor.x + v[0], cursor.y + v[1]) : ImVec2(v[0], v[1]);
                to = relative ? ImVec2(cursor.x + v[2], cursor.y + v[3]) : ImVec2(v[2], v[3]);
            } else {
                float v[2] = {0, 0};
                for (float& value : v) {
                    if (!number(value)) {
                        return;
                    }
                }
                const bool mirror =
                    (previous == 'Q' || previous == 'q' || previous == 'T' || previous == 't');
                control = mirror ? ImVec2(2 * cursor.x - lastQuadControl.x,
                                           2 * cursor.y - lastQuadControl.y)
                                 : cursor;
                to = relative ? ImVec2(cursor.x + v[0], cursor.y + v[1]) : ImVec2(v[0], v[1]);
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            // 二次 → 三次升阶
            const ImVec2 c1(cursor.x + (control.x - cursor.x) * (2.0f / 3.0f),
                            cursor.y + (control.y - cursor.y) * (2.0f / 3.0f));
            const ImVec2 c2(to.x + (control.x - to.x) * (2.0f / 3.0f),
                            to.y + (control.y - to.y) * (2.0f / 3.0f));
            sub->segments.push_back(
                IconSegment{'C', {c1.x, c1.y, c2.x, c2.y, to.x, to.y}});
            lastQuadControl = control;
            cursor = to;
            break;
        }
        case 'A': {
            float rx = 0;
            float ry = 0;
            float rotation = 0;
            float large = 0;
            float sweep = 0;
            float x = 0;
            float y = 0;
            if (!number(rx) || !number(ry) || !number(rotation) || !number(large) ||
                !number(sweep) || !number(x) || !number(y)) {
                return;
            }
            if (sub == nullptr) {
                beginSub(cursor);
            }
            const ImVec2 to = relative ? ImVec2(cursor.x + x, cursor.y + y) : ImVec2(x, y);
            AppendArc(*sub, cursor, rx, ry, rotation, large != 0.0f, sweep != 0.0f, to);
            cursor = to;
            break;
        }
        case 'Z': {
            if (sub != nullptr) {
                sub->segments.push_back(IconSegment{'L', {subStart.x, subStart.y, 0, 0, 0, 0}});
                sub->segments.push_back(IconSegment{'Z', {}});
                sub = nullptr;
            }
            cursor = subStart;
            break;
        }
        default:
            return; // 未知命令：整条路径放弃（图标库是内置常量，不会走到）
        }
        previous = command;
    }
}

// Icon.jsx:3-48 的 46 条 d 路径，逐字照抄（24 网格 / 1.6 描边）。
// 末尾两个是本实现补的：minus（真正的减号，JSX 把 x 当减号用）与
// flow（Gallery.jsx:140 传了未定义名字被兜底吞掉的 bug）。
const std::array<std::pair<std::string_view, const char*>, 48> kIconPaths{{
    {"gauge", "M12 13.5a1.8 1.8 0 1 0 0-3.6 1.8 1.8 0 0 0 0 3.6Zm1.6-2.7 3.4-3.4M4 20h16M5.5 17a8 8 0 1 1 13 0"},
    {"book", "M5 4.5A1.5 1.5 0 0 1 6.5 3H19v15.5H6.5A1.5 1.5 0 0 0 5 20V4.5ZM5 18.5A1.5 1.5 0 0 1 6.5 17H19M8.5 7h7M8.5 10h4"},
    {"masks", "M4.5 5.5c2.3-1.2 4.7-1.2 7 0v6.2c0 2.8-1.6 4.8-3.5 4.8S4.5 14.5 4.5 11.7V5.5ZM11.5 5.5c2.3-1.2 4.7-1.2 7 0v6.2c0 2.8-1.6 4.8-3.5 4.8M7 9h2.2M14 9h2.2M7.2 12.2c.9.7 1.8.7 2.7 0M14.2 12.2c.9.7 1.8.7 2.7 0"},
    {"clapper", "M4 9h16v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18V9Zm0 0-.6-2.9a1.5 1.5 0 0 1 1.2-1.8l11-1.9a1.5 1.5 0 0 1 1.7 1.1L18 6.5 4 9Zm4.6-4.4L7.3 8m6-3.6L12 8m6-3.2L16.8 8M9 13h6"},
    {"image", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11Zm0 8.2 4.2-4.2 5 5m1.6-1.6 1.7-1.7L20 14.7M15 9.5h.01"},
    {"film", "M4 5.5A1.5 1.5 0 0 1 5.5 4h13A1.5 1.5 0 0 1 20 5.5v13a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18.5v-13ZM4 9h16M4 15h16M8 4v16m8-16v16"},
    {"play", "M8 5.8v12.4a.6.6 0 0 0 .92.5l9.6-6.2a.6.6 0 0 0 0-1L8.92 5.3a.6.6 0 0 0-.92.5Z"},
    {"stop", "M7 7h10v10H7z"},
    {"search", "M15.8 15.8 20 20m-3-9.5a6.5 6.5 0 1 1-13 0 6.5 6.5 0 0 1 13 0Z"},
    {"settings", "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6Zm7.4-3a7.4 7.4 0 0 0-.1-1.2l2-1.5-2-3.4-2.3 1a7.4 7.4 0 0 0-2.1-1.3L14.5 3h-5l-.4 2.6a7.4 7.4 0 0 0-2 1.2l-2.4-1-2 3.5 2 1.5a7.4 7.4 0 0 0 0 2.4l-2 1.5 2 3.4 2.3-1a7.4 7.4 0 0 0 2.1 1.3l.4 2.6h5l.4-2.6a7.4 7.4 0 0 0 2-1.2l2.4 1 2-3.5-2-1.5c.1-.4.1-.8.1-1.2Z"},
    {"plus", "M12 5v14M5 12h14"},
    {"minus", "M5 12h14"},
    {"x", "M6 6l12 12M18 6 6 18"},
    {"chevron", "M9 5.5 15.5 12 9 18.5"},
    {"chevdown", "M5.5 9 12 15.5 18.5 9"},
    {"check", "M4.5 12.5 10 18 19.5 6.5"},
    {"alert", "M12 8.5V13m0 3.2h.01M10.3 4.2 2.8 17a1.6 1.6 0 0 0 1.4 2.4h15.6a1.6 1.6 0 0 0 1.4-2.4L13.7 4.2a1.6 1.6 0 0 0-2.8 0Z"},
    {"info", "M12 11v5m0-8.5h.01M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"},
    {"layers", "m12 3 9 5-9 5-9-5 9-5Zm9 9-9 5-9-5m18 4.5-9 5-9-5"},
    {"text", "M5 6.5V5h14v1.5M12 5v14M9 19h6"},
    {"chip", "M8 8h8v8H8V8Zm-2.5-2.5h13v13h-13v-13ZM9 3v2.5M15 3v2.5M9 18.5V21M15 18.5V21M3 9h2.5M3 15h2.5M18.5 9H21M18.5 15H21"},
    {"wave", "M3 12h2l2-6 3 12 3-9 2 5 2-2h4"},
    {"aperture", "M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Zm8.5-3.5a8.5 8.5 0 1 1-17 0 8.5 8.5 0 0 1 17 0ZM12 12h.01M12 3.5l4 5M20.5 12l-6.4.9M16.9 19.9l-3.8-5.2M7 19.9l2.4-6M3.5 12l6.4.9M7.1 4.1l3.8 5.2"},
    {"encode", "M5 15V9m3.5 8V8.5M12 19V6m3.5 10.5v-6M19 15v-3"},
    {"grid", "M4 4h7v7H4V4Zm9 0h7v7h-7V4ZM4 13h7v7H4v-7Zm9 0h7v7h-7v-7Z"},
    {"refresh", "M19 12a7 7 0 1 1-2-4.9M19 4v4h-4"},
    {"folder", "M3.5 7A1.5 1.5 0 0 1 5 5.5h4l2 2.5h8A1.5 1.5 0 0 1 20.5 9.5v8A1.5 1.5 0 0 1 19 19H5a1.5 1.5 0 0 1-1.5-1.5V7Z"},
    {"palette", "M12 21a9 9 0 1 1 9-9c0 2-1.5 3-3 3h-2a2 2 0 0 0-1.5 3.3c.4.5.4 1.2-.2 1.5-.7.2-1.5.2-2.3.2ZM7.5 11h.01M10 7.8h.01M14.5 7.5h.01"},
    {"terminal", "m5 8 3.5 3.5L5 15m7 1.5h7M4 4.5h16A1.5 1.5 0 0 1 21.5 6v12a1.5 1.5 0 0 1-1.5 1.5H4A1.5 1.5 0 0 1 2.5 18V6A1.5 1.5 0 0 1 4 4.5Z"},
    {"list", "M9 6h11M9 12h11M9 18h11M4 6h.01M4 12h.01M4 18h.01"},
    {"sparkles", "M12 4.5 13.8 9l4.7 1.8-4.7 1.8L12 17l-1.8-4.4L5.5 10.8 10.2 9 12 4.5ZM19 15l.9 2.1L22 18l-2.1.9L19 21l-.9-2.1L16 18l2.1-.9L19 15ZM5.5 3l.7 1.8L8 5.5l-1.8.7L5.5 8l-.7-1.8L3 5.5l1.8-.7L5.5 3Z"},
    {"wand", "m15 6 3 3M9 18 20 7l-3-3L6 15l-1.5 4.5L9 18ZM6.5 9.5 8 8m3.5 8.5L13 15"},
    {"dots", "M6 12h.01M12 12h.01M18 12h.01"},
    {"download", "M12 4v10m0 0 4-4m-4 4-4-4M5 19h14"},
    {"upload", "M12 14V4m0 0L8 8m4-4 4 4M5 19h14"},
    {"eye", "M12 5.5c5 0 8.5 4.2 9.5 6.5-1 2.3-4.5 6.5-9.5 6.5S3.5 14.3 2.5 12c1-2.3 4.5-6.5 9.5-6.5Zm0 9a2.5 2.5 0 1 0 0-5 2.5 2.5 0 0 0 0 5Z"},
    {"compare", "M12 3v18M8 7 4.5 12 8 17M16 7l3.5 5L16 17"},
    {"link", "M9.5 14.5 14.5 9.5M8 11 5.8 13.2a3.8 3.8 0 0 0 5.4 5.4L13.4 16m2.6-3 2.2-2.2a3.8 3.8 0 0 0-5.4-5.4L10.6 8"},
    {"zap", "M13 3 5 13.5h5.5L11 21l8-10.5h-5.5L13 3Z"},
    {"panel", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM14.5 5v14"},
    {"panelL", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM9.5 5v14"},
    {"dock", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM4 14.5h16"},
    {"moon", "M20 13.5A8 8 0 0 1 10.5 4 8 8 0 1 0 20 13.5Z"},
    {"target", "M12 13.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Zm0 4.5a6 6 0 1 0 0-12 6 6 0 0 0 0 12Zm0-16.5v3m0 17v-3M3 12h3m12 0h3"},
    {"clock", "M12 7v5l3 2m6-2a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"},
    {"users", "M9 11.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Zm-6 8c.6-3.4 3-5.5 6-5.5s5.4 2.1 6 5.5m1-13.9a3.5 3.5 0 0 1 0 6.8m1.7 2.4c2 .7 3.5 2.4 4 4.7"},
    {"bolt2", "M8 3h8l-1 6h4L8.5 21l1.5-8H5L8 3Z"},
    {"flow", "M4 6.5A1.5 1.5 0 0 1 5.5 5h13A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5v-11ZM7.5 9.5h4M15 14.5h-4m2.5-2.5L12 9.5m0 5 2.5 2.5"},
}};

class IconRegistry {
public:
    static const IconRegistry& Instance() {
        static const IconRegistry registry;
        return registry;
    }

    const IconGlyph* Find(std::string_view name) const {
        if (const auto it = byName_.find(std::string(name)); it != byName_.end()) {
            return &glyphs_[it->second];
        }
        return &glyphs_[fallback_];
    }

    const std::vector<std::string_view>& Names() const { return names_; }

private:
    IconRegistry() {
        glyphs_.resize(kIconPaths.size());
        names_.reserve(kIconPaths.size());
        for (std::size_t i = 0; i < kIconPaths.size(); ++i) {
            ParsePath(kIconPaths[i].second, glyphs_[i]);
            names_.push_back(kIconPaths[i].first);
            byName_.emplace(std::string(kIconPaths[i].first), i);
            if (kIconPaths[i].first == "info") {
                fallback_ = i;
            }
        }
    }

    std::vector<IconGlyph> glyphs_;
    std::vector<std::string_view> names_;
    std::unordered_map<std::string, std::size_t> byName_;
    std::size_t fallback_ = 0; // info
};

} // namespace

const IconGlyph* GetIcon(std::string_view name) { return IconRegistry::Instance().Find(name); }

const std::vector<std::string_view>& IconNames() { return IconRegistry::Instance().Names(); }

void DrawIcon(ImDrawList* draw, std::string_view name, ImVec2 pos, float size, ImU32 color,
              float thickness) {
    const IconGlyph* glyph = GetIcon(name);
    if (glyph == nullptr) {
        return;
    }
    const float scale = size / 24.0f;
    const float width = std::fmax(1.0f, thickness * scale);
    for (const IconSubPath& sub : glyph->subPaths) {
        if (sub.segments.empty()) {
            continue;
        }
        draw->PathClear();
        draw->PathLineTo(ImVec2(pos.x + sub.start.x * scale, pos.y + sub.start.y * scale));
        for (const IconSegment& segment : sub.segments) {
            if (segment.op == 'L') {
                draw->PathLineTo(ImVec2(pos.x + segment.p[0] * scale, pos.y + segment.p[1] * scale));
            } else if (segment.op == 'C') {
                draw->PathBezierCubicCurveTo(
                    ImVec2(pos.x + segment.p[0] * scale, pos.y + segment.p[1] * scale),
                    ImVec2(pos.x + segment.p[2] * scale, pos.y + segment.p[3] * scale),
                    ImVec2(pos.x + segment.p[4] * scale, pos.y + segment.p[5] * scale));
            }
        }
        draw->PathStroke(color, 0, width);
    }
}

void DrawIconCentered(ImDrawList* draw, std::string_view name, ImVec2 center, float size,
                      ImU32 color, float thickness) {
    DrawIcon(draw, name, ImVec2(center.x - size * 0.5f, center.y - size * 0.5f), size, color,
             thickness);
}

} // namespace shine::kit
