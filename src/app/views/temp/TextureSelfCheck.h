#pragma once
// shine::app —— 【TEMP-G3】G-S3 的纹理层验收窗口
//
// 一行 4 张测试纹理（256/128/64/32）+ 首次进入时 500 次「创建→释放」。
// **按任务书 G-S6「移到网格后删掉」**：G-S6 用真实缩略图网格替换它时，把本目录整个删除，
// 并去掉 `App::Draw()` 末尾的调用（搜 `DrawTextureSelfCheck`）。
namespace shine::app {

void DrawTextureSelfCheck(); // 仅 UI 线程；设备未就绪时直接返回

} // namespace shine::app
