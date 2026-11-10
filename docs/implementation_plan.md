# OpenCode 代理配置工具 — 实现计划

**Goal**: 用 Qt 5.12.8 C++17 构建一个独立桌面工具，把 `D:\swf\dsh_ws\opencode_proxy.py` 的功能完整移植为图形界面：自动探测本地代理、把代理环境变量写入 OpenCode 后台服务与用户环境、重启服务、三层验证，全程通过日志面板实时输出每一处探测过程、每个配置项的写入状态与验证结果。界面布局、交互、配色完全复刻 `E:\code\qt\deepseek_harness_plugin_updater`。

**Architecture**: 主窗口 `MainWindow` 驱动 `ProxyManager`（后台线程 + QProcess 执行 CLI 命令），上半区为配置表单 + 配置项状态表格，下半区为日志面板，状态栏放摘要与进度。所有对 OpenCode 的操作经 `opencode-cli.exe` 子进程实现，代理探测用原生 Qt `QTcpSocket` 完成（不依赖外部可执行文件）。

**Tech Stack**: Qt 5.12.8 (core gui widgets network) + MinGW 7.3 32-bit + C++17 + qmake + Fusion style + QSS 浅色主题。

For agentic workers:
- Write one `[ ]` checkbox task per step, then mark `[x]` when done.
- Read complete files before editing, use `edit` for targeted changes.
- After every code change, bump `appinfo.h`：`APP_VERSION` 末三位 +1（如 `SV01.000` → `SV01.001`），`APP_VERSION_NUM` 末位同步 +1，`APP_BUILD_DATE` 改为当天（`yyMMdd`）；然后按「构建环境」一节的命令证明构建通过。
- The project lives at `E:\code\qt\opencode_proxy_tool\`.

---

## 构建环境（纯 cmd，每次构建前设置）

- 必须在**纯 cmd** 中运行 qmake 和 mingw32-make。不要在 Git Bash / MSYS2 中执行：其中的 `sh.exe` 会让 qmake 把删除命令写成 `rm -f`，生成的 Makefile 在 Windows 上无法 clean。
- 必须使用 32 位 `mingw730_32`（与 Qt 5.12.8 `mingw73_32` 配套）。**不要用** `D:\sw\mingw64\bin\mingw32-make.exe`：它配套的是 64 位 GCC 8.1.0，链接时会报 `undefined reference` 与 `WinMain`。
- PATH 只保留 Qt 与 `mingw730_32` 的 bin 目录和系统目录，让 Makefile 中的 `g++` 解析到 i686 版本。
- 也可以用 Qt Creator 直接打开 `opencode_proxy_tool.pro` 构建。

```cmd
set PATH=D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin;D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
cd /d E:\code\qt\opencode_proxy_tool
D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin\qmake.exe opencode_proxy_tool.pro -spec win32-g++
D:\SW\Qt5.12.8\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

---

## 任务 0 — 功能映射（Python → C++）

`opencode_proxy.py` 的每个步骤与参数在 C++ 端的落点：

| Python | C++ 落点 |
| --- | --- |
| `find_cli()` | `ProxyManager::find_cli()`：候选顺序见 3.2 步骤 1 |
| `probe_http_proxy()` | `ProxyDetector::probe_http()`：`QTcpSocket` 发 `CONNECT www.gstatic.com:443` 看 200 |
| `probe_socks5_proxy()` | `ProxyDetector::probe_socks5()`：SOCKS5 no-auth 握手 + CONNECT |
| `listening_loopback_ports()` | `ProxyDetector::listening_ports()`：`netstat -ano` 解析，地址范围与原脚本一致（含 `0.0.0.0`、`[::]`） |
| `detect_proxies()` | `ProxyDetector::detect()`：候选顺序与原脚本一致；显式代理只探测它，不回退 |
| `service set env` | `ProxyManager::svc_set_env()`：QProcess 跑 `service set env KEY VALUE` |
| `service get` | `ProxyManager::svc_get_env()`：QProcess 跑 `service get`，JSON 回读（顶层 `env` 对象） |
| 用户环境变量 | `ProxyManager::set_user_env()`：Win32 API 写 `HKCU\Environment` 并广播 `WM_SETTINGCHANGE`（不用 `reg.exe`） |
| `service restart` | `ProxyManager::restart_service()`：`service restart`，失败降级 `stop`+`start` |
| 三层验证 | `ProxyManager::verify()`：`service get` + `service status` + `api get /api/info` + 出站隧道复测（共四项检查，见 5.3） |
| `--off` 撤销 | `ProxyManager::unset_all()`：`service unset env` + 删除 `HKCU\Environment` 中的键（含小写键）+ 重启 |
| `--list` | `ProxyManager::probe_only()`：只探测，不写入 |
| `--ca` / `--no-user-env` / `--no-restart` / `--dry-run` | 主界面选项，对应参数 `caPath` / `userEnv` / `restart` / `dryRun`（见 4.1） |
| `--cli` | 「浏览」按钮 → `ProxyManager::set_cli()` |

