/**
 * @file        proxymanager.cpp
 * @brief       代理管理器实现：定位 CLI、探测代理、写环境变量、重启服务、三层验证
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @warning     所有 QProcess 都在内部同步等待，只能在持有本对象的工作线程里调用。
 * @note        流程与原 Python 脚本一一对应：find_cli -> detect_proxies -> service set env -> 用户环境变量 -> service restart -> verify。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>代理管理器实现：定位 CLI、探测代理、写环境变量、重启服务、三层验证
 * </table>
 */

#include "proxymanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
/**
 * @brief       真正指向代理的三类键（不含 NO_PROXY）
 *
 */
const QStringList kProxyKeys = {QStringLiteral("HTTPS_PROXY"), QStringLiteral("HTTP_PROXY"),
                                QStringLiteral("ALL_PROXY")};
/**
 * @brief       撤销时要清理的全集（含小写键）
 *
 */
const QStringList kAllEnvKeys = {QStringLiteral("HTTPS_PROXY"), QStringLiteral("HTTP_PROXY"),
                                 QStringLiteral("ALL_PROXY"),   QStringLiteral("NO_PROXY"),
                                 QStringLiteral("https_proxy"), QStringLiteral("http_proxy"),
                                 QStringLiteral("all_proxy"),   QStringLiteral("no_proxy")};
const QStringList kEnvKeys = kProxyKeys + QStringList(QStringLiteral("NO_PROXY"));  ///< 写入与展示的环境变量键（顺序即展示顺序）
const QString kNoProxyValue = QStringLiteral("localhost,127.0.0.1,::1,0.0.0.0");  ///< 直连白名单，必须排除回环，否则 CLI 连本地服务也会走代理

const QColor kColorInfo(0x53, 0x61, 0x74);  ///< 常规信息灰
const QColor kColorOk(0x15, 0x94, 0x47);  ///< 成功绿
const QColor kColorWarn(0xd5, 0x6a, 0x16);  ///< 提醒橙
const QColor kColorErr(Qt::red);  ///< 错误红
const QColor kColorDim(0x8a, 0x93, 0xa3);  ///< 弱化灰
} // namespace

// ===== 构造 / 析构 =====

/**
 * @brief       构造代理管理器（中止标志复位）
 *
 * @param[in]   {parent} QObject* 父对象，一般为空
 * @return      none（构造/析构函数，无返回值）
 * @details     只把 parent 交给 QObject、把 m_abort 置 false。
 *              本对象随后由 MainWindow 通过 moveToThread 挪到工作线程，
 *              成员 m_running / m_abort 只在该线程内访问（m_abort 用 atomic，可从 UI 线程写）。
 * @doxygen_sig_fp a7ced5b1db0d767b
 */
ProxyManager::ProxyManager(QObject *parent)
    : QObject(parent)
    , m_abort(false)
{
}

/**
 * @brief       析构代理管理器（无额外资源需要释放）
 *
 * @return      none（构造/析构函数，无返回值）
 * @details     流水线里的 QProcess 都是函数内临时对象，工作线程由 MainWindow 持有并负责收尾，
 *              本类没有跨函数存活的系统资源，故用默认析构即可。
 * @doxygen_sig_fp a0248477d18ed623
 */
ProxyManager::~ProxyManager() = default;

// ===== 对外接口 =====

/**
 * @brief       记录用户指定的 opencode 可执行文件路径
 *
 * @param[in]   {path} const QString& 可执行文件路径（可空）
 * @return      none
 * @details     trim 后存入 m_cli；后续 find_cli() 会优先用它（前提是文件确实存在）。
 *              必须经 QMetaObject::invokeMethod 队列调用：与 check_state 和流水线同在
 *              工作线程的事件队列里按序执行，避免 UI 线程跨线程直写成员造成数据竞争。
 * @doxygen_sig_fp 818df55ee4c0c1b7
 */
void ProxyManager::set_cli(const QString &path)
{
    m_cli = path.trimmed();
}

/**
 * @brief       返回当前记录的 CLI 路径
 *
 * @return      路径；空串表示尚未定位到
 * @details     只读访问，UI 线程在构造主窗口时用它回填输入框，此时工作线程尚未改动 m_cli。
 * @doxygen_sig_fp 707ff90630bc40a6
 */
QString ProxyManager::cli() const
{
    return m_cli;
}

/**
 * @brief       请求中止当前流水线
 *
 * @return      none
 * @details     只置原子标志，不做任何阻塞动作，可从 UI 线程直接调用。
 *              流水线在每个步骤开始前检查 aborted()，命中就收尾并发出 finished(false)；
 *              当前正在执行的步骤（例如 service restart，最长 120 秒）无法中途打断。
 * @doxygen_sig_fp c5b4ffba0e193477
 */
void ProxyManager::request_abort()
{
    m_abort.store(true);
}

/**
 * @brief       查询是否已请求中止
 *
 * @return      true = 已请求中止
 * @details     原子读，供流水线各步骤在开始前判断是否要提前退出。
 * @doxygen_sig_fp 9f7cabb9147ca4b3
 */
