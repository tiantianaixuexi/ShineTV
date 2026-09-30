#pragma once
// shine::kit::Icon_Path —— SVG `d` 属性的解析器
//
// 从 kit/Icon.cpp 拆出。这一族是**纯解析**：把设计稿的 24 网格路径字符串变成
// IconGlyph（子路径 + 线段表），不碰任何绘制、也不认识任何具体图标名。
//
// 单独成文件的理由：解析器（约 300 行状态机 + 圆弧升阶）是本文件里唯一有
// 「自己的一套语法」的部分，它面对的输入是**人写的 SVG 字符串**（要防
// "0-3.6" 这种没有分隔符的数字、残缺的 "1e" 指数、隐式重复命令）；而调用它的
// 注册表面对的是**内置常量表**（解析一次、缓存、之后只做仿射变换）。两者的
// 失败方式与改动理由完全不同：改图标要动表，改容错要动解析器。
//
// 出口只有一个：ParseIconPath()。刻意**不**导出 Scanner / AppendArc —— 它们是
// 解析器内部的实现细节（AppendArc 的「端点参数化 → 中心参数化 → 按 ≤90° 切段」
// 只有 ParsePath 的 'A' 分支会用）。

#include "ui/imgui/kit/Icon_Glyph.h"

namespace shine::kit {

// 把一条 SVG `d` 属性解析进 glyph（追加子路径，不清空已有内容）。
// 支持 M m L l H h V v C c S s Q q T t A a Z z；遇到未知命令整条放弃。
// 圆弧按 SVG 规范附录 F.6 转成 ≤90° 的三次贝塞尔段。
void ParseIconPath(const char* d, IconGlyph& glyph);

} // namespace shine::kit
