# OpenCode 代理配置工具

[中文](#中文) | [English](#english)

## 中文

### 简介

把 [opencode_proxy.py](D:\swf\dsh_ws\opencode_proxy.py) 的功能移植为图形界面：自动探测本地代理，把代理环境变量写入 OpenCode 后台服务与用户环境，重启服务并验证，全程通过日志面板实时输出探测过程、每个配置项的写入状态与验证结果。

### 原理

依据 OpenCode 官方 [Network 文档](https://opencode.ai/docs/network/)：

1. OpenCode 没有 proxy 配置项，只认标准环境变量：`HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY` / `NO_PROXY`。
2. 后台服务是独立进程，shell 里 export 只对"从该 shell 启动"的服务生效；要持久化必须写进服务配置：`opencode service set env KEY VALUE`。
3. 改服务环境变量会停掉正在跑的服务，需要 `opencode service restart` 重新拉起。
4. CLI 与本地服务之间走 `127.0.0.1`，所以 `NO_PROXY` 必须排除回环地址（工具固定写 `localhost,127.0.0.1,::1,0.0.0.0`），否则连自己的本地服务都会被塞进代理。

### 界面

- **开始配置**：自动探测可用代理 → 写服务环境变量 → 写用户环境变量 → 重启服务 → 三层验证（对应脚本默认主流程）。
- **仅探测**：只探测可用代理并展示，不写入任何东西（对应 `--list`）。
- **撤销代理**：清空服务环境变量与用户环境变量（含小写键、`NODE_EXTRA_CA_CERTS`），按选项重启（对应 `--off`）。
- **停止**：请求中止流水线。只在步骤之间生效；`service restart` 等当前步骤执行期间无法中断。
- **说明**：弹出「使用说明」对话框：软件作用、各按钮与参数含义、右下角状态指示说明。
- **更新说明**：弹出按版本组织的功能更新记录。
- **代理地址**：留空自动探测；显式填写则只探测该地址，不通则报错、不做任何修改（不回退）。
- **常见端口下拉**：把 `http://127.0.0.1:<端口>` 填入代理地址框。
- **CLI**：留空按候选目录自动定位（`%LOCALAPPDATA%\Programs\*.opencode*\resources\opencode-cli.exe` → `~/.opencode/bin` → PATH）；也可用「浏览」指定。
- **CA 证书**：企业 SSL 拦截场景填 PEM 路径，写入 `NODE_EXTRA_CA_CERTS`（对应 `--ca`）。
- **写入用户环境变量**：勾选后写 `HKCU\Environment` 并广播 `WM_SETTINGCHANGE`（对应不加 `--no-user-env`）。
- **写完重启后台服务**：对应不加 `--no-restart`。
- **仅演练**：只打印将要执行的命令，不写入、不重启、不验证（对应 `--dry-run`）。

### 参数对应关系

| GUI | Python 脚本 |
| --- | --- |
| 代理地址输入框 | `--proxy` |
| 仅探测按钮 | `--list` |
| 仅演练复选框 | `--dry-run` |
| 撤销代理按钮 | `--off` |
| 不勾「写入用户环境变量」 | `--no-user-env` |
| 不勾「写完重启后台服务」 | `--no-restart` |
| CA 证书输入框 | `--ca` |
| CLI 输入框 / 浏览 | `--cli` |

### 验证内容（三层，四项检查）

1. 配置层：`service get` 回读值 + `service status` 地址。
2. CLI → 服务：`api get /api/info` 连通（NO_PROXY 生效才会通）。
3. 服务 → 代理：出站隧道复测（对 `HTTPS_PROXY` 实际发起 CONNECT 探测）。

### 状态栏右下角指示

- **转发已配置 / 未配置**：启动时后台静默实测一次（回读 OpenCode 服务环境变量，CLI 缺失时查用户注册表代理键）；「开始配置 / 撤销代理」真实成功后即时更新，失败或中止后自动重测。
- **已运行 HH:MM:SS**：本次运行中「开始配置」真实成功后开始计时（仅演练不算），撤销后消失；关闭工具即清零，重新打开只显示状态、不显示时长。

### 日志脱敏

日志与表格中的代理 URL 一律脱敏：`user:pass@` 显示为 `***@`。`service get` 的完整输出（含凭据）不会出现在界面。

### 构建与运行

```bat
qmake opencode_proxy_tool.pro
mingw32-make
```

产物 `release/opencode_proxy_tool.exe`，同目录已备齐 `Qt5*.dll` 与 mingw 运行库，可直接双击。必须使用 32 位 MinGW 套件，工具链路径、已知坑与验证入口见 [AGENTS.md](AGENTS.md)。

### 目录结构

- `user/src/`、`user/inc/`：全部自研源码与头文件
- `icon/app_256.ico`：应用图标（OpenCode 原始 logo 叠加代理开关徽标，16/24/32/48/64/128/256 七档）
- `docs/`：`README.zh_CN.md` 更新记录、`implementation_plan.md` 实现方案
- `release/`：构建产物与运行依赖 DLL
- 根目录：`README.md` 工程介绍（中英双语）、`LICENSE` MIT 许可、`.pro` 构建脚本、`.rc` 资源脚本、`app.manifest` 进程清单

### 注意

- 桌面版 OpenCode 可能在运行：本工具重启的是 CLI 管理的服务，桌面版可能维护另一份进程 —— 配置完成后，建议完全退出桌面版再重开一次。
- 写用户环境变量只对新开的终端/应用生效，已开着的窗口需重开。
- 探测基于真实隧道握手（CONNECT www.gstatic.com:443 必须返回 200），端口开着但连不出去的不算可用。

## English

### Overview

A Windows GUI front-end for the `opencode_proxy.py` script: it probes for a working local proxy, writes the proxy environment variables into the OpenCode background service and the Windows user environment, restarts the service, then verifies the result. Every step — probing, each configuration write, and verification — is streamed to a live log panel.

### How it works

Per the official OpenCode [Network documentation](https://opencode.ai/docs/network/):

1. OpenCode has no proxy setting of its own; it only reads the standard environment variables `HTTPS_PROXY` / `HTTP_PROXY` / `ALL_PROXY` / `NO_PROXY`.
2. The background service is a separate process, so `export` in a shell only affects a service started from that shell. Persisting the values requires writing them into the service configuration: `opencode service set env KEY VALUE`.
3. Changing a service environment variable stops the running service, so it has to be brought back up with `opencode service restart`.
4. The CLI talks to the local service over `127.0.0.1`, so `NO_PROXY` must exclude loopback addresses (the tool always writes `localhost,127.0.0.1,::1,0.0.0.0`) — otherwise the tool's own local service would be routed through the proxy.

### Interface

- **Start**：detect a usable proxy → write service env vars → write user env vars → restart the service → three-layer verification (the script's default pipeline).
- **Detect only**：probe and list usable proxies, write nothing (`--list`).
- **Revert**：clear service and user environment variables (including the lowercase keys and `NODE_EXTRA_CA_CERTS`), restart according to the option (`--off`).
- **Stop**：request an abort of the pipeline. It takes effect only between steps; a step already running, e.g. `service restart`, cannot be interrupted.
- **Help**：opens the usage dialog — what the tool does, what each button and parameter means, and the status indicators in the bottom-right corner.
- **What's new**：opens the per-version change log.
- **Proxy address**：leave empty to auto-detect; set it explicitly to probe only that address — if unreachable, the tool reports an error and changes nothing (no fallback).
- **Common ports**：fills the address field with `http://127.0.0.1:<port>`.
- **CLI**：leave empty to auto-locate (`%LOCALAPPDATA%\Programs\*.opencode*\resources\opencode-cli.exe` → `~/.opencode/bin` → PATH), or pick the binary with Browse.
- **CA certificate**：for corporate SSL-interception setups, give a PEM path; it is written to `NODE_EXTRA_CA_CERTS` (`--ca`).
- **Write user environment variables**：writes `HKCU\Environment` and broadcasts `WM_SETTINGCHANGE` (the absence of `--no-user-env`).
- **Restart the background service after writing**：the absence of `--no-restart`.
- **Dry run**：print the commands only — no writes, no restart, no verification (`--dry-run`).

### Option mapping

| GUI | Script flag |
| --- | --- |
| Proxy address field | `--proxy` |
| Detect only button | `--list` |
| Dry run checkbox | `--dry-run` |
| Revert button | `--off` |
| "Write user env vars" unchecked | `--no-user-env` |
| "Restart service" unchecked | `--no-restart` |
| CA certificate field | `--ca` |
| CLI field / Browse | `--cli` |

### Verification (three layers, four checks)

1. Configuration layer: `service get` read-back plus the address reported by `service status`.
2. CLI → service: `api get /api/info` must succeed, which only works when `NO_PROXY` takes effect.
3. Service → proxy: an outbound CONNECT tunnel is re-tested against `HTTPS_PROXY`.

### Status indicators (bottom-right)

- **Forwarding configured / not configured**: measured silently in the background on startup (reads the OpenCode service env; falls back to the user-registry proxy keys when the CLI is missing). Updated right after a successful Start/Revert, re-measured after a failure or abort.
- **Uptime HH:MM:SS**: counts from the moment a real (non-dry-run) Start succeeds; disappears after a Revert. Reset when the tool closes — reopening shows only the configured state, not the elapsed time.

### Log redaction

Proxy URLs in the log and the status table are always redacted: `user:pass@` is shown as `***@`. The full `service get` output (which contains credentials) never reaches the UI.

### Build and run

```bat
qmake opencode_proxy_tool.pro
mingw32-make
```

The result is `release/opencode_proxy_tool.exe`, shipped next to the `Qt5*.dll` and MinGW runtime it needs, so it can be started by double-clicking. The 32-bit MinGW kit is required; toolchain paths, known pitfalls and manual test entry points are documented in [AGENTS.md](AGENTS.md).

### Repository layout

- `user/src/`, `user/inc/`：all first-party sources and headers
- `icon/app_256.ico`：application icon (the original OpenCode mark plus a proxy toggle badge, seven sizes from 16 to 256)
- `docs/`：`README.zh_CN.md` for the change log, `implementation_plan.md` for the design record
- `release/`：build output and runtime DLL dependencies
- repository root：`README.md` (this bilingual document), `LICENSE` (MIT), the `.pro` build script, the `.rc` resource script, `app.manifest`

### Notes

- A running OpenCode desktop app may keep its own service process: this tool restarts the service managed by the CLI, so fully quit and relaunch the desktop app after configuring.
- User environment variables only reach newly started terminals and applications; already-open windows need reopening.
- Availability is decided by a real tunnel handshake (CONNECT to www.gstatic.com:443 must return 200); a listening port that cannot reach the internet does not count as usable.

## 许可 / License

本工具以 MIT 许可发布，全文见 [LICENSE](LICENSE)。
This tool is licensed under the MIT License — see [LICENSE](LICENSE) for details.

## 更新记录

见 [docs/README.zh_CN.md](docs/README.zh_CN.md)。