bool ProxyManager::aborted() const
{
    return m_abort.load();
}

/**
 * @brief       静默实测当前转发配置状态
 *
 * @return      none
 * @details     不做 m_running 互斥：它和流水线同在工作线程的事件队列里，天然串行，不会并发。
 *              全程不写日志、不改任何配置，只回一个 bool：
 *              ① find_cli(false) 定位 CLI 后跑 service get（10 秒超时），解析 JSON 的 env 对象，
 *                 任一 kProxyKeys 非空即视为已配置；
 *              ② 未命中时兜底查 HKCU\Environment 里的代理键（CLI 缺失或服务没起的情况）。
 *              结果经 proxy_state(bool) 信号发回 UI 线程，UI 只据此刷新右下角指示。
 * @doxygen_sig_fp f7ff96412e152f44
 */
void ProxyManager::check_state()
{
    bool configured = false;

    if (find_cli(false)) {
        int code = -1;
        QString out;
        run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("get")}, 10000, &code, &out);
        if (code == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
            const QJsonObject env = doc.object().value(QStringLiteral("env")).toObject();
            for (const QString &key : kProxyKeys) {
                if (!env.value(key).toString().trimmed().isEmpty()) {
                    configured = true;
                    break;
                }
            }
        }
    }

    if (!configured) {
        for (const QString &key : kProxyKeys) {
            QString value;
            if (read_user_env(key, &value) && !value.trimmed().isEmpty()) {
                configured = true;
                break;
            }
        }
    }

    emit proxy_state(configured);
}

/**
 * @brief       发一条带颜色的日志
 *
 * @param[in]   {text} const QString& 日志正文
 * @param[in]   {color} const QColor& 日志颜色
 * @return      none
 * @details     只是 emit log_message 的薄封装，方便类内各处少写一次信号名。
 *              槽在 UI 线程（自动队列连接），跨线程调用是安全的。
 * @doxygen_sig_fp 2702d98ef5ca6d32
 */
void ProxyManager::log(const QString &text, const QColor &color)
{
    emit log_message(text, color);
}

/**
 * @brief       上报单个配置项的写入状态
 *
 * @param[in]   {key} const QString& 环境变量名
 * @param[in]   {value} const QString& 写入的值（应已脱敏）
 * @param[in]   {ok} bool 是否成功
 * @return      none
 * @details     同时做两件事：emit env_state_changed 让 UI 表格加一行，并打一条「  key -> value」日志
 *              （成功绿、失败红）。
 *              调用方负责先脱敏：服务环境里存的可能就是带凭据的代理 URL。
 * @doxygen_sig_fp fef9f1cd5d23f9c3
 */
void ProxyManager::report_env(const QString &key, const QString &value, bool ok)
{
    emit env_state_changed(key, value, ok);
    log(QStringLiteral("  %1 → %2").arg(key, value), ok ? kColorOk : kColorErr);
}

// ===== CLI 命令执行 =====

/**
 * @brief       同步执行一条 opencode 命令并收集输出
 *
 * @param[in]   {cli} const QString& 可执行文件路径
 * @param[in]   {args} const QStringList& 命令行参数
 * @param[in]   {timeoutMs} int 等待超时（毫秒）
 * @param[out]  {exitCode} int* 退出码输出，可为空
 * @param[out]  {output} QString* 合并输出，可为空
 * @return      true = 进程正常起停（退出码另取）
 * @details     只能在工作的那个线程里调用，内部 waitForStarted / waitForFinished 会阻塞。
 *              三类结局都归一成确定的约定，调用方只需看 exitCode：
 *              ① 启动失败：exitCode=127，output 为「无法启动 <cli>」；
 *              ② 超时：先 kill 再 waitForFinished(2000) 收尾，exitCode=124，output 为「超时」；
 *              ③ 正常结束：exitCode 取进程退出码，输出按 UTF-8 合并 stdout+stderr 后 trim。
 *              CLI 输出统一是 UTF-8（与原脚本 encoding="utf-8" 一致），不要改成 fromLocal8Bit。
 * @doxygen_sig_fp a29941b6e77914b3
 */
bool ProxyManager::run_cli(const QString &cli, const QStringList &args, int timeoutMs,
                           int *exitCode, QString *output)
{
    QProcess proc;
    proc.start(cli, args);
    if (!proc.waitForStarted(timeoutMs)) {
        if (exitCode)
            *exitCode = 127;
        if (output)
            *output = QStringLiteral("无法启动 ") + cli;
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        if (exitCode)
            *exitCode = 124;
        if (output)
            *output = QStringLiteral("超时");
        return false;
    }
    if (exitCode)
        *exitCode = proc.exitCode();
    if (output) {
        const QString out = QString::fromUtf8(proc.readAllStandardOutput());
        const QString err = QString::fromUtf8(proc.readAllStandardError());
        *output = (out + err).trimmed();
    }
    return true;
}

// ===== 流水线：探测 + 应用 =====

