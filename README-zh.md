# CodexUsageBar

原生 `Win32/C++` 的 Codex 用量桌面挂件，面向 Windows。

[English README](README.md)
[![Linux.do 社区](https://img.shields.io/badge/Linux.do-%E7%A4%BE%E5%8C%BA-2ea44f?style=flat-square)](https://linux.do/)

## 项目看板

[![Build](https://github.com/luodaoyi/codex-useage-win/actions/workflows/build.yml/badge.svg)](https://github.com/luodaoyi/codex-useage-win/actions/workflows/build.yml)
[![Latest Release](https://img.shields.io/github/v/release/luodaoyi/codex-useage-win?display_name=tag)](https://github.com/luodaoyi/codex-useage-win/releases/latest)
[![Release Date](https://img.shields.io/github/release-date/luodaoyi/codex-useage-win)](https://github.com/luodaoyi/codex-useage-win/releases/latest)
[![Total Downloads](https://img.shields.io/github/downloads/luodaoyi/codex-useage-win/total)](https://github.com/luodaoyi/codex-useage-win/releases)
[![Stars](https://img.shields.io/github/stars/luodaoyi/codex-useage-win?style=flat)](https://github.com/luodaoyi/codex-useage-win/stargazers)

它会读取当前 Codex 账号的限额信息，在桌面上显示一个可拖动、可缩放的固定布局面板，用来观察：

- `5 小时限额` 已用/剩余
- `每周限额` 已用/剩余
- `当前周期理论应使用多少`
- `实际已经使用多少`
- `当前高于/低于平均进度`
- `距离重置还剩多久`
- `上次成功刷新时间`
- `距离下一次自动刷新还剩多久`

## 示例

### 标准模式（含智能评分）

![CodexUsageBar 标准模式](IMG/1.png)

### 简单模式

![CodexUsageBar 简单模式](IMG/2.png)

## 功能

- 原生 `Win32 + Direct2D + DirectWrite + WinHTTP`
- 无 `C#`、无 `WebView`
- 读取 `%USERPROFILE%\.codex\auth.json` 或 `%CODEX_HOME%\auth.json`
- 请求 `GET https://chatgpt.com/backend-api/wham/usage`
- 三种显示模式：
  - 标准模式：套餐与重置卡、限额进度条、底部操作按钮
  - 简单模式：只显示 `5小时剩余`、`本周剩余` 和状态标签，适合更紧凑的小卡片
  - 任务栏模式：更小的剩余额度小条，自动贴近当前显示器的任务栏边缘，并固定贴边显示
- 智能评分排名（默认关闭）：
  - 软件工程能力：`https://codexradar.com/api/intelligence-efficiency-metrics?refresh=1`
  - 视觉空间能力：`https://codexradar.com/api/visual-spatial-reasoning?refresh=1`
  - 按分数倒序，显示分数、时间和金额
  - 面板上按 JSON 动态生成模型组按钮，可多选过滤
  - 每页 10 条，支持上一页 / 下一页
- 桌面浮层挂件，可拖动、可缩放
- 根据当前进度状态切换颜色
- 支持开机自启开关
- 支持 `Always on top` 置顶开关
- 支持 `Lock position` 固定位置开关
- 支持 `完整模式` / `简单模式` / `任务栏模式` 显示模式切换
- 支持中英文界面切换

## 刷新策略

- 远程用量接口：每 `60` 秒刷新一次
- 智能评分：开启后每 `5` 分钟刷新一次
- 本地倒计时：每 `1` 秒重绘一次
- 右下角显示：
  - 上次成功刷新时间
  - 距离下一次自动刷新的倒计时
- 右键 `立即刷新`：立即强制刷新用量；评分开启时一并刷新雷达数据

## 使用方式

- 拖动挂件主体：移动位置
- 拖右边、下边、右下角：调整大小
- 任务栏模式固定贴边显示，不支持拖动或缩放
- 右键菜单：
  - `立即刷新`
  - `刷新 Token`
  - `开机自启`
  - `始终置顶`
  - `固定位置`
  - `显示模式`
  - `智能评分排名`：`关闭` / `软件工程能力` / `视觉空间能力`
  - `语言`
  - `重置组件位置`
  - `退出`
- 评分面板：点击 `全部` / `GPT` / `Grok` 等按钮多选过滤；用 `上一页` / `下一页` 翻页

位置和尺寸会保存到：

- `%APPDATA%\CodexUsageBar\settings.ini`

开机自启使用当前用户注册表：

- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`

## 本地构建

### 直接构建

```cmd
build.cmd
```

输出文件：

- `CodexUsageBar.exe`

### 使用 CMake

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## GitHub Actions

仓库包含自动构建和自动发布工作流。

### 自动构建

- 平台：
  - `x64`
  - `ARM64`
- 触发方式：
  - 推送到 `main` / `master`
  - `pull_request`
  - `workflow_dispatch`

### 自动发布 Release

- 触发方式：
  - 推送版本标签，例如 `v0.1.0`
- 行为：
  - 自动构建 `x64` 和 `ARM64`
  - 自动创建 GitHub Release
  - 自动上传：
    - `CodexUsageBar-x64.exe`
    - `CodexUsageBar-ARM64.exe`

## 发布流程

```bash
git tag v0.1.0
git push origin master
git push origin v0.1.0
```

如果默认分支改成了 `main`，把上面的 `master` 换成 `main` 即可。

## 已知限制

- 当前只使用 `auth.json` 中现有的 `access_token`，还没做 `refresh_token` 自动续期
- 如果 OpenAI 后端接口字段变化，需要同步调整解析逻辑
- 当前是桌面浮层挂件，不是 Windows 7 时代的官方 Gadget 平台

## Star History

<a href="https://www.star-history.com/?repos=luodaoyi%2Fcodex-useage-win&type=timeline&logscale=&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=luodaoyi/codex-useage-win&type=timeline&theme=dark&logscale&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=luodaoyi/codex-useage-win&type=timeline&logscale&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=luodaoyi/codex-useage-win&type=timeline&logscale&legend=top-left" />
 </picture>
</a>