---

## 任务 1 — 工程骨架 → 空窗口可编译

### [x] 1.1 创建工程文件 `opencode_proxy_tool.pro`

路径: `E:\code\qt\opencode_proxy_tool\opencode_proxy_tool.pro`

```pro
QT       += core gui widgets network
TARGET    = opencode_proxy_tool
TEMPLATE  = app
CONFIG   += c++17
CONFIG   -= debug debug_and_release
CONFIG   += release
DESTDIR     = release
OBJECTS_DIR = release
MOC_DIR     = release
RCC_DIR     = release
UI_DIR      = release
QMAKE_DEL_FILE = del 2>nul

SOURCES += \
    user/src/main.cpp \
    user/src/mainwindow.cpp \
    user/src/proxymanager.cpp \
    user/src/proxydetector.cpp

HEADERS += \
    user/inc/mainwindow.h \
    user/inc/proxymanager.h \
    user/inc/proxydetector.h \
    user/inc/appinfo.h

INCLUDEPATH += user/inc
RC_FILE += opencode_proxy_tool_resource.rc
RC_INCLUDEPATH += user/inc
QMAKE_PRE_LINK += $$QMAKE_RC -i $$RC_FILE -o $(RES_FILE) --include-dir=$$PWD --include-dir=$$PWD/user/inc
```

### [x] 1.2 创建 `user/inc/appinfo.h`

复用参考工程的宏结构，仅改内容：

```cpp
#define APP_NAME              "OpenCode 代理配置工具"
#define APP_ORIGINAL_FILENAME "opencode_proxy_tool.exe"
#define APP_AUTHOR_NAME       "lch"
#define APP_AUTHOR_MAIL       "fantasylch@foxmail.com"
#define APP_COPYRIGHT_YEAR    "2026"
#define APP_VERSION           "SV01.000"
#define APP_VERSION_NUM       1,0,0,0
#define APP_BUILD_DATE        "261110"
#define APP_FULL_TITLE        APP_NAME " " APP_VERSION " [" APP_BUILD_DATE "]"
#define APP_PRODUCT_VERSION   APP_VERSION " [" APP_BUILD_DATE "]"
```

### [x] 1.3 图标与资源 `opencode_proxy_tool_resource.rc`

1. 先复制图标。本工程 `icon\` 目前为空，不复制则 windres 会失败：`E:\code\qt\deepseek_harness_plugin_updater\icon\app_256.ico` → `E:\code\qt\opencode_proxy_tool\icon\app_256.ico`
2. .rc 结构参考工程，依次为：`#include <windows.h>`、`#include "user/inc/appinfo.h"`（VERSIONINFO 的宏需要它）、`IDI_ICON1 ICON DISCARDABLE "icon\\app_256.ico"`、`1 24 "app.manifest"`、`VS_VERSION_INFO` 块（字段全部取 appinfo.h 的宏）。
3. manifest 声明 `asInvoker`，关闭 Windows 的「安装程序检测」。

### [x] 1.4 创建 `app.manifest`

同参考工程，`requestedExecutionLevel level="asInvoker"` + 兼容段。

### [x] 1.5 创建 `user/src/main.cpp` + 最小 `mainwindow.{h,cpp}`

`main.cpp`：`QApplication` + `Fusion` + 注册跨线程信号类型 + `MainWindow w; w.show()`。注册语句（需 `#include "proxydetector.h"`）：