/**
 * @brief       仅探测可用代理，不写任何配置
 *
 * @param[in]   {explicitUrl} const QString& 显式代理地址（空 = 自动探测）
 * @return      none
 * @details     对应原脚本的 --list 入口。m_running 防重入、先清中止标志。
 *              find_cli 失败直接 finished(false)；探测到结果就打出「[OK] … CONNECT 隧道通过」与备选列表，
 *              并 emit detected 让表格展示；一个都没有时打 [FAIL] 与排查建议（代理软件是否开了 HTTP 端口等）。
 *              收尾 emit finished(!found.isEmpty())，UI 据 m_lastAction=Probe 不动右下角指示与计时。
 * @doxygen_sig_fp 2c4c0c1ce713f40a
 */
void ProxyManager::probe_only(const QString &explicitUrl)
{
    if (m_running)
        return;
    m_running = true;
    m_abort.store(false);

    log(QStringLiteral("=== 探测可用本地代理 ==="), kColorInfo);
    if (!find_cli()) {
        m_running = false;
        emit finished(false);
        return;
    }

    QList<ProxyDetector::Proxy> found;
    if (do_detect(explicitUrl, &found) && !found.isEmpty()) {
        const ProxyDetector::Proxy &p = found.first();
        log(QStringLiteral("  [OK] %1（%2://%3:%4）CONNECT 隧道通过")
                .arg(ProxyDetector::mask_url(p.url), p.scheme, p.host)
                .arg(p.port),
            kColorOk);
        for (int i = 1; i < found.size(); ++i)
            log(QStringLiteral("  备选 %1").arg(ProxyDetector::mask_url(found.at(i).url)),
                kColorDim);
        emit detected(found);
    } else {
        log(QStringLiteral("  [FAIL] 没找到可用代理。确认 Clash/v2rayN 等已开启 HTTP 端口，或显式指定代理地址"),
            kColorErr);
    }

    m_running = false;
    emit finished(!found.isEmpty());
}

/**
 * @brief       完整配置流水线：探测 -> 写服务环境 -> 写用户环境 -> 重启 -> 验证
 *
 * @param[in]   {explicitUrl} const QString& 显式代理地址（空 = 自动探测）
 * @param[in]   {caPath} const QString& CA 证书路径（空 = 不写）
 * @param[in]   {userEnv} bool 是否写用户环境变量
 * @param[in]   {restart} bool 是否重启后台服务
 * @param[in]   {dryRun} bool 仅演练，不写不重启不验证
 * @return      none
 * @details     七步流水线，对应原脚本的默认主流程：
 *              1. find_cli 定位 CLI，失败即结束——此时还没动过任何配置；
 *              2. do_detect 探测代理，没找到就提示排查并结束，同样不做任何修改；
 *              3. 校验 CA 文件：指定了但不存在则失败；随后检查中止标志；
 *              4. apply_service_env 写服务环境变量（这一步会停掉正在跑的服务）；
 *              5. userEnv 为真时 apply_user_env 写 HKCU 用户环境变量；
 *              6. restart 为真时 do_restart 重启服务；
 *              7. 非演练时 do_verify 做三层验证。
 *              4-7 步任一失败只把 ok 置 false 并继续后续步骤，尽量把能做的做完，让用户一次看到全部失败项。
 *              结尾打印选用代理、直连白名单、撤销入口与「桌面版需重开」提示，再 emit finished(ok)。
 * @doxygen_sig_fp b30784829c1d4f67
 */
void ProxyManager::detect_and_apply(const QString &explicitUrl, const QString &caPath,
                                    bool userEnv, bool restart, bool dryRun)
{
    if (m_running)
        return;
    m_running = true;
    m_abort.store(false);
    bool ok = true;

    log(QStringLiteral("=== OpenCode 本地代理配置 ==="), kColorInfo);

    if (!find_cli()) {
        m_running = false;
        emit finished(false);
        return;
    }

    QList<ProxyDetector::Proxy> found;
    if (!do_detect(explicitUrl, &found) || found.isEmpty()) {
        log(QStringLiteral("  [FAIL] 没找到可用代理，未做任何修改。"), kColorErr);
        log(QStringLiteral("  排查：代理软件是否开启「HTTP 代理 / 允许局域网」；端口是否被占用；或用显式地址指定。"),
            kColorDim);
        m_running = false;
        emit finished(false);
        return;
    }
    const QString proxyUrl = found.first().url;
    log(QStringLiteral("  选用 %1").arg(ProxyDetector::mask_url(proxyUrl)), kColorOk);
    for (int i = 1; i < found.size(); ++i)
        log(QStringLiteral("  备选 %1").arg(ProxyDetector::mask_url(found.at(i).url)), kColorDim);
    emit detected(found);

    if (!caPath.trimmed().isEmpty() && !QFileInfo::exists(caPath.trimmed())) {
        log(QStringLiteral("  [FAIL] --ca 指定的文件不存在：%1").arg(caPath), kColorErr);
        m_running = false;
        emit finished(false);
        return;
    }

    if (aborted()) {
        m_running = false;
        emit finished(false);
        return;
    }

    log(QStringLiteral("=== 写入后台服务环境变量（持久化，重启后仍在）==="), kColorInfo);
    if (!apply_service_env(proxyUrl, caPath, dryRun))
        ok = false;

    if (userEnv) {
        log(QStringLiteral("=== 写入用户级环境变量（让 CLI 与其它新开进程也走代理）==="), kColorInfo);
        if (!apply_user_env(proxyUrl, caPath, dryRun))
            ok = false;
    }

    if (restart) {
        log(QStringLiteral("=== 重启后台服务使配置生效 ==="), kColorInfo);
        if (!do_restart(dryRun))
            ok = false;
    }

    if (!dryRun) {
        log(QStringLiteral("=== 验证 ==="), kColorInfo);
        do_verify();
    }

    log(QStringLiteral("=== 完成 ==="), kColorInfo);
    log(QStringLiteral("  代理：%1").arg(ProxyDetector::mask_url(proxyUrl)), kColorInfo);
    log(QStringLiteral("  直连白名单：%1").arg(kNoProxyValue), kColorInfo);
    log(QStringLiteral("  撤销：点「撤销代理」"), kColorInfo);
    log(QStringLiteral("  提示：桌面版 OpenCode 若已在运行，完全退出再重开一次，它拉起的服务才会继承新环境。"),
        kColorDim);

    m_running = false;
    emit finished(ok);
}

