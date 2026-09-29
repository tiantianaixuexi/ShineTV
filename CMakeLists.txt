cmake_minimum_required(VERSION 3.20)
project(ShineTVStudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(THIRD ${CMAKE_SOURCE_DIR}/third)

# --- Qt 6（MSYS2 MinGW64 自带；与 GCC 16 同源，ABI 一致）---
# UI 使用 Qt 6 Widgets；模块边界见 docs/00-overview/architecture.md。
#
# QML 分层（2026-09-29 架构层）：Quick/Qml 只给新页面用，既有 Widgets 全部保留。
#   Widgets → QuickWidgets → Qml 逐层叠加，不反向依赖：
#     shine_kit     仍是纯 Widgets 控件层（不引入任何 QML 头）
#     shine_qml     新增：QQuickWidget 宿主 + 主题 token 的 QML 桥（只依赖 shine_kit）
#     ShineTVStudio 前端同时链两层，页面按需二选一或嵌套
#   QuickWidgets 必须显式列出：只写 Quick 拿不到 QQuickWidget 那个类。
list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
#   QuickEffects 必须显式列出：QtQuick.Effects（MultiEffect = box-shadow / 阴影）
#   是独立 QML 模块，不写它 QML 侧 import QtQuick.Effects 会整页加载失败。
find_package(Qt6 REQUIRED COMPONENTS Widgets Quick QuickWidgets Qml QuickEffects)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- libhv: HTTP + WebSocket client (static only, no server/examples) ---
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(WITH_HTTP_SERVER ON CACHE BOOL "" FORCE)
set(WITH_OPENSSL OFF CACHE BOOL "" FORCE)
set(WITH_GNUTLS OFF CACHE BOOL "" FORCE)
set(WITH_MBEDTLS OFF CACHE BOOL "" FORCE)
set(WITH_LUA OFF CACHE BOOL "" FORCE)
set(WITH_RPC OFF CACHE BOOL "" FORCE)
set(SHINE_SKIP_LIBHV_RC ON)
add_subdirectory(${THIRD}/libhv EXCLUDE_FROM_ALL)

# --- zlib + libpng：图片解码（P4.1 与图片库 G 线共用；不在 third/ 里改任何文件）---
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD}/zlib EXCLUDE_FROM_ALL)

add_library(shine_png STATIC
  ${THIRD}/libpng/png.c
  ${THIRD}/libpng/pngerror.c
  ${THIRD}/libpng/pngget.c
  ${THIRD}/libpng/pngmem.c
  ${THIRD}/libpng/pngpread.c
  ${THIRD}/libpng/pngread.c
  ${THIRD}/libpng/pngrio.c
  ${THIRD}/libpng/pngrtran.c
  ${THIRD}/libpng/pngrutil.c
  ${THIRD}/libpng/pngset.c
  ${THIRD}/libpng/pngtrans.c
  ${THIRD}/libpng/pngwio.c
  ${THIRD}/libpng/pngwrite.c
  ${THIRD}/libpng/pngwtran.c
  ${THIRD}/libpng/pngwutil.c
)
target_include_directories(shine_png PUBLIC ${THIRD}/libpng ${THIRD}/zlib)
target_link_libraries(shine_png PUBLIC zlibstatic)
# MinGW 下没有 NASM：关掉需要汇编的 SIMD 路径，纯 C 编译
target_compile_definitions(shine_png PRIVATE PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0)

