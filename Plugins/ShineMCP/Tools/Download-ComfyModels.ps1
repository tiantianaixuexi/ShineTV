#Requires -Version 5.1
<#
.SYNOPSIS
    下载 ShineAI / ComfyUI 需要的基础模型。

.DESCRIPTION
    【约定】本项目下载模型一律优先使用 ModelScope 国内镜像（https://modelscope.cn），
    不走 huggingface.co（站点在国内不可用）；只有 ModelScope 确实没有该文件时，
    才退回 hf-mirror.com。

    脚本串行下载（不要并行！实测并行会被限速到几百 KB/s，串行可达 ~10MB/s），
    支持断点续传（curl -C -），已下载完整会跳过。

    下载内容：
      --- SD1.5 路线（颜色 img2img + 深度/法线双 ControlNet）---
      1. checkpoints/v1-5-pruned-emaonly.safetensors        SD1.5 基础模型（约 4.0GB）
         来源 AI-ModelScope/stable-diffusion-v1-5
      2. controlnet/control_v11f1p_sd15_depth.pth           ControlNet 深度（约 1.35GB）
         来源 AI-ModelScope/ControlNet-v1-1
      3. controlnet/control_v11p_sd15_normalbae.pth         ControlNet 法线（约 1.35GB）
         来源 AI-ModelScope/ControlNet-v1-1

      --- LTX-2 路线（图像 -> 视频）---
      4. text_encoders/gemma_3_12B_it_fp4_mixed.safetensors LTX-2 官方 Gemma-3 12B 文本编码器（约 9.5GB）
         来源 Comfy-Org/ltx-2 的 split_files/text_encoders
         注意：必须是这个文件（内部带 spiece tokenizer）。秋叶包自带的 gemma4_* 是
         另一套更小的架构（hidden 1536，不是 Gemma-3 12B），加载会报 "invalid tokenizer"。
      其余 LTX-2 权重一般已随整合包提供，本脚本只补缺的文件：
         checkpoints/ltx-2-19b-dev-fp8.safetensors
         loras/ltx-2-19b-distilled-lora-384.safetensors
         latent_upscale_models/ltx-2-spatial-upscaler-x2-1.0.safetensors

    ModelScope 直链格式：
      https://modelscope.cn/api/v1/models/{namespace}/{name}/repo?Revision=master&FilePath={文件路径}

.PARAMETER ComfyRoot
    ComfyUI 根目录，默认 H:\ComfyUI-aki-v3

.PARAMETER Only
    只下载其中一项：checkpoint / depth / normal / ltx2

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Download-ComfyModels.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Download-ComfyModels.ps1 -Only ltx2
#>
[CmdletBinding()]
param(
    [string]$ComfyRoot = 'H:\ComfyUI-aki-v3',
    [ValidateSet('all', 'checkpoint', 'depth', 'normal', 'ltx2')]
    [string]$Only = 'all'
)

$ErrorActionPreference = 'Stop'

$ModelsRoot = Join-Path $ComfyRoot 'ComfyUI\models'
if (-not (Test-Path -LiteralPath $ModelsRoot)) {
    throw "找不到 ComfyUI models 目录：$ModelsRoot"
}

function New-ModelScopeUrl {
    param([string]$Repo, [string]$FilePath)
    return "https://modelscope.cn/api/v1/models/$Repo/repo?Revision=master&FilePath=$FilePath"
}

$Tasks = @(
    [pscustomobject]@{
        Key   = 'checkpoint'
        Repo  = 'AI-ModelScope/stable-diffusion-v1-5'
        File  = 'v1-5-pruned-emaonly.safetensors'
        Dir   = 'checkpoints'
        Bytes = 4265146304
    }
    [pscustomobject]@{
        Key   = 'depth'
        Repo  = 'AI-ModelScope/ControlNet-v1-1'
        File  = 'control_v11f1p_sd15_depth.pth'
        Dir   = 'controlnet'
        Bytes = 1445235365
    }
    [pscustomobject]@{
        Key   = 'normal'
        Repo  = 'AI-ModelScope/ControlNet-v1-1'
        File  = 'control_v11p_sd15_normalbae.pth'
        Dir   = 'controlnet'
        Bytes = 1445236049
    }
    [pscustomobject]@{
        Key        = 'ltx2'
        Repo       = 'Comfy-Org/ltx-2'
        RemotePath = 'split_files/text_encoders/gemma_3_12B_it_fp4_mixed.safetensors'
        File       = 'gemma_3_12B_it_fp4_mixed.safetensors'
        Dir        = 'text_encoders'
        Bytes      = 9447702218
    }
)

if ($Only -ne 'all') {
    $Tasks = @($Tasks | Where-Object { $_.Key -eq $Only })
}

$LogFile = Join-Path $ModelsRoot 'shine_model_download.log'
function Write-Log {
    param([string]$Message)
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Message
    Write-Host $line
    Add-Content -LiteralPath $LogFile -Value $line -Encoding UTF8
}

Write-Log "开始下载（ModelScope 镜像），共 $($Tasks.Count) 个文件"

$sw = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($task in $Tasks) {
    $dir = Join-Path $ModelsRoot $task.Dir
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }

    $out = Join-Path $dir $task.File
    $remotePath = if ($task.PSObject.Properties.Name -contains 'RemotePath' -and $task.RemotePath) { $task.RemotePath } else { $task.File }
    $url = New-ModelScopeUrl -Repo $task.Repo -FilePath $remotePath

    if (Test-Path -LiteralPath $out) {
        $size = (Get-Item -LiteralPath $out).Length
        if ($size -eq $task.Bytes) {
            Write-Log "已存在且完整，跳过：$($task.File)"
            continue
        }
        Write-Log ("续传：{0}（已有 {1:N1} MB / {2:N1} MB）" -f $task.File, ($size / 1MB), ($task.Bytes / 1MB))
    }
    else {
        Write-Log ("下载：{0}（{1:N1} MB）" -f $task.File, ($task.Bytes / 1MB))
    }

    $t0 = [System.Diagnostics.Stopwatch]::StartNew()
    & curl.exe -L --fail --retry 10 --retry-delay 5 --connect-timeout 20 `
        -C - -o $out $url `
        --write-out "http=%{http_code} downloaded=%{size_download} avg=%{speed_download}B/s total=%{time_total}s`n" `
        -s
    $code = $LASTEXITCODE
    $t0.Stop()

    if ($code -ne 0) {
        Write-Log ("下载失败（curl 退出码 {0}）：{1}" -f $code, $task.File)
        exit $code
    }

    $finalSize = (Get-Item -LiteralPath $out).Length
    if ($finalSize -eq $task.Bytes) {
        Write-Log ("完成：{0}（{1:N1} MB，耗时 {2:N1}s）" -f $task.File, ($finalSize / 1MB), $t0.Elapsed.TotalSeconds)
    }
    else {
        Write-Log ("尺寸不匹配：{0} 实际 {1} 期望 {2}，可重新运行本脚本续传。" -f $task.File, $finalSize, $task.Bytes)
    }
}

$sw.Stop()
Write-Log ("全部结束，总耗时 {0:N1} 分钟。重启 ComfyUI 后模型即可在节点下拉里看到。" -f $sw.Elapsed.TotalMinutes)