// ===== 定位 CLI =====

/**
 * @brief       定位 opencode 可执行文件
 *
 * @param[in]   {verbose} bool true = 输出定位日志
 * @return      true = 找到并已写入 m_cli
 * @details     候选顺序与原脚本 find_cli 一致，命中即写回 m_cli（并转成原生路径分隔符）：
 *              ① 用户用「浏览…」指定的 m_cli（文件存在才用）；
 *              ② OPENCODE_CLI 环境变量；
 *              ③ %LOCALAPPDATA%/Programs/ 下形如 *.opencode* 的目录里的 resources/opencode-cli.exe 与 opencode-cli.exe；
 *              ④ ~/.opencode/bin/opencode-cli.exe、opencode.exe；
 *              ⑤ %APPDATA%/npm/opencode.cmd；
 *              ⑥ PATH 上的 opencode-cli / opencode（对应 shutil.which）。
 *              verbose=false 时（check_state 调用）失败不刷日志，避免启动时反复提示。
 * @doxygen_sig_fp 4efe6dc5a0a30279
 */
bool ProxyManager::find_cli(bool verbose)
{
    if (verbose)
        log(QStringLiteral("定位 opencode CLI…"), kColorInfo);

    if (!m_cli.isEmpty() && QFileInfo::exists(m_cli)) {
        if (verbose)
            log(QStringLiteral("  [OK] %1").arg(m_cli), kColorOk);
        return true;
    }

    QStringList cands;

    const QString envCli = qEnvironmentVariable("OPENCODE_CLI");
    if (!envCli.isEmpty())
        cands << envCli;

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QDir base(QDir::fromNativeSeparators(localAppData) + QStringLiteral("/Programs"));
        if (base.exists()) {
            const QStringList dirs = base.entryList(QStringList() << QStringLiteral("*.opencode*"),
                                                   QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Name);
            for (const QString &d : dirs) {
                const QString root = base.filePath(d);
                cands << root + QStringLiteral("/resources/opencode-cli.exe");
                cands << root + QStringLiteral("/opencode-cli.exe");
            }
        }
    }

    const QString home = QDir::homePath();
    cands << home + QStringLiteral("/.opencode/bin/opencode-cli.exe");
    cands << home + QStringLiteral("/.opencode/bin/opencode.exe");
    cands << home + QStringLiteral("/AppData/Roaming/npm/opencode.cmd");

    for (const QString &c : cands) {
        if (QFileInfo::exists(c)) {
            m_cli = QDir::toNativeSeparators(c);
            if (verbose)
                log(QStringLiteral("  [OK] %1").arg(m_cli), kColorOk);
            return true;
        }
    }

    const QStringList names = {QStringLiteral("opencode-cli"), QStringLiteral("opencode")};
    for (const QString &name : names) {
        const QString p = QStandardPaths::findExecutable(name);
        if (!p.isEmpty()) {
            m_cli = p;
            if (verbose)
                log(QStringLiteral("  [OK] %1（PATH）").arg(m_cli), kColorOk);
            return true;
        }
    }

    if (verbose) {
        log(QStringLiteral("  [FAIL] 找不到 opencode CLI。请用「浏览」指定路径，例如："), kColorErr);
        log(QStringLiteral("    %LOCALAPPDATA%\\Programs\\ai.opencode.desktop\\resources\\opencode-cli.exe"),
            kColorDim);
    }
    return false;
}

// ===== 探测 =====

