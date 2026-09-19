#pragma once
// S17：小说侧 headless 命令行入口（`main.cpp` 在 `--mcp-stdio` 分支之后、建窗口之前调用）。
//
// 支持：
//   `--novel-generate <chapter_id>`  生成一章（`0` 或缺省 → 第一张未完成的章）
//   `--novel-run <manual|semi|auto>` 连跑（`09` §2.1）
// 公共项：
//   `--db <path>`              工程库（缺省 = 设置里的 `mcpNovelDbPath`）
//   `--project <dir>`          工程根（缺省 = 库文件所在目录）
//   `--max N`                  连跑章数上限（0 = 不限）
//   `--checkpoint N`           检查点周期（缺省 10）
//   `--max-revisions N`        单章修订轮次（缺省 2）
//   `--max-total-calls N`      全书预算上限（`09` §2.4 / 09-12；缺省 0 = 不限）
//   `--auto-create`            无非完成章时自动建下一章
//
// 返回：0 = 成功；1 = 业务失败（生成失败 / `auto` 被拒启动）；2 = 参数或环境错。
namespace shine::app::novel {

int RunNovelCli(const wchar_t* cmdline);

} // namespace shine::app::novel