```cpp
qRegisterMetaType<QList<ProxyDetector::Proxy>>("QList<ProxyDetector::Proxy>");
```

最小主窗口：标题 `APP_FULL_TITLE`，`resize(1180, 720)`，中央一个占位 QLabel「界面待实现」。

**验收**：qmake + mingw32-make 产出 `release/opencode_proxy_tool.exe`，可运行出空窗口。

---

## 任务 2 — 代理探测器 `ProxyDetector`

### [x] 2.1 `user/inc/proxydetector.h` — 接口

```cpp
class ProxyDetector : public QObject {
    Q_OBJECT
public:
    struct Proxy {
        QString url;    // 完整 URL，如 http://127.0.0.1:7890
        QString scheme; // http / https / socks5
        QString host;
        int port;
    };
    explicit ProxyDetector(QObject *parent = nullptr);
    static bool probe(const QString &scheme, const QString &host, int port, int timeoutMs = 5000); // 阻塞，仅在工作线程调用
    static QList<int> listening_ports(int timeoutMs = 20000); // netstat -ano 解析，阻塞
    static QString mask_url(const QString &url); // 日志用：user:pass@ 替换为 ***@
    QList<Proxy> detect(const QString &explicitUrl, const QList<int> &livePorts); // 返回通过者列表，首个为选用项
};
Q_DECLARE_METATYPE(ProxyDetector::Proxy) // 跨线程信号需要，见 1.5
```

### [x] 2.2 `user/src/proxydetector.cpp` — 实现

- `probe_http`：`QTcpSocket::connectToHost`，发 `CONNECT www.gstatic.com:443 HTTP/1.1`，读首行 `HTTP/1.x 200`。
- `probe_socks5`：发 `\x05\x01\x00` 等 `\x05\x00`，再发 CONNECT 域名字节，读 `\x05\x00` 响应。
- `listening_ports`：`QProcess` 跑 `netstat -ano -p TCP`（在工作线程内同步等待）。逐行用正则 `^\s*TCP\s+(\S+):(\d+)\s+\S+\s+LISTENING`（忽略大小写）解析，地址只接受 `127.0.0.1`、`0.0.0.0`、`[::]`、`[::1]`、`::1`（与原脚本一致），去重保序。
- `mask_url`：若含 `user[:pass]@`，替换为 `***@`，其余原样返回。
- `detect`：URL 解析规则与原脚本 `parse_proxy` 一致（无 scheme 按 http；缺省端口 socks 为 1080、其它为 8080；解析失败视为不通过），同一 `(scheme, host, port)` 只探测一次。
  - 若 `explicitUrl` 非空：**只探测它**。通过则返回它；不通过则返回空列表，**不回退**到自动探测（与原脚本一致）。
  - 否则依次：环境变量候选（`HTTPS_PROXY`、`https_proxy`、`ALL_PROXY`、`all_proxy`、`HTTP_PROXY`、`http_proxy`，全部探测）→ `livePorts` 中属于常见端口的（`7890,7897,7891,10809,10808,1080,1087,8889,8080,8888,20171,2080,4780,6152,7892,10801,8118`，按此顺序，首个通过即停）→ `livePorts` 中其它 1024–65535 的端口（首个通过即停）→ 以上都不通过时，盲探常见端口（首个通过即停）。
  - 返回所有通过者，首个为选用项，其余为备选。

**验收**：写一个临时 main 打印探测结果，或由任务 5 的 UI 验证。

---

## 任务 3 — 代理管理器 `ProxyManager`

### [x] 3.1 `user/inc/proxymanager.h` — 接口与信号