/**
 * @brief       探测可用代理并逐条打印过程
 *
 * @param[in]   {explicitUrl} const QString& 显式代理地址（空 = 自动探测）
 * @param[out]  {found} QList<ProxyDetector::Proxy>* 探测结果输出
 * @return      true = 至少找到一个可用代理
 * @details     只有自动探测（explicitUrl 为空）才先跑 listening_ports，并打印扫描动作与端口数量——
 *              显式指定地址时没必要扫 netstat。
 *              真正的候选推进交给 ProxyDetector::detect，回调里逐条打「正在探测 <脱敏URL> … [OK]/[FAIL]」，
 *              让用户能看到在试哪个端口、卡在哪一步。
 * @doxygen_sig_fp 0681ffb4679a9037
 */
bool ProxyManager::do_detect(const QString &explicitUrl, QList<ProxyDetector::Proxy> *found)
{
    log(QStringLiteral("探测可用本地代理…"), kColorInfo);

    ProxyDetector detector;
    QList<int> livePorts;

    if (explicitUrl.trimmed().isEmpty()) {
        log(QStringLiteral("  扫描本机监听端口（netstat）…"), kColorDim);
        livePorts = ProxyDetector::listening_ports();
        log(QStringLiteral("  发现 %1 个监听端口").arg(livePorts.size()), kColorDim);
    }

    *found = detector.detect(explicitUrl, livePorts,
        [this](const QString &url, bool pass) {
            log(QStringLiteral("  正在探测 %1 … %2")
                    .arg(ProxyDetector::mask_url(url),
                         pass ? QStringLiteral("[OK] CONNECT 隧道通过")
                              : QStringLiteral("[FAIL] 连接被拒绝")),
                pass ? kColorOk : kColorWarn);
        });

    return !found->isEmpty();
}

// ===== 写服务环境 =====

/**
 * @brief       把代理写进 OpenCode 后台服务环境变量
 *
 * @param[in]   {proxyUrl} const QString& 要写入的代理地址
 * @param[in]   {caPath} const QString& CA 证书路径（空 = 不写）
 * @param[in]   {dryRun} bool 仅演练：只打印命令
 * @return      true = 全部键写入成功
 * @details     组装 HTTPS_PROXY / HTTP_PROXY / ALL_PROXY = proxyUrl、NO_PROXY = kNoProxyValue，
 *              caPath 非空时再加 NODE_EXTRA_CA_CERTS。
 *              逐键执行 service set env <key> <value>（60 秒超时）；dryRun 只打印「[演练] service set env」。
 *              任一键失败置 good=false 但不中断循环，其余键仍然写完。
 *              NODE_EXTRA_CA_CERTS 不在 kEnvKeys 里，最后单独再跑一次。
 * @doxygen_sig_fp 171678f4652dd20f
 */
bool ProxyManager::apply_service_env(const QString &proxyUrl, const QString &caPath, bool dryRun)
{
    QHash<QString, QString> values;
    values.insert(QStringLiteral("HTTPS_PROXY"), proxyUrl);
    values.insert(QStringLiteral("HTTP_PROXY"), proxyUrl);
    values.insert(QStringLiteral("ALL_PROXY"), proxyUrl);
    values.insert(QStringLiteral("NO_PROXY"), kNoProxyValue);
    if (!caPath.trimmed().isEmpty())
        values.insert(QStringLiteral("NODE_EXTRA_CA_CERTS"), caPath.trimmed());

    bool good = true;
    for (const QString &key : kEnvKeys) {
        const QString value = values.value(key);
        if (dryRun) {
            log(QStringLiteral("  [演练] service set env %1 %2")
                    .arg(key, ProxyDetector::mask_url(value)),
                kColorDim);
            report_env(key, ProxyDetector::mask_url(value), true);
            continue;
        }

        int code = -1;
        QString out;
        run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("set"),
                        QStringLiteral("env"), key, value},
                60000, &code, &out);
        const bool okKey = (code == 0);
        if (!okKey) {
            log(QStringLiteral("  [FAIL] %1 写入失败：%2")
                    .arg(key, out.left(300)),
                kColorErr);
            good = false;
        }
        report_env(key, ProxyDetector::mask_url(value), okKey);
    }

    if (values.contains(QStringLiteral("NODE_EXTRA_CA_CERTS"))) {
        const QString key = QStringLiteral("NODE_EXTRA_CA_CERTS");
        const QString value = values.value(key);
        if (dryRun) {
            log(QStringLiteral("  [演练] service set env %1 %2").arg(key, value), kColorDim);
            report_env(key, value, true);
        } else {
            int code = -1;
            QString out;
            run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("set"),
                            QStringLiteral("env"), key, value},
                    60000, &code, &out);
            const bool okKey = (code == 0);
            if (!okKey) {
                log(QStringLiteral("  [FAIL] %1 写入失败：%2").arg(key, out.left(300)), kColorErr);
                good = false;
            }
            report_env(key, value, okKey);
        }
    }

    return good;
}

// ===== 写用户环境（Win32 API + 广播）=====