# --- libjpeg-turbo（G-S12a）：独立源码编入（上游 CMake 禁止 add_subdirectory）---
# jconfig.h / jconfigint.h / jversion.h 已 vendored（MinGW、WITH_SIMD=0、无 NASM）
set(SHINE_JPEG_DIR ${THIRD}/libjpeg-turbo)
add_library(shine_jpeg STATIC
  ${SHINE_JPEG_DIR}/jaricom.c
  ${SHINE_JPEG_DIR}/jcapimin.c
  ${SHINE_JPEG_DIR}/jcapistd.c
  ${SHINE_JPEG_DIR}/jcarith.c
  ${SHINE_JPEG_DIR}/jccoefct.c
  ${SHINE_JPEG_DIR}/jccolor.c
  ${SHINE_JPEG_DIR}/jcdctmgr.c
  ${SHINE_JPEG_DIR}/jcdiffct.c
  ${SHINE_JPEG_DIR}/jchuff.c
  ${SHINE_JPEG_DIR}/jcicc.c
  ${SHINE_JPEG_DIR}/jcinit.c
  ${SHINE_JPEG_DIR}/jclhuff.c
  ${SHINE_JPEG_DIR}/jclossls.c
  ${SHINE_JPEG_DIR}/jcmainct.c
  ${SHINE_JPEG_DIR}/jcmarker.c
  ${SHINE_JPEG_DIR}/jcmaster.c
  ${SHINE_JPEG_DIR}/jcomapi.c
  ${SHINE_JPEG_DIR}/jcparam.c
  ${SHINE_JPEG_DIR}/jcphuff.c
  ${SHINE_JPEG_DIR}/jcprepct.c
  ${SHINE_JPEG_DIR}/jcsample.c
  ${SHINE_JPEG_DIR}/jctrans.c
  ${SHINE_JPEG_DIR}/jdapimin.c
  ${SHINE_JPEG_DIR}/jdapistd.c
  ${SHINE_JPEG_DIR}/jdarith.c
  ${SHINE_JPEG_DIR}/jdatadst.c
  ${SHINE_JPEG_DIR}/jdatasrc.c
  ${SHINE_JPEG_DIR}/jdcoefct.c
  ${SHINE_JPEG_DIR}/jdcolor.c
  ${SHINE_JPEG_DIR}/jddctmgr.c
  ${SHINE_JPEG_DIR}/jddiffct.c
  ${SHINE_JPEG_DIR}/jdhuff.c
  ${SHINE_JPEG_DIR}/jdicc.c
  ${SHINE_JPEG_DIR}/jdinput.c
  ${SHINE_JPEG_DIR}/jdlhuff.c
  ${SHINE_JPEG_DIR}/jdlossls.c
  ${SHINE_JPEG_DIR}/jdmainct.c
  ${SHINE_JPEG_DIR}/jdmarker.c
  ${SHINE_JPEG_DIR}/jdmaster.c
  ${SHINE_JPEG_DIR}/jdmerge.c
  ${SHINE_JPEG_DIR}/jdphuff.c
  ${SHINE_JPEG_DIR}/jdpostct.c
  ${SHINE_JPEG_DIR}/jdsample.c
  ${SHINE_JPEG_DIR}/jdtrans.c
  ${SHINE_JPEG_DIR}/jerror.c
  ${SHINE_JPEG_DIR}/jfdctflt.c
  ${SHINE_JPEG_DIR}/jfdctfst.c
  ${SHINE_JPEG_DIR}/jfdctint.c
  ${SHINE_JPEG_DIR}/jidctflt.c
  ${SHINE_JPEG_DIR}/jidctfst.c
  ${SHINE_JPEG_DIR}/jidctint.c
  ${SHINE_JPEG_DIR}/jidctred.c
  ${SHINE_JPEG_DIR}/jmemmgr.c
  ${SHINE_JPEG_DIR}/jmemnobs.c
  ${SHINE_JPEG_DIR}/jpeg_nbits.c
  ${SHINE_JPEG_DIR}/jquant1.c
  ${SHINE_JPEG_DIR}/jquant2.c
  ${SHINE_JPEG_DIR}/jutils.c
)
target_include_directories(shine_jpeg PUBLIC ${SHINE_JPEG_DIR})
target_compile_definitions(shine_jpeg PRIVATE WITH_SIMD=0)

