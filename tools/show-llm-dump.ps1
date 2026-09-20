# S40：把 `SHINE_LLM_DUMP` 落的原始报文渲染成**人可读**的 markdown。
#
# 用法（在工程根目录）：
#   .\tools\show-llm-dump.ps1                                  # 用默认 dump 目录
#   .\tools\show-llm-dump.ps1 -Dir <目录> -Latest 3            # 指定目录、只看最近 3 组
#
# 产出：同目录下的 *_readable.md —— 每份含 model / 总长 / system 段 / user 段 / 回复文本。
# 为什么要有它：raw 报文里换行是 `\n` 转义，读起来等于没有换行（用户："发给 AI 的提示词在哪"）。
param(
    [string]$Dir = "E:\c++\ShineTV\runtime\sd-e2e\novels\rain-signal\work\llm-dump",
    [int]$Latest = 5
)

if (-not (Test-Path $Dir)) {
    Write-Host "目录不存在：$Dir" -ForegroundColor Yellow
    Write-Host "先带 dump 跑一次，例如：" -ForegroundColor Yellow
    Write-Host '  $env:SHINE_LLM_DUMP = "' + $Dir + '"' -ForegroundColor Yellow
    Write-Host '  .\build\ShineTVStudio.exe --novel-storyboard 1' -ForegroundColor Yellow
    exit 1
}

# 取每个时间戳的**请求**（_req.txt），按时间倒序
$reqs = Get-ChildItem "$Dir\*_req.txt" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First $Latest
if (-not $reqs) {
    Write-Host "该目录里没有 *_req.txt（dump 开关没开？）" -ForegroundColor Yellow
    exit 1
}

function Write-Section([string]$Title, [string]$Text) {
    "## $Title（$($Text.Length) 字符)"
    ""
    '```text'
    $Text
    '```'
    ""
}

foreach ($req in $reqs) {
    $stamp = $req.BaseName -replace '_req$', ''
    $out = Join-Path $Dir "${stamp}_readable.md"
    $md = New-Object System.Collections.Generic.List[string]

    try {
        $j = Get-Content $req.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        $md.Add("# 请求（$stamp）—— 不是合法 JSON，原样附上")
        $md.Add('')
        $md.Add('```text')
        $md.Add((Get-Content $req.FullName -Raw -Encoding UTF8))
        $md.Add('```')
        Set-Content -Path $out -Value $md -Encoding UTF8
        Write-Host "写出（未解析）：$out" -ForegroundColor DarkYellow
        continue
    }

    $md.Add("# LLM 请求（$stamp）")
    $md.Add('')
    $md.Add("- model: ``$($j.model)``")
    if ($null -ne $j.max_output_tokens) { $md.Add("- max_output_tokens: $($j.max_output_tokens)") }
    if ($null -ne $j.max_completion_tokens) { $md.Add("- max_completion_tokens: $($j.max_completion_tokens)") }
    $md.Add("- 报文总长: $((Get-Item $req.FullName).Length) 字节")
    $md.Add('')

    # system：Responses 用 instructions；Chat 用 messages[role=system]
    $sys = ""
    if ($j.instructions) { $sys = [string]$j.instructions }
    $usr = ""
    if ($j.messages) {
        foreach ($m in $j.messages) {
            if ($m.role -eq 'system' -and -not $sys) { $sys = [string]$m.content }
            elseif ($m.role -eq 'user') { $usr += [string]$m.content }
        }
    }
    if (-not $usr -and $null -ne $j.input) {
        if ($j.input -is [string]) { $usr = [string]$j.input }
        else { $usr = ($j.input | ConvertTo-Json -Depth 30) }
    }

    $md.Add((Write-Section 'system（instructions）' $sys))
    $md.Add('')
    $md.Add((Write-Section 'user（拼进去的上下文）' $usr))

    # 顺带把 user 里的分段标题列出来（一眼看骨架）
    $titles = [regex]::Matches($usr, [char]0x3010 + '[^' + [char]0x3011 + ']{1,40}' + [char]0x3011)
    if ($titles.Count -gt 0) {
        $md.Add('## user 的分段骨架')
        $md.Add('')
        foreach ($t in $titles) { $md.Add("- ``$($t.Value)``") }
        $md.Add('')
    }

    # 响应
    $respFile = Get-ChildItem "$Dir\${stamp}*_resp.txt" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($respFile) {
        $raw = Get-Content $respFile.FullName -Raw -Encoding UTF8
        $body = $raw -replace '(?s)^status=\d+ error=[^\r\n]*[\r\n]*', ''
        $text = ""
        try {
            $rj = $body | ConvertFrom-Json
            if ($rj.output_text) { $text = [string]$rj.output_text }
            elseif ($rj.choices) { $text = [string]$rj.choices[0].message.content }
        } catch { }
        $md.Add('# LLM 响应')
        $md.Add('')
        if ($text) { $md.Add((Write-Section '回复正文' $text)) }
        else { $md.Add('（没解析出正文，附原始报文）'); $md.Add(''); $md.Add('```text'); $md.Add($body); $md.Add('```') }
    }

    Set-Content -Path $out -Value $md -Encoding UTF8
    Write-Host "写出：$out" -ForegroundColor Green
}

Write-Host ""
Write-Host "打开最近的：" -ForegroundColor Cyan
Get-ChildItem "$Dir\*_readable.md" | Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 | ForEach-Object { Write-Host "  $($_.FullName)" -ForegroundColor Cyan }