/**
 * @brief       写一个用户环境变量并广播刷新
 *
 * @param[in]   {key} const QString& 环境变量名
 * @param[in]   {value} const QString& 要写入的值
 * @return      true = 写入成功
 * @details     RegOpenKeyExW(HKCU\Environment, KEY_SET_VALUE) -> RegSetValueExW(REG_SZ) -> RegCloseKey，
 *              每一步失败都直接返回 false。
 *              写入成功后 SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, ..., L"Environment",
 *              SMTO_ABORTIFHUNG, 3000) 广播一次，新开的终端 / 应用就能直接继承，不必注销重登；
 *              已经打开的窗口不会重新读环境，需要用户重开。
 *              非 Windows 平台直接返回 false（本工具只在 Windows 上提供该能力）。
 * @doxygen_sig_fp 4e7a8ef00302cf60
 */
bool ProxyManager::write_user_env(const QString &key, const QString &value)
{
#ifdef Q_OS_WIN
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey)
        != ERROR_SUCCESS) {
        return false;
    }
    const LONG rc = RegSetValueExW(
        hKey, reinterpret_cast<LPCWSTR>(key.utf16()), 0, REG_SZ,
        reinterpret_cast<const BYTE *>(value.utf16()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS)
        return false;

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 3000, nullptr);
    return true;
#else
    Q_UNUSED(key);
    Q_UNUSED(value);
    return false;
#endif
}

/**
 * @brief       删除一个用户环境变量并广播刷新
 *
 * @param[in]   {key} const QString& 环境变量名
 * @return      true = 删除成功或本来就不存在
 * @details     与 write_user_env 同款拍子：RegOpenKeyExW(KEY_SET_VALUE | KEY_QUERY_VALUE) -> RegDeleteValueW。
 *              ERROR_FILE_NOT_FOUND 也当成功（撤销时大量键本来就没写过，不该算失败）。
 *              删除成功后同样广播 WM_SETTINGCHANGE。
 * @doxygen_sig_fp 6991a2755fcd0272
 */
bool ProxyManager::delete_user_env(const QString &key)
{
#ifdef Q_OS_WIN
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &hKey)
        != ERROR_SUCCESS) {
        return false;
    }
    const LONG rc = RegDeleteValueW(hKey, reinterpret_cast<LPCWSTR>(key.utf16()));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND)
        return false;

    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"),
                        SMTO_ABORTIFHUNG, 3000, nullptr);
    return true;
#else
    Q_UNUSED(key);
    return false;
#endif
}

/**
 * @brief       只读查询一个用户环境变量
 *
 * @param[in]   {key} const QString& 环境变量名
 * @param[out]  {value} QString* 读到的值输出，可为空
 * @return      true = 键存在且读取成功
 * @details     两步 RegQueryValueExW：先取类型与字节长度，再按长度取值。
 *              ① 键不存在（最常见）直接返回 false；
 *              ② 只认 REG_SZ / REG_EXPAND_SZ，其它类型视为不可用（REG_EXPAND_SZ 原样取出不展开，
 *                 不影响「是否非空」的判断）；
 *              ③ 缓冲区按 size+2 字节分配，多留 2 字节保证结尾 L'\0' 存在，再用 fromWCharArray 转出。
 *              只读：不写注册表、不广播，供 check_state 静默查询用。
 * @doxygen_sig_fp ec4c22d7352f1b31
 */
bool ProxyManager::read_user_env(const QString &key, QString *value)
{
#ifdef Q_OS_WIN
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &hKey)
        != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(hKey, reinterpret_cast<LPCWSTR>(key.utf16()),
                         nullptr, &type, nullptr, &size)
        != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(hKey);
        return false;
    }

    QByteArray buf(static_cast<int>(size) + 2, 0);
    const LONG rc = RegQueryValueExW(hKey, reinterpret_cast<LPCWSTR>(key.utf16()),
                                     nullptr, nullptr,
                                     reinterpret_cast<BYTE *>(buf.data()), &size);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS)
        return false;

    if (value)
        *value = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(buf.constData()));
    return true;
#else
    Q_UNUSED(key);
    Q_UNUSED(value);
    return false;
#endif
}

/**
 * @brief       把代理写进 Windows 用户环境变量
 *
 * @param[in]   {proxyUrl} const QString& 要写入的代理地址
 * @param[in]   {caPath} const QString& CA 证书路径（空 = 不写）
 * @param[in]   {dryRun} bool 仅演练：只打印命令
 * @return      true = 全部键写入成功
 * @details     键与值与 apply_service_env 完全一致（三类代理键 + NO_PROXY，caPath 非空再追加
 *              NODE_EXTRA_CA_CERTS），区别只是落到 HKCU\Environment 而不是服务里。
 *              逐键调用 write_user_env；dryRun 打「[演练] 写用户环境 k=v」，成功打
 *              「[OK] HKCU\Environment 已更新 k」；失败置 good=false 但继续写其余键。
 * @doxygen_sig_fp 94c9b9dd901a80cd
 */
