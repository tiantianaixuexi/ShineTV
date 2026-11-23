# ShineTV Studio 界面与交互规范

[交互设计稿](ui-interaction.html) · [原视觉基准](design-spec.md) · [代码地图](README.md)

## 设计决策

面向在同一个 Windows 工程里处理小说、设定、分镜和生成结果的创作者。Hallmark 的 Bloom 方向用于纸墨主题，用暖纸背景、墨色正文和陶土红标记主操作、当前选择及焦点。项目中心用「最近打开」和项目索引承载继续工作的入口。小说以阅读正文为主，分镜以选中镜头和时间线为主，出图和出片以任务面板、节点与产物为主。新版在原有项目结构和工作区入口上更换视觉系统与布局。

设计稿中的项目封面是原创示例剧照，来自 ImageGen，提示为「暴雨蓝调时刻的空乡村站台，右侧暖色站灯，湿轨反光，无文字与人物」。应用的项目封面使用索引中的真实文件，缺图时显示模板图标。演示数据以仓库 fixture 为依据，实际工程在原生程序里打开。 [示例剧照](ui-demo-railway.png) · [Hallmark](https://github.com/Nutlope/hallmark)

<details>
<summary>示例剧照的生成提示</summary>

使用内置 ImageGen。

> Generate an original cinematic still, used only as a demo project cover in a professional Chinese indie film production desktop application mockup. Wide landscape 3:2 photograph. Empty rural railway platform at blue hour in heavy rain, with one warm amber station lamp and wet tracks, quiet dark blue shadows, practical production-lighting feel, late 1990s, restrained grain. Large legible cinematic shapes, read well as a 16:9 center crop. No text, no signage, no people, no brand, no borders, no overlays. Compose the lamp and curved tracks in the right third, leave the left darker and quiet. Photorealistic production still, avoid oversaturated sci-fi or fantasy.

</details>

## 工作流

| 页面 | 用户动作与结果 | 溢出和反馈 |
|---|---|---|
| 项目中心 | 新建或打开项目，选择项目进入工作区 | 项目网格随宽度换列。路径可在项目详情中查看，项目名称优先显示。 |
| 总控 | 查看阶段、预算、队列、账本和最近产物，使用顶部运行或停止 | 页面主滚动负责账本和规则表。右栏可以独立滚到最后一张卡。执行体未接入时使用明确的状态文案。 |
| 小说 | 从侧栏选择卷章，在正文、设定及相关页签切换 | 当前卷章在面包屑末段，完整标题保留在正文头部。章节树与正文各自滚动。 |
| 资产 | 选择实体，切换总览与详情，打开设定图 | 网格换列，详情与主滚动一起走。图片从加载到就绪后可打开查看器。 |
| 分镜 | 选择镜头，对照画面、参数和连续性报告 | 镜头条横向滚动，列表纵向滚动。选择镜头后详情与时间线一致。 |
| 出图 | 浏览节点图，在绑定、批量、图评审和结果间切换 | 工具条限在画布列，标题给提交按钮让位。任务面板内的长内容与画布缩放分开。 |
| 出片 | 核对首尾帧链、视频任务和成片 | 保留画布、任务面板和胶片条，横向内容通过同区滚动访问。 |
| 全局 | 顶栏回项目中心或运行，`Ctrl+K` 打开命令，`Esc` 逐层关闭浮层 | `Ctrl+B` 切侧栏，`Ctrl+I` 切检查器，`Ctrl+J` 切底栏。浮层打开时下层暂停响应。 |

## 项目中心和创建

| 状态 | 动作和反馈 | 实现 |
|---|---|---|
| 项目中心 | 最近打开项目置于索引前。搜索项目名或创意，按最近打开时间或名称排序。逐行打开、在资源管理器显示、切换主题或进入移除确认。 | [项目中心](../src/ui/imgui/pages/ProjectHub.cpp) · [项目行](../src/ui/imgui/pages/HubCard.cpp) |
| 打开项目 | 最近项目列表可滚动。选择其中一项读取该工程，也可用「浏览目录」选择含 `project.json` 的已有项目。读取失败保留对话框及错误。 | [打开对话框](../src/ui/imgui/pages/HubDialogs.cpp) · [打开流程](../src/ui/imgui/pages/HubState.cpp) |
| 新建 1／4 | 选择小说、影视或空白模板，以模板注册表实际文案为准。下一步保留选中模板。 | [模板来源](../src/project/ProjectTemplate.cpp) |
| 新建 2／4 | 输入名称，选择父目录，预览目标路径和该模板的文件骨架。空名称留在当前步并显示错误。 | [新建向导](../src/ui/imgui/pages/HubWizard.cpp) |
| 新建 3／4 | 创意可留空，输入后显示 Unicode 字符数。超过 200 字留在当前步。返回上一步保留已填数据。 | [新建向导](../src/ui/imgui/pages/HubWizard.cpp) |
| 新建 4／4 | 核对项目名、目标目录、模板和创意。创建成功进入工作区，失败停留在确认步并显示服务错误。取消退出向导。 | [新建向导](../src/ui/imgui/pages/HubWizard.cpp) · [工作区绑定](../src/ui/imgui/pages/Shell.cpp) |
| 移除 | 确认框写清移除对象。操作只更新最近项目索引，磁盘工程可通过「打开项目」重新加入。 | [移除确认](../src/ui/imgui/pages/HubDialogs.cpp) · [项目状态](../src/ui/imgui/pages/HubState.cpp) |

## 工作区数据与操作

| 区域 | 数据、选择与状态 | 实现 |
|---|---|---|
| 总控 | 显示阶段流、KPI、甘特、账本和 S1–S12。右栏先显示最近生成产物，再显示预算停止规则、运行信息与章节阶段矩阵。执行体不可用时说明原因，账本显示空态。 | [总控](../src/ui/imgui/pages/WorkspaceA.cpp) · [右栏](../src/ui/imgui/pages/OverviewRightRail.cpp) |
| 小说／章节 | 使用当前章的标题、字数、状态、摘要和正文。侧栏或横向章条切章后同步正文、章节属性与该章镜列表。无章、读取中和读失败各显示实际状态。 | [小说](../src/ui/imgui/pages/Page_Novel.cpp) · [章节查询](../src/ui/imgui/pages/BookQuery.cpp) |
| 小说／设定、流水线 | 设定展示已读实体与摘要。流水线显示阶段定义和当前章可查产物。初始化、评审、模型、状态、自动分别说明对应接口尚未接入。 | [小说模式](../src/ui/imgui/pages/Page_Novel.cpp) |
| 资产／总览 | 主区与侧栏共用实体类别筛选，网格和侧栏树各自滚动。选择实体叶子打开详情，长实体名按槽宽截断。 | [资产页](../src/ui/imgui/pages/Page_Assets.cpp) · [资产侧栏](../src/ui/imgui/pages/Shell_Sidebar.cpp) |
| 资产／详情 | 状态和查看动作占不同列。属性包含类别、实体标识和状态、视觉资产、设定状态、生产状态、形象层、设定图与 V0 派生状态。四个派生层与联系表按同一顺序。缺图时展示现有状态，打开或导出整版走查看器。 | [资产详情](../src/ui/imgui/pages/Page_Assets.cpp) · [查看请求](../src/ui/imgui/pages/MediaViewer_Request.cpp) |
| 分镜 | 按章更新 V1–V8 状态，选镜显示镜号、场次、动作、表情、时长、台词、旁白、机位与灯光。连续性列表独立纵向滚动。时间线横向滚动，拖到末尾能选最后一镜。 | [分镜](../src/ui/imgui/pages/Page_Storyboard.cpp) |
| 出图／绑定、批量 | 绑定展示镜头和素材对应。批量列当前章候选镜、动作与设定状态，行内长动作省略。提交入口给出现有服务状态反馈。 | [出图](../src/ui/imgui/pages/Page_ImageFlow.cpp) |
| 出图／图评审、结果 | 图评审读取视觉资产生产状态，目前无逐项评分数据。结果随选中镜更新，显示成图、所属章节、场景、出图及设定状态和已有的 Prompt、模型、步数、分辨率。没有文件、加载中与文件不可用分别反馈。重跑按钮说明服务缺口。 | [出图结果](../src/ui/imgui/pages/Page_ImageFlow.cpp) · [生成参数查询](../src/ui/imgui/pages/BookQuery.cpp) |
| 出片／首尾帧链、视频任务 | 相邻镜构成首尾帧链。视频任务读取 Comfy 队列快照，与底栏共享数据；进度由快照提供。 | [出片](../src/ui/imgui/pages/Page_VideoFlow.cpp) · [底栏队列](../src/ui/imgui/pages/Shell_Dock.cpp) |
| 出片／成片 | 显示本章镜数、总时长和视觉资产，成片条横向浏览逐镜预览。成片视频文件及视频编解码参数暂无只读投影。 | [成片与胶片条](../src/ui/imgui/pages/Page_VideoFlow.cpp) |
| 组件画廊 | 用同一套主题和控件渲染按钮、标签、表单、进度、步骤、列表和表格。网格按同一行最高内容推进，最后一张卡可滚到。 | [组件画廊](../src/ui/imgui/pages/Gallery.cpp) |

## 项目面板和浮层

| 区域 | 操作与验收 | 实现 |
|---|---|---|
| 项目侧栏 | 工作区切换后展示对应章节、镜头或实体树。树行选中与主区联动，最后一行可单独滚到。 | [侧栏](../src/ui/imgui/pages/Shell_Sidebar.cpp) · [树节点](../src/ui/imgui/pages/Shell_Sidebar_Tree.cpp) |
| 检查器 | 属性、预览、关联可折叠。属性和关联随当前选中对象变化，展开后的段高从实际行数推导。矮窗由面板自身滚动。 | [检查器](../src/ui/imgui/pages/Shell_Inspector.cpp) |
| 底栏／任务队列、日志 | 队列有等待、运行、失败和空态，进度随快照。日志保留最近的可见尾部并提示条数范围。 | [底栏](../src/ui/imgui/pages/Shell_Dock.cpp) |
| 底栏／产物、校验报告 | 产物来自工程输出目录，条目可交给系统打开。报告来自章节工作目录，选中后可读检查项、严重度和说明，并导出真实 JSON。 | [产物](../src/ui/imgui/pages/Shell_Dock.cpp) · [报告](../src/ui/imgui/pages/Shell_Reports.cpp) |
| 项目与服务设置 | 上部查看工程根、运行环境及输出位置。LLM 配置支持供应商、协议、端点、密钥、默认模型和规划／写作／评审／提取角色模型，以及 MiMo Token Plan 和 MiniMax 关思考。以实际保存时机为准，设计稿无网络请求。 | [设置界面](../src/ui/imgui/pages/Shell_Palette.cpp) · [应用设置](../src/core/Settings.h) |
| 命令面板 | 组合搜索、可点击结果、方向键、回车、退出。方向键跟随选中行，鼠标滚动和拖动滑块可浏览其余条目。 | [命令面板](../src/ui/imgui/pages/Shell_Palette.cpp) |
| 媒体查看器 | 支持上／下一张、缩放、适屏和放大镜。联系表与单张查看分别展示。复制、另存为及整版导出由当前文件支持情况决定。 | [查看器](../src/ui/imgui/pages/MediaViewer.cpp) |

## 布局和阅读规则

- 100% 逻辑尺寸下沿用现有主题和 56 单位活动栏。宽屏并列显示侧栏、主工作区和检查器。空间不足时优先为工作区保留宽度，活动栏或快捷键可以切换优先显示的面板。恢复宽度后还原面板偏好。
- 矮窗缩减底栏高度。活动栏在按钮放不全时可独立纵向滚动，组件画廊跟在最后一项。工作区里的宽表和流程可横向到达，活动栏、顶栏和状态栏固定。
- 正文与动作沿用 13 单位字号。面包屑和状态文字使用至少 12 单位。可操作的标题与状态优先保留，辅助提示在空间不足时让位。长中文项目名、路径及混排名称按实际字宽省略，文本始终留在自己的区域。
- 列表最末项可由滚轮或拖动滑块到达。轨道只在有溢出时出现，滑块悬停及拖动时用主题强调色。宽窗在画布上用滚轮缩放。窄窗把任务面板排在画布下，滚轮滚页面，`Ctrl+滚轮` 缩放节点，出片胶片条仍可横向浏览。

## DPI 与验收

`逻辑尺寸 = 客户区物理尺寸 ÷ 缩放系数`。窗口跟随所在显示器，Windows 换屏通知统一更新尺寸、裁剪、字体位图密度和鼠标。设计稿的缩放选择用来比较物理像素与同一套逻辑排版。

| 验收场景 | 判定 |
|---|---|
| 100 / 125 / 150 / 200% | 同逻辑窗口的排版、滚动和目标命中一致，截图尺寸按比例变化。 |
| 900 × 560 逻辑窗口 | 面包屑不覆盖提示，活动栏可达末项，侧栏和检查器可切换，流程工具条不压住任务页签。 |
| 长项目名与中文路径 | 省略号留在文本槽内，标题、按钮、状态分别有独立空间。 |
| 长列表与项目切换 | 最后一项可达，滚动距离与真实内容一致，切换工作区不继承上一页滚动位置。 |
| 双屏跨 DPI | 拖过屏幕边界时窗口位置、文字和指针命中同步。需两台不同缩放率的显示器实机验收。 |

基准回归从仓库根执行 `out/_make_fixture.ps1`，然后运行 `out/_run_review.ps1 -Theme paperink`。DPI 快速取证使用同一 fixture 和脚本，额外设置 `SHINE_IMGUI_REVIEW_SMOKE=1`、`SHINE_IMGUI_WINDOW=900x560`，分别用 `SHINE_UI_SCALE=1、1.25、1.5、2` 启动。它核对逻辑客户区、Win32 鼠标坐标、导航点击和滚动到底。双屏搬动与持续滚动的手感另外验收。

本机产物见 [37 张设计图](../build/ui-hallmark/index.html) 和 [原生截图及验收报告](../build/ui-native-hallmark/index.html)。原生向导保留模态承载，设计稿以独立页面展示同样的四步和字段。