# 12/16-bit 精度变体（libjpeg-turbo 3.x 多精度：j12*/j16* 符号）
set(SHINE_JPEG12_SRC
  ${SHINE_JPEG_DIR}/jcapistd.c ${SHINE_JPEG_DIR}/jccolor.c ${SHINE_JPEG_DIR}/jcdiffct.c
  ${SHINE_JPEG_DIR}/jclossls.c ${SHINE_JPEG_DIR}/jcmainct.c ${SHINE_JPEG_DIR}/jcprepct.c
  ${SHINE_JPEG_DIR}/jcsample.c ${SHINE_JPEG_DIR}/jdapistd.c ${SHINE_JPEG_DIR}/jdcolor.c
  ${SHINE_JPEG_DIR}/jddiffct.c ${SHINE_JPEG_DIR}/jdlossls.c ${SHINE_JPEG_DIR}/jdmainct.c
  ${SHINE_JPEG_DIR}/jdpostct.c ${SHINE_JPEG_DIR}/jdsample.c ${SHINE_JPEG_DIR}/jutils.c
  ${SHINE_JPEG_DIR}/jccoefct.c ${SHINE_JPEG_DIR}/jcdctmgr.c ${SHINE_JPEG_DIR}/jdcoefct.c
  ${SHINE_JPEG_DIR}/jddctmgr.c ${SHINE_JPEG_DIR}/jdmerge.c ${SHINE_JPEG_DIR}/jfdctfst.c
  ${SHINE_JPEG_DIR}/jfdctint.c ${SHINE_JPEG_DIR}/jidctflt.c ${SHINE_JPEG_DIR}/jidctfst.c
  ${SHINE_JPEG_DIR}/jidctint.c ${SHINE_JPEG_DIR}/jidctred.c ${SHINE_JPEG_DIR}/jquant1.c
  ${SHINE_JPEG_DIR}/jquant2.c
)
set(SHINE_JPEG16_SRC
  ${SHINE_JPEG_DIR}/jcapistd.c ${SHINE_JPEG_DIR}/jccolor.c ${SHINE_JPEG_DIR}/jcdiffct.c
  ${SHINE_JPEG_DIR}/jclossls.c ${SHINE_JPEG_DIR}/jcmainct.c ${SHINE_JPEG_DIR}/jcprepct.c
  ${SHINE_JPEG_DIR}/jcsample.c ${SHINE_JPEG_DIR}/jdapistd.c ${SHINE_JPEG_DIR}/jdcolor.c
  ${SHINE_JPEG_DIR}/jddiffct.c ${SHINE_JPEG_DIR}/jdlossls.c ${SHINE_JPEG_DIR}/jdmainct.c
  ${SHINE_JPEG_DIR}/jdpostct.c ${SHINE_JPEG_DIR}/jdsample.c ${SHINE_JPEG_DIR}/jutils.c
)
add_library(shine_jpeg12 OBJECT ${SHINE_JPEG12_SRC})
target_include_directories(shine_jpeg12 PUBLIC ${SHINE_JPEG_DIR})
target_compile_definitions(shine_jpeg12 PRIVATE WITH_SIMD=0 BITS_IN_JSAMPLE=12)
add_library(shine_jpeg16 OBJECT ${SHINE_JPEG16_SRC})
target_include_directories(shine_jpeg16 PUBLIC ${SHINE_JPEG_DIR})
target_compile_definitions(shine_jpeg16 PRIVATE WITH_SIMD=0 BITS_IN_JSAMPLE=16)
target_link_libraries(shine_jpeg PUBLIC shine_jpeg12 shine_jpeg16)

# --- libwebp（G-S12b）：只编 decode 静态库，工具/示例全关 ---
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
set(WEBP_ENABLE_SIMD OFF CACHE BOOL "" FORCE)
set(WEBP_NEAR_LOSSLESS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD}/libwebp EXCLUDE_FROM_ALL)

