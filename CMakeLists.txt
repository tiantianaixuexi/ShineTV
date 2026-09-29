cmake_minimum_required(VERSION 3.20)
project(ShineTVStudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(THIRD ${CMAKE_SOURCE_DIR}/third)

# --- 前端（Dear ImGui，refactor/phases.md P0.2 引入、P7 定型）---
#
# P7 删掉了 SHINE_UI_QT 与整棵 Qt 树（src/ui/{kit,qml,pages,layout,verify,app}），
# 现在**只有一个前端**：src/ui/imgui/**。不再有 find_package(Qt6) / AUTOMOC / AUTORCC，
# 配过与构建全程零 Qt 依赖 —— 这就是 P7 的验收判据。
option(SHINE_UI_IMGUI "ImGui 前端" ON)
if(NOT SHINE_UI_IMGUI)
  message(FATAL_ERROR "SHINE_UI_IMGUI=OFF 无处可去：Qt 前端已在 P7 删除，没有第二个前端可切")
endif()

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
  # 图形：src/gpu 的纹理上传用 GL 1.1 的 6 个函数，opengl32.dll 直接导出。
  # （原为 d3d11/dxgi/d3dcompiler —— 渲染后端改走 GL3 后已无 DX11 调用点。）
  opengl32
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
# ImGui 前端 —— third/imgui + src/ui/imgui/**
#
# 目标分层（refactor/architecture.md §3）：
#   shine_core（零 Qt 零 ImGui，完全不动）
#        ▲
#   shine_imgui（静态库：theme / font / kit / host / pages / verify）
#        ▲
#   ShineTVStudio（exe：只有一个 main.cpp）
#
# ⚠️ ImGui 是静态编译进 exe 的：d3d11/dxgi/d3dcompiler/dwmapi/user32/gdi32
#    全是系统库，不需要像 Qt 那样部署运行库（392MB → 十几 MB 量级，P7.7 实测）。
# ============================================================================
if(SHINE_UI_IMGUI)
  if(NOT EXISTS "${THIRD}/imgui/imgui.h")
    message(FATAL_ERROR
      "third/imgui 缺失。恢复方式：git checkout aa61046 -- third/imgui")
  endif()

  # 只编 8 个 .cpp：核心 5（含 demo 取证用）+ win32/dx11 后端 + stdlib。
  # imgui_demo 保留是因为 P3.7 组件画廊与 P6 取证要 ShowDemoWindow 做对照。
  add_library(shine_imgui_third STATIC
    ${THIRD}/imgui/imgui.cpp
    ${THIRD}/imgui/imgui_draw.cpp
    ${THIRD}/imgui/imgui_tables.cpp
    ${THIRD}/imgui/imgui_widgets.cpp
    ${THIRD}/imgui/imgui_demo.cpp
    ${THIRD}/imgui/backends/imgui_impl_win32.cpp
    ${THIRD}/imgui/backends/imgui_impl_opengl3.cpp
    ${THIRD}/imgui/misc/cpp/imgui_stdlib.cpp
  )
  target_include_directories(shine_imgui_third PUBLIC
    ${THIRD}/imgui ${THIRD}/imgui/backends)
  # IMGUI_DEFINE_MATH_OPERATORS：全套件用 IM_COL32/RGBA 宏，缺它会编译不过。
  # UNICODE/NOMINMAX/WIN32_LEAN_AND_MEAN 与 shine_core 保持一致，
  # 否则同一份 Win32 头在两侧宏不一致 → 结构体布局错、链接重复符号。
  target_compile_definitions(shine_imgui_third PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
  # opengl32 而非 d3d11/dxgi：渲染后端是 GL3（自带 imgl3w 加载器，GL 着色器由它编译），
  # src/gpu 的纹理上传只用 GL 1.1，那 6 个函数由 opengl32.dll 直接导出。
  # dwmapi 仍要留：imgui_impl_win32 的 DWM 合成（EnableAlphaCompositing）链到它。
  target_link_libraries(shine_imgui_third PUBLIC opengl32 user32 gdi32 dwmapi)

  # 前端源文件用 GLOB + CONFIGURE_DEPENDS：组件与页面是逐步长出来的，
  # 写死列表会让「还没建的页面」把整个配置卡成硬错误。
  # 只有一个例外：app/main.cpp 属于 exe，不进静态库。
  file(GLOB_RECURSE SHINE_IMGUI_LIB_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/ui/imgui/*.cpp")
  list(FILTER SHINE_IMGUI_LIB_SRC EXCLUDE REGEX "/src/ui/imgui/app/")
  if(NOT SHINE_IMGUI_LIB_SRC)
    message(FATAL_ERROR "src/ui/imgui 下没有任何 .cpp —— ImGui 前端被误删？")
  endif()

  add_library(shine_imgui STATIC ${SHINE_IMGUI_LIB_SRC})
  target_include_directories(shine_imgui PUBLIC ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(shine_imgui PUBLIC shine_core shine_imgui_third)

  # AppEnvironment.{h,cpp} 原在 src/ui/app/（与 Qt 前端同目录）。P7 删掉那棵树时
  # 把它搬到了 src/ui/imgui/host/ —— 它是零 Qt 的宽字符环境变量读取器，ImGui 侧
  # 要用；现在它已被上面的 GLOB 收进 shine_imgui，**不要**再在下面重复列出。
  add_executable(ShineTVStudio
    src/ui/imgui/app/main.cpp)
  target_include_directories(ShineTVStudio PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(ShineTVStudio PRIVATE shine_imgui)

  # 主题 JSON 随程序分发：拷到 <exe目录>/themes（5 套主题，见 P2.1）。
  # 启动时由 AppEntry 的 LoadStartupTheme 从那里读（SHINE_THEME_DIR 可覆盖）。
  # ⚠️ 源目录原来是 src/ui/kit/theme/Themes —— P7 删掉 Qt 树后那条路径已不存在，
  #    不改这里就是「构建照样过，但主题全落回内置默认值」。
  add_custom_command(TARGET ShineTVStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/ui/imgui/theme/Themes"
            "$<TARGET_FILE_DIR:ShineTVStudio>/themes")
endif() # SHINE_UI_IMGUI