bool ProxyManager::apply_user_env(const QString &proxyUrl, const QString &caPath, bool dryRun)
{
    QHash<QString, QString> values;
    values.insert(QStringLiteral("HTTPS_PROXY"), proxyUrl);
    values.insert(QStringLiteral("HTTP_PROXY"), proxyUrl);
    values.insert(QStringLiteral("ALL_PROXY"), proxyUrl);
    values.insert(QStringLiteral("NO_PROXY"), kNoProxyValue);
    if (!caPath.trimmed().isEmpty())
        values.insert(QStringLiteral("NODE_EXTRA_CA_CERTS"), caPath.trimmed());

    bool good = true;
    const QStringList keys = kEnvKeys + (caPath.trimmed().isEmpty()
                                             ? QStringList()
                                             : QStringList(QStringLiteral("NODE_EXTRA_CA_CERTS")));
    for (const QString &key : keys) {
        const QString value = values.value(key);
        if (dryRun) {
            log(QStringLiteral("  [演练] 写用户环境 %1=%2")
                    .arg(key, ProxyDetector::mask_url(value)),
                kColorDim);
            report_env(key, ProxyDetector::mask_url(value), true);
            continue;
        }
        const bool okKey = write_user_env(key, value);
        if (!okKey) {
            log(QStringLiteral("  [FAIL] 写用户环境变量 %1 失败").arg(key), kColorErr);
            good = false;
        } else {
            log(QStringLiteral("  [OK] HKCU\\Environment 已更新 %1").arg(key), kColorOk);
        }
        report_env(key, ProxyDetector::mask_url(value), okKey);
    }
    return good;
}

// ===== 重启 =====

/**
 * @brief       重启 OpenCode 后台服务
 *
 * @param[in]   {dryRun} bool 仅演练：只打印命令
 * @return      true = 服务已重新起来
 * @details     改完服务环境变量必须重启才生效，所以这一步不允许静默跳过。
 *              dryRun 只打印；否则先 service restart（120 秒超时）；
 *              restart 失败时回退成 stop（90 秒）+ start（120 秒）——老版本 CLI 的 restart 不稳，
 *              拆开执行成功率更高；两条路都失败才打 [FAIL] 并提示用户手动执行 opencode service start。
 * @doxygen_sig_fp 355ee452d1442ff8
 */
bool ProxyManager::do_restart(bool dryRun)
{
    if (dryRun) {
        log(QStringLiteral("  [演练] service restart"), kColorDim);
        return true;
    }

    int code = -1;
    QString out;
    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("restart")}, 120000, &code, &out);
    if (code == 0) {
        log(QStringLiteral("  [OK] 后台服务已重启"), kColorOk);
        return true;
    }

    log(QStringLiteral("  restart 失败，改用 stop + start"), kColorWarn);
    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("stop")}, 90000, &code, &out);
    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("start")}, 120000, &code, &out);
    if (code == 0) {
        log(QStringLiteral("  [OK] 后台服务已启动"), kColorOk);
        return true;
    }

    log(QStringLiteral("  [FAIL] 服务启动失败：%1").arg(out.left(400)), kColorErr);
    log(QStringLiteral("  → 后台服务未能启动，请手动执行 opencode service start"), kColorErr);
    return false;
}

// ===== 验证 =====

/**
 * @brief       三层验证并逐条上报结果
 *
 * @return      none
 * @details     配置写完后按四步实测，每条结果都经 verify_result(name, ok, detail) 发回 UI：
 *              ① service get（30 秒）回读配置：退出码非 0 / 不是 JSON / env 里没有任何目标键，分别上报失败；
 *                 成功时把 KEY=VALUE 拼成一行展示（值经 mask_url 脱敏）；
 *              ② service status（30 秒）取首行地址，证明服务进程在跑；
 *              ③ api get /api/info（45 秒）验证 CLI 与本地服务连通——NO_PROXY 写对才通，
 *                 失败通常说明回环地址被塞进了代理；
 *              ④ service get env HTTPS_PROXY（30 秒）取第一个非空行（跳过可能混入的 "{}" 提示），
 *                 parse_url 后用探测器复测出站隧道，确认代理本身此刻仍然可用。
 * @doxygen_sig_fp d02f996d72c831dd
 */