# --- hiredis：Redis 客户端（src/db 使用；不 add_subdirectory，避免 tests/install）---
add_library(shine_hiredis STATIC
  ${THIRD}/hiredis/hiredis.c
  ${THIRD}/hiredis/net.c
  ${THIRD}/hiredis/read.c
  ${THIRD}/hiredis/sds.c
  ${THIRD}/hiredis/alloc.c
  ${THIRD}/hiredis/dict.c
  ${THIRD}/hiredis/async.c
  ${THIRD}/hiredis/sockcompat.c
)
target_include_directories(shine_hiredis PUBLIC ${THIRD}/hiredis)
if(WIN32 OR MINGW)
  target_compile_definitions(shine_hiredis PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(shine_hiredis PUBLIC ws2_32 crypt32)
endif()

# --- sqlite：官方 amalgamation（third/sqlite）---
add_library(shine_sqlite STATIC ${THIRD}/sqlite/sqlite3.c)
target_include_directories(shine_sqlite PUBLIC ${THIRD}/sqlite)
target_compile_definitions(shine_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_ENABLE_FTS5
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_LOAD_EXTENSION
)
if(WIN32 OR MINGW)
  target_link_libraries(shine_sqlite PUBLIC ws2_32)
endif()

# ============================================================================
# shine_core —— 业务层静态库（零 Qt include；Qt 套件与前端共用）
#
# P01-S3 三目标重组：模块按 API.md 归位表落位
#   （graph→flow、openai→llm、gallery 并入 media、video→visual 与 flow、
#     agent 拆 novel/llm、app 非可视编排→novel、theme→shine_kit）。
# 分层方向只准向下：util ← core ← net/db ← llm/comfy/media ← flow/visual/novel/paint/mcp ← project ← pipeline
# ============================================================================
add_library(shine_core STATIC
  # core / util（util 为纯头文件库，无源文件登记）
  src/core/Settings.cpp
  src/core/Log.cpp
  src/core/Async.cpp

  # net：libhv 就绪 + 出站 HTTP（comfy/llm 共用）
  src/net/LibhvReady.cpp
  src/net/HttpClient.cpp

  # db：sqlite + redis
  src/db/Db.cpp
  src/db/redis/Redis.cpp
  src/db/redis/RedisValue.cpp
  src/db/redis/RedisClient.cpp
  src/db/redis/RedisPool.cpp
  src/db/redis/RedisAsync.cpp
  src/db/sqlite/SqliteDb.cpp
  src/db/sqlite/SqliteStmt.cpp

  # llm：LLM 网关（多供应商，原 openai 模块）+ Agent 工具循环（自 agent 归位）
  src/llm/OpenAIHttp.cpp
  src/llm/OpenAIClient.cpp
  src/llm/OpenAIStream.cpp
  src/llm/OpenAIProvider.cpp
  src/llm/OpenAIChat.cpp
  src/llm/OpenAIAnthropic.cpp
  src/llm/ToolRegistry.cpp
  src/llm/BuiltinTools.cpp
  src/llm/AgentKit.cpp

  # comfy：ComfyUI 桥
  src/comfy/ComfyTypes.cpp
  src/comfy/ComfyNodeDef.cpp
  src/comfy/ComfyHttp.cpp
  src/comfy/ComfyClient.cpp
  src/comfy/ComfySocket.cpp
  src/comfy/ComfySession.cpp
  src/comfy/ComfyQueueModel.cpp
  src/comfy/ComfyWorkflows.cpp

  # gpu：DX11 纹理（媒体缩略图共用）
  src/gpu/GpuDevice.cpp
  src/gpu/GpuTexture.cpp
  src/gpu/GpuTextureManager.cpp
  src/gpu/GpuTextureCache.cpp

  # media：媒体库 + 图库（原 gallery 并入：解码/缓存/扫描）
  src/media/ImageFetch.cpp
  src/media/MediaLibrary.cpp
  src/media/VideoThumb.cpp
  src/media/Image.cpp
  src/media/MetaProbe.cpp
  src/media/ImageLoader.cpp
  src/media/decoders/PngDecoder.cpp
  src/media/decoders/JpegDecoder.cpp
  src/media/decoders/WebpDecoder.cpp
  src/media/ExifOrientation.cpp
  src/media/ImageScanner.cpp
  src/media/GalleryModel.cpp
  src/media/Gallery.cpp
  src/media/ThumbnailService.cpp
  src/media/Viewer.cpp
  src/media/FileActions.cpp
  src/media/cache/CpuThumbCache.cpp
  src/media/cache/DiskThumbCache.cpp
  src/media/Resize.cpp
  src/media/GalleryLayout.cpp

  # flow：纯图模型（原 graph）+ workflow 构造（自 video 归位）
  src/flow/GraphHost.cpp
  src/flow/ComfyNode.cpp
  src/flow/GraphCompiler.cpp
  src/flow/WorkflowIO.cpp
  src/flow/H3WorkflowBuilder.cpp
  src/flow/ApiGraphValidator.cpp
  src/flow/FlowBinder.cpp
  src/flow/FlowValidator.cpp
  src/flow/BatchRender.cpp
  src/flow/ImageReview.cpp
  src/flow/VideoCatalog.cpp
  src/flow/VideoChain.cpp

  # visual：视觉生成模型与桥（自 video 归位）
  src/visual/VideoProject.cpp
  src/visual/CharacterAsset.cpp
  src/visual/MentionResolver.cpp
  src/visual/VideoTaskRunner.cpp
  src/visual/SceneToImageBuilder.cpp
  src/visual/NovelShotBridge.cpp
  src/visual/ReferenceLibrary.cpp
  src/visual/ReferenceChain.cpp

  # novel：小说生产（含 Director/Stage 自 agent 归位；NovelCli/NovelPipeline 自 app 归位）
  src/novel/NovelProjects.cpp
  src/novel/NovelDb.cpp
  src/novel/NovelGraph.cpp
  src/novel/NovelMemory.cpp
  src/novel/NovelVisual.cpp
  src/novel/NovelFields.cpp
  src/novel/NovelMcpTools.cpp
  src/novel/NovelImageGen.cpp
  src/novel/NovelImageStore.cpp
  src/novel/NovelAssetPipeline.cpp
  src/novel/NovelCommit.cpp
  src/novel/NovelRunLoop.cpp
  src/novel/NovelChecks.cpp
  src/novel/NovelStageLedger.cpp
  src/novel/NovelInit.cpp
  src/novel/NovelContinuity.cpp
  src/novel/NovelGeneration.cpp
  src/novel/NovelPromptGen.cpp
  src/novel/ContextBuilder.cpp
  src/novel/NovelDirector.cpp
  src/novel/NovelStoryboard.cpp
  src/novel/NovelVisualStages.cpp
  src/novel/NovelCli.cpp
  src/novel/NovelPipeline.cpp

  # paint
  src/paint/PaintCanvas.cpp
  src/paint/PngCodec.cpp
  src/paint/PaintService.cpp

  # mcp：工具注册 / 传输 / 协议
  src/mcp/Schema.cpp
  src/mcp/ToolRegistry.cpp
  src/mcp/McpBootstrap.cpp
  src/mcp/HttpServer.cpp
  src/mcp/MCPServer.cpp
  src/mcp/McpSse.cpp
  src/mcp/McpHttpClient.cpp
  src/mcp/McpRemoteAgent.cpp
  src/mcp/BuiltinTools.cpp

  # project：项目模型 / 索引 / 模板（P03-S1..S3；只依赖 core/util/db）
  src/project/Project.cpp
  src/pipeline/StageMachine.cpp
  src/pipeline/Checkpoint.cpp
  src/pipeline/Budget.cpp
  src/pipeline/StopPolicy.cpp
  src/pipeline/Ledger.cpp
  src/pipeline/Runner.cpp
  src/project/ProjectIndex.cpp
  src/project/ProjectTemplate.cpp

  # pipeline：阶段/预算/账本/检查点（已登记于上方 StageMachine/Runner 等）

  # mimalloc amalgamation
  ${THIRD}/mimalloc/src/static.c

  # yyjson
  ${THIRD}/yyjson/yyjson.c
)

target_include_directories(shine_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${THIRD}/yyjson
  ${THIRD}/zmij
  ${THIRD}/fmt
  ${THIRD}/libpng
  ${THIRD}/imageinfo
  ${THIRD}/libjpeg-turbo
  ${THIRD}/libwebp/src
  ${THIRD}
  ${THIRD}/function2
  ${THIRD}/mimalloc/include
  ${THIRD}/stdexec/include
  ${THIRD}/libhv
  ${THIRD}/libhv/include
  ${THIRD}/libhv/include/hv
  ${THIRD}/libhv/base
  ${THIRD}/libhv/event
  ${THIRD}/libhv/ssl
  ${THIRD}/libhv/util
  ${THIRD}/libhv/cpputil
  ${THIRD}/libhv/evpp
  ${THIRD}/libhv/http
  ${THIRD}/libhv/http/client
)

target_compile_definitions(shine_core PUBLIC
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  FMT_HEADER_ONLY
  SPDLOG_FMT_EXTERNAL
  HV_STATICLIB
  MI_MALLOC_OVERRIDE=0
  SHINE_VERSION="0.2.0"
)

if(MINGW)
  target_compile_options(shine_core PRIVATE -O2 -Wno-unknown-pragmas)

  # C++26 静态反射（P2996）：GCC 16 必须显式开启，否则 <meta>/^^T 不可用。
  # 只对 C++ 源文件生效（C 源文件如 mimalloc/yyjson 不需要）。
  # 静态反射写法与边界见 docs/30-engineering/coding-rules.md。
  # PUBLIC：Qt 前端（src/ui/app）同样需要反射（设置/主题序列化）。
  target_compile_options(shine_core PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
endif()

target_link_libraries(shine_core PUBLIC
  hv_static
  shine_png
  shine_jpeg
  webp
  shine_hiredis
  shine_sqlite
  d3d11
  dxgi
  d3dcompiler
  dwmapi
  user32
  gdi32
  shell32
  ole32
  shlwapi  # P5.6：AssocQueryStringW（自检里查"播放"的系统关联程序）
  uuid
  secur32
  crypt32
  winmm
  iphlpapi
  ws2_32
)

# ============================================================================
# shine_kit —— UI 套件（Theme/Qss 与控件；只被前端使用，允许依赖 Qt）
#
# P01-S3：只收编 theme（Token/QssBuilder 骨架见 S5，控件实现 P02）。
# 方向：shine_core ← shine_kit ← ShineTVStudio；kit 不许 include 业务类型头。
# ============================================================================
add_library(shine_kit STATIC
  src/ui/kit/theme/Theme.cpp
  src/ui/kit/theme/ThemeService.cpp
  src/ui/kit/theme/QssBuilder.cpp
  src/ui/kit/motion/Easing.cpp
  src/ui/kit/motion/Tween.cpp
  src/ui/kit/motion/Transition.cpp
  src/ui/kit/motion/MotionScope.cpp
  src/ui/kit/controls/WidgetCommon.cpp
  src/ui/kit/controls/Controls.cpp
  src/ui/kit/controls/Inputs.cpp
  src/ui/kit/controls/Surfaces.cpp
  src/ui/kit/controls/Feedback.cpp
  src/ui/kit/controls/Navigation.cpp
  src/ui/kit/canvas/FlowCanvas.cpp
  src/ui/kit/data/Table.cpp
  src/ui/kit/data/Flow.cpp
  src/ui/kit/data/Panels.cpp
  src/ui/kit/images/Grid.cpp
  src/ui/kit/images/Viewer.cpp
  src/ui/kit/images/Sheet.cpp
)

target_link_libraries(shine_kit PUBLIC shine_core Qt6::Widgets)

# ============================================================================
# shine_qml —— QML 接缝层（2026-09-29 架构层）
#
# 职责边界（刻意做得很窄，避免长成第二套 UI 框架）：
#   * QuickHost：QQuickWidget 宿主 + 场景图抓图（harness 出图要走它）
#   * ThemeBridge：把既有 ColorToken 投影给 QML，**零新增 token 真值**
#   * 混色算法不在这里 —— 唯一实现在 kit/theme/ToneMix.h，与 QSS 共用
#
# 依赖方向：shine_kit ← shine_qml。shine_kit 保持纯 Widgets、不引任何 QML 头，
# 这样既有 43K 行 Widgets 代码一行不用改，QML 只在新页面里增量引入。
# ============================================================================
add_library(shine_qml STATIC
  src/ui/kit/qml/ThemeBridge.cpp
  src/ui/kit/qml/QuickHost.cpp
)
target_link_libraries(shine_qml PUBLIC shine_kit Qt6::QuickWidgets Qt6::Qml Qt6::QuickEffects)
# 头文件（ThemeBridge.h 的 QML_ELEMENT 宏）需要 moc 看到
target_include_directories(shine_qml PUBLIC ${CMAKE_SOURCE_DIR}/src)

# QML 页面资源：编译进 exe，避免运行时依赖磁盘上的 .qml（打包/评审都省事）
#
# ⚠️ 用 GLOB + CONFIGURE_DEPENDS，不逐个列 FILES：页面是并行子 Agent 各自新建的，
# 写死列表会让「某页还没提交」直接把**整个配置**卡成硬错误 —— 一个页面文件缺失
# 就编不出主程序。CONFIGURE_DEPENDS 让 CMake 在每次构建时重新扫，新增文件自动入包。
file(GLOB SHINE_QML_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
if(NOT SHINE_QML_FILES)
  message(FATAL_ERROR "src/ui/qml 下没有任何 .qml —— QML 接缝层被误删？")
endif()
qt_add_resources(shine_qml "shin_qml"
  PREFIX "/qt/qml"
  BASE   "${CMAKE_SOURCE_DIR}/src/ui/qml"
  FILES  ${SHINE_QML_FILES}
)

# ============================================================================
# ShineTVStudio —— Qt Widgets 前端（P01-S6 空壳主窗口；P03 起为项目中心外壳）
# ============================================================================
add_executable(ShineTVStudio
  src/ui/app/main.cpp
  src/ui/kit/qml/QmlPageReview.cpp
  src/ui/app/AppEntry.h
  src/ui/app/AppEntry.cpp
  src/ui/app/AppEnvironment.h
  src/ui/app/AppEnvironment.cpp
  src/ui/app/StartupChecks.h
  src/ui/app/StartupChecks.cpp
  src/ui/app/AcceptanceChecks.h
  src/ui/app/AcceptanceChecks.cpp
  src/ui/verify/checks/P02Checks.h
  src/ui/verify/checks/P02Checks.cpp
  src/ui/verify/checks/P03Checks.h
  src/ui/verify/checks/P03Checks.cpp
  src/ui/verify/checks/P04WorldChecks.h
  src/ui/verify/checks/P09Checks.h
  src/ui/verify/checks/P09Checks.cpp
  src/ui/verify/review/P09Review.h
  src/ui/verify/review/P09Review.cpp
  src/ui/verify/checks/P10Checks.h
  src/ui/verify/checks/P10Checks.cpp
  src/ui/verify/review/P10Review.h
  src/ui/verify/review/P10Review.cpp
  src/ui/verify/checks/P04WorldChecks.cpp
  src/ui/verify/checks/P04InitChainChecks.h
  src/ui/verify/checks/P04InitChainChecks.cpp
  src/ui/verify/checks/P04ChapterChecks.h
  src/ui/verify/checks/P08Checks.h
  src/ui/verify/checks/P08Checks.cpp
  src/ui/verify/review/P08Review.h
  src/ui/verify/review/P08Review.cpp
  src/ui/verify/checks/P07Checks.h
  src/ui/verify/checks/P07Checks.cpp
  src/ui/verify/review/P07Review.h
  src/ui/verify/review/P07Review.cpp
  src/ui/verify/checks/P04ChapterChecks.cpp
  src/ui/verify/checks/P04ReviewChecks.h
  src/ui/verify/checks/P04ReviewChecks.cpp
  src/ui/verify/checks/P05Checks.h
  src/ui/verify/checks/P05Checks.cpp
  src/ui/pages/shell/MainWindow.h
  src/ui/pages/shell/MainWindow.cpp
  src/ui/pages/shell/TopBar.h
  src/ui/pages/shell/TopBar.cpp
  src/ui/pages/shell/ActivityRail.h
  src/ui/pages/shell/ActivityRail.cpp
  src/ui/pages/shell/RightPanel.h
  src/ui/pages/shell/RightPanel.cpp
  src/ui/pages/shell/SidePanel.h
  src/ui/pages/shell/SidePanel.cpp
  src/ui/pages/shell/BottomDock.h
  src/ui/pages/shell/BottomDock.cpp
  src/ui/pages/shell/StatusBar.h
  src/ui/pages/shell/StatusBar.cpp
  src/ui/pages/shell/Breadcrumb.h
  src/ui/pages/shell/Breadcrumb.cpp
  src/ui/pages/shell/CommandPalette.h
  src/ui/pages/shell/CommandPalette.cpp
  src/ui/pages/settings/FirstRunWizard.h
  src/ui/pages/settings/FirstRunWizard.cpp
  src/ui/pages/project/ProjectHubView.h
  src/ui/pages/project/ProjectHubView.cpp
  src/ui/pages/project/ProjectWizardDialog.h
  src/ui/pages/project/ProjectWizardDialog.cpp
  src/ui/verify/review/P03Review.h
  src/ui/verify/review/P04Review.h
  src/ui/verify/review/P04Review.cpp
  src/ui/pages/imageflow/ComfyPanel.h
  src/ui/pages/imageflow/ComfyPanel.cpp
  src/ui/pages/imageflow/BindingView.h
  src/ui/pages/imageflow/BindingView.cpp
  src/ui/pages/pipeline/GanttView.h
  src/ui/pages/pipeline/GanttView.cpp
  src/ui/pages/pipeline/LedgerView.h
  src/ui/pages/pipeline/LedgerView.cpp
  src/ui/pages/pipeline/StopReportView.h
  src/ui/pages/pipeline/StopReportView.cpp
  src/ui/pages/pipeline/PipelineWorkspace.h
  src/ui/pages/pipeline/PipelineWorkspace.cpp
  src/ui/pages/videoflow/ChainView.h
  src/ui/pages/videoflow/ChainView.cpp
  src/ui/pages/videoflow/VideoTaskView.h
  src/ui/pages/videoflow/VideoTaskView.cpp
  src/ui/pages/videoflow/FilmStrip.h
  src/ui/pages/videoflow/FilmStrip.cpp
  src/ui/pages/videoflow/FinalCutView.h
  src/ui/pages/videoflow/FinalCutView.cpp
  src/ui/pages/videoflow/VideoFlowWorkspace.h
  src/ui/pages/videoflow/VideoFlowWorkspace.cpp
  src/ui/pages/imageflow/BatchRenderView.h
  src/ui/pages/imageflow/BatchRenderView.cpp
  src/ui/pages/imageflow/ImageReviewView.h
  src/ui/pages/imageflow/ImageReviewView.cpp
  src/ui/pages/imageflow/RenderResultView.h
  src/ui/pages/imageflow/RenderResultView.cpp
  src/ui/pages/imageflow/ImageFlowWorkspace.h
  src/ui/pages/imageflow/ImageFlowWorkspace.cpp
  src/ui/verify/review/P05Review.h
  src/ui/verify/review/P05Review.cpp
  src/ui/verify/review/P06Review.h
  src/ui/verify/review/P06Review.cpp
  src/ui/verify/review/P03Review.cpp
  src/ui/pages/assets/AssetWorkspace.h
  src/ui/pages/assets/AssetWorkspace.cpp
  src/ui/pages/assets/AssetDetailView.h
  src/ui/pages/assets/AssetDetailView.cpp
  src/ui/pages/assets/AssetPolicyPanel.h
  src/ui/pages/assets/AssetPolicyPanel.cpp
  src/ui/pages/assets/AssetPolicy.h
  src/ui/pages/assets/AssetPageModel.h
  src/ui/pages/assets/AssetPageModel.cpp
  src/ui/pages/assets/QmlAssetsPage.h
  src/ui/pages/assets/QmlAssetsPage.cpp
  src/ui/pages/assets/ConsistencyView.h
  src/ui/pages/assets/ConsistencyView.cpp
  src/ui/pages/storyboard/StoryboardWorkspace.h
  src/ui/pages/storyboard/StoryboardWorkspace.cpp
  src/ui/pages/storyboard/StoryboardTimeline.h
  src/ui/pages/storyboard/StoryboardTimeline.cpp
  src/ui/pages/storyboard/ShotTableView.h
  src/ui/pages/storyboard/ShotTableView.cpp
  src/ui/pages/storyboard/ShotDetailView.h
  src/ui/pages/storyboard/ShotDetailView.cpp
  src/ui/pages/storyboard/ContinuityView.h
  src/ui/pages/storyboard/ContinuityView.cpp
  src/ui/pages/storyboard/GenShotBridgeView.h
  src/ui/pages/storyboard/GenShotBridgeView.cpp
  src/ui/pages/assets/RefLibraryView.h
  src/ui/pages/assets/RefLibraryView.cpp
  src/ui/pages/novel/NovelWorkspace.h
  src/ui/pages/novel/NovelWorkspace.cpp
  src/ui/pages/novel/WorldBoardView.h
  src/ui/pages/novel/WorldBoardView.cpp
  src/ui/pages/novel/WorldBoardS3.cpp
  src/ui/pages/novel/InitChainView.h
  src/ui/pages/novel/InitChainView.cpp
  src/ui/pages/novel/ChapterFlowView.h
  src/ui/pages/novel/ChapterFlowView.cpp
  src/ui/pages/novel/DraftView.h
  src/ui/pages/novel/DraftView.cpp
  src/ui/pages/novel/ReviewView.h
  src/ui/pages/novel/ReviewView.cpp
  src/ui/pages/novel/ModelPromptView.h
  src/ui/pages/novel/ModelPromptView.cpp
  src/ui/pages/novel/StateDiffView.h
  src/ui/pages/novel/StateDiffView.cpp
  src/ui/pages/novel/AutoRunPanel.h
  src/ui/pages/novel/AutoRunPanel.cpp
  src/ui/verify/gallery/WidgetGalleryView.h
  src/ui/verify/gallery/WidgetGalleryView.cpp
  src/ui/verify/gallery/GalleryWorkspace.h
  src/ui/verify/checks/P06Checks.h
  src/ui/verify/checks/P06Checks.cpp
  src/ui/verify/gallery/GalleryWorkspace.cpp
  src/ui/verify/gallery/QssStateProbe.h
  src/ui/verify/gallery/QssStateProbe.cpp
  src/ui/verify/gallery/Bench.h
  src/ui/verify/gallery/Bench.cpp
  src/ui/pages/settings/StyleEditorDialog.h
  src/ui/pages/settings/StyleEditorDialog.cpp
)

target_compile_options(ShineTVStudio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-freflection>)

target_link_libraries(ShineTVStudio PRIVATE
  shine_core
  shine_kit
  shine_qml
  Qt6::Widgets
)

# 主题 JSON 随程序分发（P02-S1）：拷到 <exe目录>/themes
add_custom_command(TARGET ShineTVStudio POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/src/ui/kit/theme/Themes"
          "$<TARGET_FILE_DIR:ShineTVStudio>/themes")
