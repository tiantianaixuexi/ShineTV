#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ShineAI 端到端流程：UE 场景 -> (颜色/深度/法线) -> ComfyUI -> AI 生成 -> 回读进 UE

它做的事情：
  1. 通过 ShineMCP 让编辑器新建一个关卡，并摆好一个简单演示场景；
  2. 从算好的机位把场景渲染成 颜色 / 深度 / 法线 三张 PNG；
  3. 把三张图上传到 ComfyUI 的 input 目录；
  4. 在编辑器里新建一张 Shine Comfy 图（ShineEditor 的图表资产），
     并用 JSON 把"场景 AI 生成"节点连好；
  5. 把这张图导出成 ComfyUI prompt 并提交；
  6. 轮询直到生成完成，下载结果，图片再导入 UE 当纹理资产。

两条互斥的生成路线（--mode）：
  image（默认）：SD1.5 图片路线 —— 颜色图 img2img + 深度/法线双 ControlNet
  video        ：LTX-2 视频路线 —— 用场景颜色图做首帧，生成一段视频（含音频）

依赖：只需要 Python 3 标准库 + 一个正在运行的 UE 编辑器（ShineMCP 插件已自动开启服务）。

用法：
    python ShineSceneToImage.py
    python ShineSceneToImage.py --mode video --frames 49
    python ShineSceneToImage.py --mode video --prompt "camera slowly pushes in, dust drifts" --seed 777
