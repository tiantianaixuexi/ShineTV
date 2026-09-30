#include "ui/imgui/kit/Icon_Path.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

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

} // namespace

// 解析一条 d 属性。支持 M m L l H h V v C c S s Q q T t A a Z z。
void ParseIconPath(const char* d, IconGlyph& glyph) {
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

} // namespace shine::kit