void ProxyManager::do_verify()
{
    int code = -1;
    QString out;
    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("get")}, 30000, &code, &out);
    if (code != 0) {
        emit verify_result(QStringLiteral("service get"), false, out.left(200));
    } else {
        const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
        if (doc.isObject()) {
            const QJsonObject env = doc.object().value(QStringLiteral("env")).toObject();
            QStringList shown;
            for (const QString &key : kEnvKeys) {
                if (env.contains(key)) {
                    shown << key + QStringLiteral("=")
                                 + ProxyDetector::mask_url(env.value(key).toString());
                }
            }
            if (!shown.isEmpty()) {
                emit verify_result(QStringLiteral("service get"), true, shown.join(QStringLiteral("  ")));
            } else {
                emit verify_result(QStringLiteral("service get"), false, QStringLiteral("service env 为空"));
            }
        } else {
            emit verify_result(QStringLiteral("service get"), false, QStringLiteral("回读不是 JSON"));
        }
    }

    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("status")}, 30000, &code, &out);
    if (code == 0 && !out.isEmpty())
        emit verify_result(QStringLiteral("service status"), true, out.split(QLatin1Char('\n')).first());
    else
        emit verify_result(QStringLiteral("service status"), false, QStringLiteral("无地址信息"));

    run_cli(m_cli, {QStringLiteral("api"), QStringLiteral("get"), QStringLiteral("/api/info")},
            45000, &code, &out);
    if (code == 0)
        emit verify_result(QStringLiteral("CLI → 本地服务"), true, QStringLiteral("连通（NO_PROXY 生效）"));
    else
        emit verify_result(QStringLiteral("CLI → 本地服务"), false,
                           QStringLiteral("返回 %1：%2（服务可能还没起好）").arg(code).arg(out.left(200)));

    run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("get"), QStringLiteral("env"),
                    QStringLiteral("HTTPS_PROXY")},
            30000, &code, &out);
    const QString proxyLine = [&out]() {
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (!t.isEmpty())
                return t;
        }
        return QString();
    }();
    if (code == 0 && !proxyLine.isEmpty() && proxyLine != QStringLiteral("{}")) {
        bool okParse = false;
        const ProxyDetector::Proxy p = ProxyDetector::parse_url(proxyLine, &okParse);
        if (okParse && ProxyDetector::probe(p.scheme, p.host, p.port))
            emit verify_result(QStringLiteral("出站隧道"), true,
                               QStringLiteral("可用：%1").arg(ProxyDetector::mask_url(p.url)));
        else
            emit verify_result(QStringLiteral("出站隧道"), false,
                               QStringLiteral("不可用：%1").arg(ProxyDetector::mask_url(proxyLine)));
    } else {
        emit verify_result(QStringLiteral("出站隧道"), false, QStringLiteral("未取到 HTTPS_PROXY"));
    }
}

// ===== 撤销 =====

/**
 * @brief       撤销全部代理设置，恢复直连
 *
 * @param[in]   {restart} bool 是否重启后台服务
 * @param[in]   {dryRun} bool 仅演练：只打印命令
 * @return      none
 * @details     对应原脚本的 --off 入口。m_running 防重入、先清中止标志，find_cli 失败即结束。
 *              1. 服务环境：kEnvKeys + NODE_EXTRA_CA_CERTS 逐键 service unset env（30 秒），
 *                 输出里含 not found / unknown 也算成功（本来就没写过）；其余失败打 [WARN] 并置 ok=false；
 *              2. 用户环境：kAllEnvKeys（含小写键）+ NODE_EXTRA_CA_CERTS 逐键 delete_user_env，
 *                 键不存在同样算成功；
 *              3. 需要时 do_restart 让清理生效。
 *              结尾提示已恢复直连，并提醒已开着的终端 / 桌面版需重开才会读到新环境。
 * @doxygen_sig_fp 621088f2d5ddf0e8
 */
void ProxyManager::unset_all(bool restart, bool dryRun)
{
    if (m_running)
        return;
    m_running = true;
    m_abort.store(false);
    bool ok = true;

    log(QStringLiteral("=== 撤销 OpenCode 代理设置 ==="), kColorInfo);

    if (!find_cli()) {
        m_running = false;
        emit finished(false);
        return;
    }

    const QStringList svcKeys = kEnvKeys + QStringList(QStringLiteral("NODE_EXTRA_CA_CERTS"));
    for (const QString &key : svcKeys) {
        if (dryRun) {
            log(QStringLiteral("  [演练] service unset env %1").arg(key), kColorDim);
            report_env(key, QString(), true);
            continue;
        }
        int code = -1;
        QString out;
        run_cli(m_cli, {QStringLiteral("service"), QStringLiteral("unset"),
                        QStringLiteral("env"), key},
                30000, &code, &out);
        const bool okKey = (code == 0) || out.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
                           || out.contains(QStringLiteral("unknown"), Qt::CaseInsensitive);
        if (!okKey) {
            log(QStringLiteral("  [WARN] %1 撤销返回 %2：%3").arg(key).arg(code).arg(out.left(200)),
                kColorWarn);
            ok = false;
        }
        report_env(key, QString(), okKey);
    }

    log(QStringLiteral("=== 清理用户环境变量 ==="), kColorInfo);
    const QStringList userKeys = kAllEnvKeys + QStringList(QStringLiteral("NODE_EXTRA_CA_CERTS"));
    for (const QString &key : userKeys) {
        if (dryRun) {
            log(QStringLiteral("  [演练] 删除用户环境 %1").arg(key), kColorDim);
            continue;
        }
        if (delete_user_env(key))
            log(QStringLiteral("  [OK] 已删除用户环境 %1").arg(key), kColorOk);
    }
    log(QStringLiteral("  用户环境变量已清理（已开着的终端/应用需重开）"), kColorDim);

    if (restart) {
        log(QStringLiteral("=== 重启后台服务 ==="), kColorInfo);
        if (!do_restart(dryRun))
            ok = false;
    }

    log(QStringLiteral("=== 完成 ==="), kColorInfo);
    log(QStringLiteral("  OpenCode 已恢复直连。若桌面版仍在跑，重启一次桌面版更稳妥。"), kColorInfo);

    m_running = false;
    emit finished(ok);
}