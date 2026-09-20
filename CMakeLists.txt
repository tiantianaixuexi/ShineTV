cmake_minimum_required(VERSION 3.20)
project(ShineTVStudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(THIRD ${CMAKE_SOURCE_DIR}/third)
set(IMGUI_DIR ${THIRD}/imgui)
set(VNS_DIR ${THIRD}/VisualNodeSystem)

# --- libhv: HTTP + WebSocket client (static only, no server/examples) ---
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(WITH_HTTP_SERVER ON CACHE BOOL "" FORCE)
# S64：**libhv 的 TLS 后端改用 OpenSSL**（原为 OFF ⇒ Windows 上落到 wintls/Schannel）。
# 为什么换（有对照实证，别再往"网络"上归因）：真跑时出现过**成串**的
# `ssl handshake failed: -1 [HttpClient.cpp:270]`（一轮 PLAN 5/5 全失败、横跨 30 秒；下一轮 0 次），
# 而**同期** `curl`（OpenSSL）与 .NET `SslStream`（Schannel）**都正常** ⇒ 网络、对端、机器 Schannel
# 均无责，问题在 libhv 的 wintls 客户端路径（其失败码 `HSSL_ERROR=-1` 无更多信息，
# 且每请求都要 `hssl_ctx_new`/释放一次凭据句柄）。
# ⚠️ 静态链接：本工程 exe 目前**只依赖系统 DLL**（自包含）。若用 msys64 的导入库
#    （`libssl.dll.a`）会引入 `libssl-3-x64.dll`/`libcrypto-3-x64.dll` 的部署依赖 ⇒ 用 `.a` 静态库。
# 依赖来源：msys64 的 mingw64 前缀（与编译器同一个 toolchain，`libssl.a`/`libcrypto.a` 齐备）。
set(WITH_OPENSSL ON CACHE BOOL "" FORCE)
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
if(NOT DEFINED OPENSSL_ROOT_DIR OR OPENSSL_ROOT_DIR STREQUAL "")
  set(OPENSSL_ROOT_DIR "$ENV{MSYSTEM_PREFIX}" CACHE PATH "OpenSSL 前缀（msys64 mingw64）")
  if(NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "C:/msys64/mingw64" CACHE PATH "OpenSSL 前缀（msys64 mingw64）" FORCE)
  endif()
endif()
set(WITH_GNUTLS OFF CACHE BOOL "" FORCE)
set(WITH_MBEDTLS OFF CACHE BOOL "" FORCE)
set(WITH_LUA OFF CACHE BOOL "" FORCE)
set(WITH_RPC OFF CACHE BOOL "" FORCE)
set(SHINE_SKIP_LIBHV_RC ON)
add_subdirectory(${THIRD}/libhv EXCLUDE_FROM_ALL)
# ⚠️ 开了 OpenSSL 之后的**静态链接冲突**：OpenSSL 3.x 的 `libcrypto.a(pathtools)` 里也有一个
# 非 static 的 `get_executable_path`，与 libhv `base/hbase.c` 的同名 ⇒
#   `ld: multiple definition of 'get_executable_path'`
# 两边都只在**各自的库内部**用它（libhv 给 `get_executable_dir`/`get_executable_file`；OpenSSL 给自己的
# 路径探测），语义也一致（填可执行文件路径）。**所以我们这份工程一处都不调它** ⇒
# 只把 libhv 那侧改个名即可，**不必改 `third/` 里的源文件**（工程规矩）。
target_compile_definitions(hv_static PRIVATE get_executable_path=shine_hv_get_executable_path)

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

add_executable(ShineTVStudio WIN32
  src/main.cpp
  src/app/App.cpp
  src/app/UiState.cpp
  # R-S0：从 App.cpp 搬出的窗口（一窗口一文件，声明见 app/AppInternal.h）
  src/app/ui/DrawConnectionStatus.cpp
  src/app/ui/Widgets.cpp
  src/app/ui/ThumbGrid.cpp
  src/theme/ThemeTokens.cpp
  src/app/shell/DrawMenuBar.cpp
  src/app/shell/DrawActivityBar.cpp
  src/app/shell/SideViewTitle.cpp
  src/app/shell/DrawSideBar.cpp
  src/app/shell/DrawDockedPanels.cpp
  src/app/shell/DrawStatusBar.cpp
  src/app/panels/assets/DrawAssetsPanel.cpp
  src/app/panels/nodes/DrawNodesPanel.cpp
  src/app/panels/workflows/DrawWorkflowsPanel.cpp
  src/app/panels/comfy/DrawComfySidePanel.cpp
  src/app/views/graph/DrawGraphPanel.cpp
  src/app/views/inspector/DrawInspectorPanel.cpp
  src/app/views/preview/DrawPreviewPanel.cpp
  src/app/views/bottom/DrawBottomPanel.cpp
  src/app/views/bottom/queue/DrawQueueTab.cpp
  src/app/views/bottom/log/DrawLogTab.cpp
  src/app/dialogs/DrawSettingsWindow.cpp
  src/app/dialogs/DrawTemplateWindow.cpp
  src/app/dialogs/DrawNodeWindow.cpp
  src/app/DockLayout.cpp
  src/app/Fonts.cpp
  src/app/FileDialog.cpp
  src/theme/Theme.cpp
  src/core/Settings.cpp
  src/core/Log.cpp
  src/core/Async.cpp

  src/comfy/ComfyTypes.cpp
  src/comfy/ComfyNodeDef.cpp
  src/comfy/ComfyHttp.cpp
  src/comfy/ComfyClient.cpp
  src/comfy/ComfySocket.cpp
  src/comfy/ComfySession.cpp
  src/comfy/ComfyQueueModel.cpp
  src/comfy/ComfyWorkflows.cpp

  src/graph/GraphHost.cpp
  src/graph/ComfyNode.cpp
  src/graph/GraphCompiler.cpp
  src/graph/WorkflowIO.cpp

  src/gpu/GpuDevice.cpp
  src/gpu/GpuTexture.cpp
  src/gpu/GpuTextureManager.cpp
  src/gpu/GpuTextureCache.cpp

  src/media/ImageFetch.cpp
  src/media/MediaLibrary.cpp
  src/media/VideoThumb.cpp
  src/app/output/OutputView.cpp

  src/video/VideoProject.cpp
  src/video/CharacterAsset.cpp
  src/video/MentionResolver.cpp
  src/video/H3WorkflowBuilder.cpp
  src/video/ApiGraphValidator.cpp
  src/video/VideoTaskRunner.cpp
  src/video/SceneToImageBuilder.cpp
  src/video/NovelShotBridge.cpp
  src/paint/PaintCanvas.cpp
  src/paint/PngCodec.cpp
  src/paint/PaintService.cpp
  src/app/paint/PaintCanvasView.cpp
  src/app/Shortcuts.cpp
  src/app/shots/ShotTableView.cpp
  src/gallery/Image.cpp
  src/gallery/MetaProbe.cpp
  src/gallery/ImageLoader.cpp
  src/gallery/decoders/PngDecoder.cpp
  src/gallery/decoders/JpegDecoder.cpp
  src/gallery/decoders/WebpDecoder.cpp
  src/gallery/ExifOrientation.cpp
  src/gallery/ImageScanner.cpp
  src/gallery/GalleryModel.cpp
  src/gallery/Gallery.cpp
  src/gallery/ThumbnailService.cpp
  src/gallery/Viewer.cpp
  src/gallery/FileActions.cpp
  src/gallery/cache/CpuThumbCache.cpp
  src/gallery/cache/DiskThumbCache.cpp
  src/gallery/Resize.cpp
  src/gallery/GalleryLayout.cpp
  src/app/gallery/FolderPicker.cpp
  src/app/gallery/GalleryView.cpp

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
  src/agent/ContextBuilder.cpp
  src/agent/ToolRegistry.cpp
  src/agent/BuiltinTools.cpp
  src/agent/NovelDirector.cpp
  src/agent/NovelStoryboard.cpp
  src/agent/NovelVisualStages.cpp
  src/agent/AgentKit.cpp
  src/app/novel/NovelCli.cpp
  src/app/novel/NovelPipeline.cpp
  src/app/novel/NovelView.cpp

  # P1：OpenAI Responses 客户端（RULES-HOOK 特例允许 libhv）
  src/openai/OpenAIHttp.cpp
  src/openai/OpenAIClient.cpp
  src/openai/OpenAIStream.cpp
  src/openai/OpenAIProvider.cpp
  src/openai/OpenAIChat.cpp
  src/openai/OpenAIAnthropic.cpp

  src/db/Db.cpp
  src/db/redis/Redis.cpp
  src/db/redis/RedisValue.cpp
  src/db/redis/RedisClient.cpp
  src/db/redis/RedisPool.cpp
  src/db/redis/RedisAsync.cpp
  src/db/sqlite/SqliteDb.cpp
  src/db/sqlite/SqliteStmt.cpp

  # P7.1：MCP 工具注册地基（分模块注册；HTTP/协议层见 P7.2/P7.3）
  src/mcp/Schema.cpp
  src/mcp/ToolRegistry.cpp
  src/mcp/McpBootstrap.cpp
  # P7.2：libhv HTTP Server（MCP 传输层骨架）
  src/mcp/HttpServer.cpp
  # P7.3/P7.4：JSON-RPC 协议 + 内置工具
  src/mcp/MCPServer.cpp
  src/mcp/McpSse.cpp
  src/mcp/McpHttpClient.cpp
  src/mcp/McpRemoteAgent.cpp
  src/mcp/BuiltinTools.cpp
  # net：libhv 就绪 + 出站 HTTP（comfy/openai 共用）
  src/net/LibhvReady.cpp
  src/net/HttpClient.cpp

  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp

  # VisualNodeSystem core (compiled into app)
  ${VNS_DIR}/VisualNodeSystem.cpp
  ${VNS_DIR}/VisualNode.cpp
  ${VNS_DIR}/VisualNodeSocket.cpp
  ${VNS_DIR}/VisualNodeCore.cpp
  ${VNS_DIR}/VisualNodeFactory.cpp
  ${VNS_DIR}/GroupComment.cpp
  ${VNS_DIR}/SubSystems/TextInputPopup.cpp
  ${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeArea.cpp
  ${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaLogic.cpp
  ${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaInput.cpp
  ${VNS_DIR}/SubSystems/VisualNodeArea/VisualNodeAreaRendering.cpp
  ${VNS_DIR}/StandardNodes/BoundaryNodes/SocketMirrorNode.cpp
  ${VNS_DIR}/StandardNodes/BoundaryNodes/LinkNode/LinkNode.cpp
  ${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaNode.cpp
  ${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaInputNode.cpp
  ${VNS_DIR}/StandardNodes/BoundaryNodes/SubAreaNode/SubAreaOutputNode.cpp
  ${VNS_DIR}/ThirdParty/jsoncpp/json_reader.cpp
  ${VNS_DIR}/ThirdParty/jsoncpp/json_value.cpp
  ${VNS_DIR}/ThirdParty/jsoncpp/json_writer.cpp

  # mimalloc amalgamation
  ${THIRD}/mimalloc/src/static.c

  # yyjson
  ${THIRD}/yyjson/yyjson.c
)

target_include_directories(ShineTVStudio PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${IMGUI_DIR}/misc/cpp
  ${VNS_DIR}
  ${VNS_DIR}/SubSystems
  ${VNS_DIR}/SubSystems/VisualNodeArea
  ${VNS_DIR}/ThirdParty
  ${VNS_DIR}/ThirdParty/glm
  ${VNS_DIR}/ThirdParty/jsoncpp
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

target_compile_definitions(ShineTVStudio PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  IMGUI_DEFINE_MATH_OPERATORS
  FMT_HEADER_ONLY
  SPDLOG_FMT_EXTERNAL
  HV_STATICLIB
  MI_MALLOC_OVERRIDE=0
  SHINE_VERSION="0.2.0"
)

if(MINGW)
  target_compile_options(ShineTVStudio PRIVATE -O2 -Wno-unknown-pragmas)

  # C++26 静态反射（P2996）：GCC 16 必须显式开启，否则 <meta>/^^T 不可用。
  # 只对 C++ 源文件生效（C 源文件如 mimalloc/yyjson 不需要）。
  # 实测用法（define_static_array + template for + splice）见 Doc/RULES-LANG.md §13.2 / §13.6。
  target_compile_options(ShineTVStudio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-freflection>)

  target_link_options(ShineTVStudio PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(ShineTVStudio PRIVATE
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