```cpp
class ProxyManager : public QObject {
    Q_OBJECT
public:
    explicit ProxyManager(QObject *parent = nullptr);
    void set_cli(const QString &path);
    QString cli() const;
    void request_abort(); // 直接调用（不排队），只置 std::atomic<bool> 标志

public slots: // UI 以队列连接调用，保证在工作线程执行
    void detect_and_apply(const QString &explicitUrl, const QString &caPath,
                          bool userEnv, bool restart, bool dryRun); // caPath 为空则不写 NODE_EXTRA_CA_CERTS
    void probe_only(const QString &explicitUrl); // 对应 --list
    void unset_all(bool restart, bool dryRun);   // 对应 --off

signals:
    void log_message(const QString &text, const QColor &color);
    void detected(const QList<ProxyDetector::Proxy> &proxies); // 元类型见 1.5 / 2.1
    void env_state_changed(const QString &key, const QString &value, bool ok); // 每个配置项写入状态
    void verify_result(const QString &name, bool ok, const QString &detail);
    void finished(bool ok);
};
```

### [x] 3.2 `user/src/proxymanager.cpp` — 线程模型与串行流水线

**线程模型（只用这一种）**：

- `ProxyManager` 用 `QThread` + `moveToThread` 放入工作线程；工作线程保持事件循环（`QThread::start()` 默认即是），以便接收队列调用。
- `detect_and_apply` / `probe_only` / `unset_all` 在工作线程中**同步线性**执行：`QProcess` 在函数内局部创建，`start()` 后 `waitForFinished(超时)` 等待；`probe` / `listening_ports` 直接同步调用。不使用 lambda 串联。
- UI 线程只接收信号，不调用任何阻塞函数（`probe`、`listening_ports`、`waitForFinished`）。
- `request_abort()` 置标志，流水线每一步开始前检查，命中则 `finished(false)` 退出。
- **局限（需在界面上说明）**：停止只在步骤之间生效。`service restart`（超时 120 秒）与单次探测（超时 5 秒）期间不可中断。

**流水线**（按顺序执行，每步都 `log_message`；写入类步骤额外发 `env_state_changed`）：

1. `find_cli()`：按顺序探测 `OPENCODE_CLI` 环境变量 → `%LOCALAPPDATA%\Programs\*.opencode*\resources\opencode-cli.exe` 与 `…\*.opencode*\opencode-cli.exe` → `~/.opencode/bin/opencode-cli.exe` 与 `opencode.exe` → `%APPDATA%\npm\opencode.cmd`（`.cmd` 经 `cmd /d /c` 包装执行）→ PATH 上的 `opencode-cli` / `opencode`。找到后执行 `--version` 写入日志。
2. `detect()`：
   - 指定了显式代理：只探测它，不调用 `listening_ports()`。不通过则 `bad("显式代理不可用，未做任何修改")` 并结束。
   - 未指定：先调用 `listening_ports()`，再交给 `detect()`。每个候选都输出「探测中 / 通过 / 失败」到日志（URL 经 `mask_url()`）。
   - 首个通过者为选用项，其余作为备选输出。没有可用代理则 `bad("没找到可用代理，未做任何修改")` 并结束。
   - 通过 `detected(...)` 上报结果；`probe_only` 到此结束。
3. 校验：若指定了 CA 路径，先确认文件存在，否则 `bad(...)` 并结束。
4. 写服务环境：`service set env HTTPS_PROXY / HTTP_PROXY / ALL_PROXY / NO_PROXY`（有 CA 再加 `NODE_EXTRA_CA_CERTS`）。`NO_PROXY` 固定为 `localhost,127.0.0.1,::1,0.0.0.0`（与原脚本一致）。每个键一条 `env_state_changed`。注意：修改托管环境变量会停止正在运行的服务（官方文档），因此写入之后必须重启，失败处理见步骤 6。
5. 写用户环境（`userEnv` 为真时）：用 Win32 API（`RegOpenKeyExW` / `RegSetValueExW`，REG_SZ）写 `HKCU\Environment`，写完用 `SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment")` 广播。键与步骤 4 相同。**不用 `reg.exe`**：它不广播，新启动的程序可能拿到旧值。若链接报 `undefined reference`，在 `.pro` 中补 `LIBS += -ladvapi32 -luser32`。
6. 重启（`restart` 为真时）：`service restart`，失败则降级为 `service stop` + `service start`。仍失败时在日志与状态栏给出红色错误和提示（「后台服务未能启动，请手动执行 `opencode service start`」），然后继续验证以便定位。
7. 验证（`verify()`，三层、共四项检查）：`service get` JSON 回读（`env` 中的键与值应与写入一致）→ `service status` → `api get /api/info`（CLI 与服务直连）→ 出站 `probe` 复测。每项发 `verify_result`。
8. `finished(ok)`：任一步骤失败（写入、重启、验证）则 `ok` 为 false。