"""

import argparse
import json
import math
import os
import re
import sys
import time
import urllib.error
import urllib.request

DEFAULT_MCP_URL = os.environ.get("SHINE_MCP_URL", "http://127.0.0.1:8931")
DEFAULT_COMFY_URL = os.environ.get("SHINE_COMFY_URL", "http://127.0.0.1:8188")

LEVEL_PATH = "/Game/ShineAI/Maps/L_ShineDemo"
GRAPH_PACKAGE_PATH = "/Game/ShineAI/Comfy"
GRAPH_ASSET_NAME = "SA_SceneToImage"
GRAPH_ASSET_PATH = f"{GRAPH_PACKAGE_PATH}/{GRAPH_ASSET_NAME}"
TEXTURE_PACKAGE_PATH = "/Game/ShineAI/Comfy/Output"

DEFAULT_LTX_PROMPT = ("A cinematic shot of a futuristic desert outpost at dusk, modular white "
                      "structures on a wide sand plain, distant mountain ridges, soft blue sky, "
                      "warm low sunlight, gentle camera push in, ultra detailed, photorealistic")
DEFAULT_LTX_NEGATIVE = ("blurry, low quality, still frame, frames, watermark, overlay, titles, "
                        "has blurbox, has subtitles, text, logo")

# ComfyUI 里必须是数字的输入键：导演台模板把参数当占位符插进去时会变成字符串，
# 提交前统一转回数字，避免不同 ComfyUI 版本对字符串→数字的宽容度不一致。
NUMERIC_KEYS = {
    "width", "height", "steps", "seed", "cfg", "denoise", "strength", "strength_model",
    "start_percent", "end_percent", "batch_size", "frame_count", "fps", "frame_rate",
    "scale_by", "longer_edge", "img_compression", "frames_number",
}


def log(message):
    print(f"[ShineFlow] {message}", flush=True)


class McpError(RuntimeError):
    pass


class McpClient:
    def __init__(self, base_url):
        self.base_url = base_url.rstrip("/")

    def post(self, path, payload, timeout):
        data = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            f"{self.base_url}{path}",
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            # 工具执行失败时服务端会返回 4xx/5xx，但 body 仍是标准 JSON，
            # 这里照样解析出来交给调用方判断 success。
            body = error.read().decode("utf-8", errors="replace")
            try:
                return json.loads(body)
            except json.JSONDecodeError:
                raise McpError(f"{path} 返回 HTTP {error.code}: {body[:600]}") from error
        except urllib.error.URLError as error:
            raise McpError(
                f"连不上 ShineMCP（{self.base_url}）：{error.reason}\n"
                f"请确认 UE 编辑器已启动，且 Plugins/ShineMCP 已编译并启用。"
            ) from error

    def call(self, tool, arguments=None, timeout=600):
        return self.post(f"/tools/{tool}", arguments or {}, timeout)

    def health(self, timeout=20):
        try:
            with urllib.request.urlopen(f"{self.base_url}/health", timeout=timeout) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.URLError as error:
            raise McpError(f"连不上 ShineMCP（{self.base_url}）：{error.reason}") from error

    def ensure_success(self, tool, result, hint=""):
        if not isinstance(result, dict) or not result.get("success", False):
            detail = result.get("error") if isinstance(result, dict) else result
            raise McpError(f"{tool} 失败：{detail} {hint}".strip())
        return result


def parse_vector(text):
    """解析 UE 的向量字符串：兼容 "X=1.0 Y=2.0 Z=3.0" 与 "(1.0, 2.0, 3.0)" 两种写法。"""
    numbers = re.findall(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", str(text))
    values = [float(value) for value in numbers[:3]]
    while len(values) < 3:
        values.append(0.0)
    return values


def look_at_rotation(camera, target):
    """UE 风格（左手、Z 向上、单位度）的看向目标旋转。"""
    direction = [target[i] - camera[i] for i in range(3)]
    yaw = math.degrees(math.atan2(direction[1], direction[0]))
    horizontal = math.sqrt(direction[0] ** 2 + direction[1] ** 2)
    pitch = math.degrees(math.atan2(direction[2], horizontal))
    return pitch, yaw, 0.0


def normalize_prompt_numbers(prompt):
    """把已知数值输入里的字符串转回 int/float。"""
    for node in prompt.values():
        if not isinstance(node, dict):
            continue
        inputs = node.get("inputs")
        if not isinstance(inputs, dict):
            continue
        for key, value in list(inputs.items()):
            if key not in NUMERIC_KEYS or not isinstance(value, str):
                continue
            try:
                if "." in value or "e" in value.lower():
                    inputs[key] = float(value)
                else:
                    inputs[key] = int(value)
            except ValueError:
                pass
    return prompt


def build_graph_json(params):
    """生成 Shine Graph JSON：导演台节点 + 一个 Preview 显示节点（结果直接画在图里）。"""
    return json.dumps(
        {
            "nodes": [
                {
                    "id": "scene_ai",
                    "type": "Director",
                    "title": "场景 AI 生成",
                    "position": [0, 0],
                    "parameters": params,
                },
                {
                    # 接在导演台的 Image 输出上：生成结果会直接画在这个节点里
                    "id": "preview",
                    "type": "Preview",
                    "title": "结果预览",
                    "position": [620, 0],
                    "parameters": {},
                },
            ],
            "links": [
                {"from": ["scene_ai", "Image"], "to": ["preview", "Image"]},
            ],
        },
        ensure_ascii=False,
        indent=2,
    )


def main():
    parser = argparse.ArgumentParser(description="UE 场景 -> ComfyUI AI 生成 的端到端流程")
    parser.add_argument("--mode", choices=["image", "video"], default="image",
                        help="image=SD1.5 图片路线（默认）；video=LTX-2 图像→视频")
    parser.add_argument("--mcp-url", default=DEFAULT_MCP_URL, help="ShineMCP 服务地址")
    parser.add_argument("--comfy-url", default=DEFAULT_COMFY_URL, help="ComfyUI 服务地址")
    parser.add_argument("--level", default=LEVEL_PATH, help="关卡资产路径")
    parser.add_argument("--checkpoint", default=None,
                        help="checkpoint 文件名（例如二次元底模 Counterfeit-V2.5_fp16.safetensors）；默认用 SD1.5 基础模型")
    parser.add_argument("--prompt", default=None, help="正向提示词")
    parser.add_argument("--negative", default=None, help="反向提示词")
    parser.add_argument("--seed", type=int, default=None, help="随机种子")
    parser.add_argument("--steps", type=int, default=None, help="采样步数")
    parser.add_argument("--cfg", type=float, default=None, help="CFG（越高越听提示词，风格化用 8~9）")
    parser.add_argument("--denoise", type=float, default=None, help="img2img 重绘强度")
    parser.add_argument("--frames", type=int, default=49, help="[视频] 帧数（建议 8n+1）")
    parser.add_argument("--fps", type=float, default=25.0, help="[视频] 帧率")
    parser.add_argument("--resolution", type=int, default=768, help="生成与捕获分辨率（正方形）")
    parser.add_argument("--width", type=int, default=None,
                        help="覆盖宽度（做横构图用，例如 1152；捕获与生成会一起变，保证 ControlNet 与出图同比例）")
    parser.add_argument("--height", type=int, default=None, help="覆盖高度（例如 648）")
    parser.add_argument("--depth-strength", type=float, default=None, help="深度 ControlNet 强度（越大越贴合 UE 场景结构）")
    parser.add_argument("--normal-strength", type=float, default=None, help="法线 ControlNet 强度")
    parser.add_argument("--control-end", type=float, default=None,
                        help="ControlNet 结束步占比（调小=后面几步放开，风格更容易变成二次元）")
    parser.add_argument("--viewport-camera", action="store_true",
                        help="不按包围盒算机位，直接用当前关卡视口的相机（先在编辑器里摆好构图）")
    parser.add_argument("--skip-level", action="store_true", help="不新建关卡，只跑捕获与生成")
    parser.add_argument("--open-result", action="store_true",
                        help="跑完自动打开图表资产（默认不打开，参数都是直接改资产，不会弹窗）")
    parser.add_argument("--poll-timeout", type=int, default=None,
                        help="等待生成完成的最长秒数（默认图片 900，视频 3600）")
    args = parser.parse_args()

    if args.poll_timeout is None:
        args.poll_timeout = 3600 if args.mode == "video" else 900

    client = McpClient(args.mcp_url)

    # ---------------------------------------------------------------- 0. 探活
    health = client.health()
    log(f"MCP 服务正常：{health.get('server')} {health.get('version')}，工具数 {health.get('toolCount')}")

    comfy = client.ensure_success("ue_comfy_ping", client.call("ue_comfy_ping", {"baseUrl": args.comfy_url}))
    log(f"ComfyUI 连接正常：{comfy.get('comfyuiVersion')} / {comfy.get('device')}，显存空闲 {comfy.get('vramFreeGB', 0):.1f} GB")

    checkpoints = client.ensure_success("ue_comfy_list_checkpoints", client.call("ue_comfy_list_checkpoints", {"baseUrl": args.comfy_url}))
    checkpoint_names = checkpoints.get("checkpoints", [])
    log(f"可用 checkpoint：{checkpoint_names}")
    if "v1-5-pruned-emaonly.safetensors" not in checkpoint_names:
        log("警告：没找到 v1-5-pruned-emaonly.safetensors，"
            "请先运行 Plugins/ShineMCP/Tools/Download-ComfyModels.ps1 下载模型。")

    # ------------------------------------------------------- 1. 关卡与演示场景
    if not args.skip_level:
        created = client.call("ue_create_level", {"assetPath": args.level, "buildDemoScene": True})
        if created.get("success"):
            log(f"已新建关卡 {args.level} 并摆好演示场景（{created.get('demoScene', {}).get('spawned', 0)} 个 Actor）")
        else:
            log(f"新建关卡未成功（{created.get('error')}），改为打开已有关卡并重建演示场景")
            client.ensure_success("ue_load_level", client.call("ue_load_level", {"assetPath": args.level}))
            scene = client.ensure_success("ue_build_demo_scene", client.call("ue_build_demo_scene", {"clearExisting": True}))
            log(f"演示场景已重建：{scene.get('spawned')} 个 Actor")

        saved = client.call("ue_save_current_level")
        log(f"关卡已保存：{saved.get('saved')}")

        # 刚生成的 Actor 需要一帧才会在渲染里出现，等一下再捕获。
        time.sleep(1.0)

    # ------------------------------------------------------------ 2. 计算机位
    bounds = client.call("ue_get_level_bounds")
    resolution = max(256, min(2048, args.resolution))

    # 横构图时捕获与生成用同一组宽高，这样深度/法线 ControlNet 和出图完全同比例，
    # 不会因为 ImageScale 拉伸而把场景结构带歪。
    capture_width = max(256, min(2048, args.width)) if args.width else resolution
    capture_height = max(256, min(2048, args.height)) if args.height else resolution

    if bounds.get("success") and not bounds.get("empty"):
        center = parse_vector(bounds["center"])
        size = float(bounds.get("size", 2000.0))
    else:
        center = [0.0, 0.0, 150.0]
        size = 4000.0

    # 地面会撑大包围盒，所以按比例取一个"框住主体"的观察距离，再做上下限约束。
    distance = min(max(size * 0.20, 900.0), 1800.0)
    pitch_degrees = 18.0
    horizontal = distance * math.cos(math.radians(pitch_degrees))
    vertical = distance * math.sin(math.radians(pitch_degrees))

    target = [center[0], center[1], min(center[2], 200.0)]
    camera = [
        target[0] - horizontal * 0.65,
        target[1] - horizontal * 0.76,
        target[2] + vertical,
    ]
    pitch, yaw, roll = look_at_rotation(camera, target)

    # -------------------------------------------------------------- 3. 捕获
    if args.viewport_camera:
        # 构图由人在编辑器里摆：直接用关卡视口相机（和 ShineEditor 那个"捕获视口"按钮同一套逻辑）
        log("机位：直接使用当前关卡视口相机")
        capture = client.ensure_success(
            "ue_capture_from_viewport",
            client.call("ue_capture_from_viewport", {
                "width": capture_width, "height": capture_height,
                "prefix": "Scene",
            }),
        )
        log(f"  实际机位：{capture.get('cameraLabel', '?')}")
    else:
        log(f"机位 {tuple(round(v, 1) for v in camera)} -> 目标 {tuple(round(v, 1) for v in target)}，FOV 55")
        capture = client.ensure_success(
            "ue_capture_scene",
            client.call("ue_capture_scene", {
                "x": round(camera[0], 2), "y": round(camera[1], 2), "z": round(camera[2], 2),
                "pitch": round(pitch, 3), "yaw": round(yaw, 3), "roll": round(roll, 3),
                "fov": 55.0,
                "width": capture_width, "height": capture_height,
                "prefix": "Scene",
                "captureColor": True, "captureDepth": True, "captureNormal": True,
            }),
        )
    files = capture["files"]
    log(f"场景已捕获到 {capture['directory']}")
    for channel in ("color", "depth", "normal"):
        log(f"  {channel:6s} -> {files[channel]['path']}")

    # ---------------------------------------------------------- 4. 上传图
    uploaded = {}
    for channel in ("color", "depth", "normal"):
        result = client.ensure_success(
            "ue_comfy_upload_image",
            client.call("ue_comfy_upload_image", {"filePath": files[channel]["path"], "baseUrl": args.comfy_url}),
        )
        uploaded[channel] = result["name"]
        log(f"已上传 {channel} -> {result['name']}")

    # ------------------------------------------- 5. 新建 Shine Comfy 图并连好
    graph = client.ensure_success(
        "ue_create_comfy_graph",
        client.call("ue_create_comfy_graph", {
            "packagePath": GRAPH_PACKAGE_PATH,
            "assetName": GRAPH_ASSET_NAME,
            "baseUrl": args.comfy_url,
        }),
    )
    log(f"Shine Comfy 图资产：{graph['assetPath']}（新建={graph.get('created')}）")

    node_params = {
        # 两条路线互斥
        "EnableImageGen": args.mode == "image",
        "EnableVideoGen": args.mode == "video",

        # --- SD1.5 图片路线 ---
        "Checkpoint": "v1-5-pruned-emaonly.safetensors",
        "ColorImage": uploaded["color"],
        "DepthImage": uploaded["depth"],
        "NormalImage": uploaded["normal"],
        "DepthControlNet": "control_v11f1p_sd15_depth.pth",
        "NormalControlNet": "control_v11p_sd15_normalbae.pth",
        "Width": capture_width,
        "Height": capture_height,
        "Steps": 30,
        "CfgScale": 7.5,
        "Seed": 12345,
        "Denoise": 0.78,
        "DepthStrength": 0.7,
        "NormalStrength": 0.38,
        "ControlEndPercent": 0.85,
        "SamplerName": "dpmpp_2m",
        "Scheduler": "karras",
        "OutputPrefix": "Shine/SceneToImage",

        # --- LTX-2 视频路线 ---
        "LTXCheckpoint": "ltx-2-19b-dev-fp8.safetensors",
        "LTXTextEncoder": "gemma_3_12B_it_fp4_mixed.safetensors",
        "LTXDistilledLora": "ltx-2-19b-distilled-lora-384.safetensors",
        "LTXUpscaler": "ltx-2-spatial-upscaler-x2-1.0.safetensors",
        "LTXVideoImage": uploaded["color"],
        "LTXPositivePrompt": DEFAULT_LTX_PROMPT,
        "LTXNegativePrompt": DEFAULT_LTX_NEGATIVE,
        "LTXWidth": capture_width,
        "LTXHeight": capture_height,
        "LTXFrames": args.frames,
        "LTXFps": args.fps,
        "LTXSteps": 20,
        "LTXSeed": 42,
        "LTXOutputPrefix": "Shine/LTX2_i2v",
    }

    if args.checkpoint:
        node_params["Checkpoint"] = args.checkpoint
    if args.prompt:
        node_params["PositivePrompt"] = args.prompt
        node_params["LTXPositivePrompt"] = args.prompt
    if args.negative:
        node_params["NegativePrompt"] = args.negative
        node_params["LTXNegativePrompt"] = args.negative
    if args.seed is not None:
        node_params["Seed"] = args.seed
        node_params["LTXSeed"] = args.seed
    if args.cfg is not None:
        node_params["CfgScale"] = args.cfg
    if args.steps is not None:
        node_params["Steps"] = args.steps
        node_params["LTXSteps"] = args.steps
    if args.denoise is not None:
        node_params["Denoise"] = args.denoise
    if args.depth_strength is not None:
        node_params["DepthStrength"] = args.depth_strength
    if args.normal_strength is not None:
        node_params["NormalStrength"] = args.normal_strength
    if args.control_end is not None:
        node_params["ControlEndPercent"] = args.control_end

    built = client.ensure_success(
        "ue_build_comfy_graph",
        client.call("ue_build_comfy_graph", {
            "assetPath": graph["assetPath"],
            "graphJson": build_graph_json(node_params),
        }),
    )
    mode_label = "SD1.5 图片（颜色 img2img + 深度/法线双 ControlNet）" if args.mode == "image" \
        else f"LTX-2 视频（{args.frames} 帧 @ {args.fps:g}fps，首帧=场景颜色图）"
    log(f"图表已连好：{built.get('nodeCount')} 个节点，路线 = {mode_label}")

    # ---------------------------------------- 6. 导出 prompt 并提交给 ComfyUI
    exported = client.ensure_success(
        "ue_export_comfy_prompt",
        client.call("ue_export_comfy_prompt", {"assetPath": graph["assetPath"]}),
    )
    prompt = normalize_prompt_numbers(json.loads(exported["promptJson"]))
    log(f"已从图表导出 ComfyUI prompt：{len(prompt)} 个原生节点")
    log("  展开后的节点类型：" + ", ".join(sorted({n.get("class_type", "?") for n in prompt.values()})))

    submitted = client.ensure_success(
        "ue_comfy_submit",
        client.call("ue_comfy_submit", {"promptJson": json.dumps(prompt), "baseUrl": args.comfy_url}),
    )
    prompt_id = submitted["promptId"]
    log(f"任务已提交，promptId = {prompt_id}")

    # ------------------------------------------------------- 7. 等生成完成
    started = time.time()
    status = "unknown"
    images = []
    while time.time() - started < args.poll_timeout:
        result = client.call("ue_comfy_prompt_result", {"promptId": prompt_id, "baseUrl": args.comfy_url}, timeout=120)
        status = result.get("status")

        # 出错时优先把 ComfyUI 的原始异常抛出来，光看状态码定位不了问题。
        if result.get("isError") or status == "error":
            raise McpError(
                "ComfyUI 执行出错："
                f"{result.get('exceptionType', 'Error')}: {result.get('exceptionMessage', result.get('statusStr', ''))}\n"
                f"    失败节点：{result.get('failedNodeType', '?')} (id={result.get('failedNodeId', '?')})"
                + (f"\n{result.get('traceback', '')}" if result.get("traceback") else "")
            )

        if status == "done":
            images = result.get("images", [])
            break

        queue = client.call("ue_comfy_queue", {"baseUrl": args.comfy_url}, timeout=60)
        log(f"  等待中… 状态={status} 队列(运行/等待)={queue.get('running')}/{queue.get('pending')}")
        time.sleep(2)

    if status != "done":
        raise McpError(f"等待超时（{args.poll_timeout}s），promptId={prompt_id}")

    log(f"生成完成，用时 {time.time() - started:.1f}s，输出 {len(images)} 个文件")

    # ------------------------------------------------------- 8. 回读进 UE
    # 图里同时接了 SaveImage 和 PreviewImage，后者会多回一份 temp 图（内容相同）。
    output_images = [item for item in images if item.get("type") == "output"] or images

    # 直接用 ComfyUI 写出的原图：不下载副本、不导入 UE 纹理资产。
    preview_paths = [item["localPath"] for item in output_images if item.get("localPath")]
    if preview_paths:
        for path in preview_paths:
            log(f"结果原图（ComfyUI）：{path}")
    else:
        log("没有拿到结果原图的本地路径：请到 项目设置 → 插件 → Shine Comfy 里填 ComfyUI 的 output 目录"
            "（ComfyUI 跑在别的机器上时才会需要下载副本）。")

    # 结果直接贴到关卡视口右侧，编辑器里一眼就能看到
    if preview_paths:
        shown = client.call("ue_show_image_preview", {
            "filePaths": preview_paths,
            "title": f"ComfyUI 生成结果 {prompt_id[:8]}",
        })
        if shown.get("success"):
            log(f"结果已贴到 UE 视口右侧预览面板（{shown.get('shown')} 张）")
        else:
            log(f"贴预览失败：{shown.get('error')}")

    # 把结果图写进图里的显示节点（Preview / Gallery），打开图表就能看到，且会存进资产
    if preview_paths:
        in_graph = client.call("ue_set_graph_result_images", {
            "assetPath": graph["assetPath"],
            "filePaths": preview_paths,
        })
        if in_graph.get("success"):
            log(f"结果已画进图里的显示节点（{in_graph.get('updatedNodes')} 个节点，含 "
                + ", ".join(in_graph.get("nodeTitles", [])) + "）")
        else:
            log(f"写进图表失败：{in_graph.get('error')}")

    # 默认不弹窗口：参数都是直接改资产，需要看图时自己打开图表就行。
    if args.open_result:
        client.call("ue_open_asset", {"assetPath": graph["assetPath"]})

    log("全部完成 ✔")
    log(f"图表资产：{graph['assetPath']}（结果就画在图里的 Preview Image 节点上，点“放大”看大图）")
    if preview_paths:
        log(f"结果原图：{preview_paths[0]}")


if __name__ == "__main__":
    try:
        main()
    except McpError as error:
        print(f"\n[ShineFlow] 失败：{error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[ShineFlow] 已中断", file=sys.stderr)
        sys.exit(130)