`dryRun` 为真时，步骤 4–7 只打印将要执行的命令，不实际写入、重启或验证；步骤 1–3 仍真实执行。

**验收**：配合任务 5 的 UI，点「开始配置」能从日志看到完整流水线。

---

## 任务 4 — 主窗口 `MainWindow`（UI 骨架 + 日志面板）

### [x] 4.1 布局（复刻参考工程）

```
┌ 工具栏卡片：开始配置 / 撤销代理 / 仅探测 / 停止   |  关于 / 退出 ┐
├ 表单行 1：代理地址（留空=自动探测）+ 常见端口下拉 + 浏览(opencode-cli) ┤
├ 表单行 2：CA 证书路径（可选，企业 SSL 拦截）+ 浏览 ┤
├ 选项行：☑ 写入用户环境变量  ☑ 写完重启后台服务  ☐ 仅演练（只打印命令，不写入） ┤
├ 状态表格卡片（QTableWidget）：配置项 | 目标 | 状态 | 说明 ┤
├ 日志面板卡片（QTextEdit，常驻可见，彩色分级）            ┤
├ 图例：●成功 ●提醒 ●错误 ●弱化                              ┤
└ 状态栏：摘要 + 进度条                                       ┘
```

- 上中下用 `QSplitter(Qt::Vertical)`，表格/日志都套 `panelCard` 白框。
- 日志颜色常量与参考工程一致：`kColorInfo 0x536174` / `kColorOk 0x159447` / `kColorWarn 0xd56a16` / `kColorErr red` / `kColorDim 0x8a93a3`。
- 常见端口下拉：选中后把 `http://127.0.0.1:<端口>` 填入代理地址框。
- 「撤销代理」同样读取「写完重启后台服务」「仅演练」两个选项。
- `append_log(text, color)`：先对 `text` 做 HTML 转义（`<` `>` `&`），再着色；上限 2000 块裁剪。

### [x] 4.2 状态表格列

`配置项 | 目标 | 状态 | 说明`，每写入一个环境变量/每完成一项验证就 `env_state_changed` / `verify_result` 驱动插入一行，状态列用颜色文字（成功/失败/进行中/跳过）。

### [x] 4.3 信号接线

把 ProxyManager 的 5 个信号接到 MainWindow 的槽，日志与表格实时刷新。按钮对 `detect_and_apply` / `probe_only` / `unset_all` 使用队列连接调用；「停止」直接调用 `request_abort()`（见 3.1）。

**验收**：窗口能展示、表格能插入行、日志能彩色滚动。

---

## 任务 5 — 功能闭环 + 全程日志

### [x] 5.1 探测过程实时日志

把 ProxyDetector 的每个候选端口探测做成可观测：`正在探测 http://127.0.0.1:7890 …` → `[OK] CONNECT 隧道通过` / `[FAIL] 连接被拒绝`，逐条进日志，而不是只报最终结果。日志由 `ProxyManager` 在调用 `probe` 前后输出（`probe` 是静态阻塞函数，不发信号），URL 一律经 `mask_url()`。

### [x] 5.2 每个配置项写入状态

`service set env` 的 4（或 5）个键、用户环境的 4（或 5）个键，逐个上报 `env_state_changed(key, value, ok)`，表格实时刷。

### [x] 5.3 验证结果（三层，共四项检查）

第一层（配置）：`service get` 回读值 + `service status` 地址；第二层（CLI → 服务）：`api get /api/info` 连通；第三层（服务 → 代理）：出站隧道复测。四项逐项 `verify_result(name, ok, detail)`，日志 + 表格双显示。

### [x] 5.4 撤销（--off 等价）

- 服务环境：`service unset env` 逐个清除（含 `NODE_EXTRA_CA_CERTS`）；对「不存在」的键视为成功。
- 用户环境：删除 `HKCU\Environment` 中 `ALL_ENV_KEYS`（含小写 `https_proxy` 等）以及 `NODE_EXTRA_CA_CERTS`；删除不存在的值视为成功（`ERROR_FILE_NOT_FOUND`）。原脚本撤销时不清理 `NODE_EXTRA_CA_CERTS`，此处补上。
- 按「写完重启后台服务」选项重启；支持「仅演练」。
- 完成后提示：已开着的终端需重开；桌面版需完全退出再开。
- 日志同样全程。

### [x] 5.5 停止 / 忙态互斥

运行中禁用所有按钮、启用「停止」。「停止」直接调用 `request_abort()`，流水线在下一步开始前退出；界面显示「正在停止…」，直到当前步骤（如 `service restart`）结束。窗口关闭时若流水线仍在运行：先 `request_abort()`，并等待当前步骤结束后再退出，不得直接销毁运行中的 QThread。

---

## 任务 6 — 收尾

### [x] 6.1 版本核对

`APP_VERSION` 末三位与 `APP_VERSION_NUM` 末位一致（如 `SV01.003` ↔ `1,0,0,3`），`APP_BUILD_DATE` 为构建当天（`yyMMdd`）。版本只递增、不回退（见「For agentic workers」）。

### [x] 6.2 完整构建验证

在工程根目录、纯 cmd 中执行（命令与「构建环境」一节一致）：

```cmd
set PATH=D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin;D:\SW\Qt5.12.8\Tools\mingw730_32\bin;C:\Windows\system32;C:\Windows
cd /d E:\code\qt\opencode_proxy_tool
D:\SW\Qt5.12.8\5.12.8\mingw73_32\bin\qmake.exe opencode_proxy_tool.pro -spec win32-g++
findstr /b DEL_FILE Makefile
D:\SW\Qt5.12.8\Tools\mingw730_32\bin\mingw32-make.exe -j4
```

- 不要在 `/tmp` 等外部目录构建：`.pro` 中 `DESTDIR = release` 是相对于构建目录的，exe 会落到外部目录。
- `findstr` 输出应为 `del`；若为 `rm -f`，说明 PATH 中混入了 `sh.exe`，清理 PATH 后重新运行 qmake。
- 产物：`release\opencode_proxy_tool.exe`。

### [x] 6.3 `docs/README.zh_CN.md` + 本计划文档

写清用法、原理（OpenCode 无 proxy 配置项、只认环境变量 + `service set env`、NO_PROXY 必须排除回环）、每个按钮的作用、参数对应关系（GUI 字段 ↔ Python 脚本参数，见任务 0 表格）、日志脱敏说明。

### [ ] 6.4 实机冒烟

- 在有代理监听（`127.0.0.1:7890`）的环境点「开始配置」，核对日志流水线与脚本输出一致；点「撤销代理」后 `service get` 回到空。
- 显式指定一个不通的代理地址再点「开始配置」：应报错且不修改任何配置（验证不回退）。
- 代理改为绑定 `0.0.0.0`（开启「允许局域网」）时，仍能被自动探测到。
- 勾选「仅演练」后点击「开始配置」：服务环境与用户环境保持不变，日志只出现将执行的命令。

---

## 关键约束（须遵守）

- Qt 5.12.8 不支持 `QRecursiveMutex`、`std::span` 等新 API；C++17 特性可用但避免过新。
- 中文一律 `QStringLiteral`，源码 UTF-8（带 BOM 或 qmake 指定 `CONFIG += utf8_source`）。
- Windows 下 QProcess 调 `.exe` 直接跑，不套 `cmd /c`；只有 `.cmd/.bat` 才需要 `cmd /d /c` 包装。
- 后台长任务只在工作线程中同步执行（见 3.2）；UI 线程不得调用 `probe`、`listening_ports`、`waitForFinished`。
- 日志与表格中出现的代理 URL 一律经 `ProxyDetector::mask_url()` 脱敏，不得输出 `user:pass`。
- 跨线程信号中的自定义类型必须注册元类型（见 1.5、2.1）。
- 构建只在纯 cmd 中进行，PATH 不含 `sh.exe`（见「构建环境」）。
- 桌面版 OpenCode 可能在运行：脚本重启的是 CLI 管理的服务，GUI 可能维护另一份进程 —— 界面要有提示文案。
