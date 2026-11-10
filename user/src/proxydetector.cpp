/**
 * @file        proxydetector.cpp
 * @brief       代理探测器实现：CONNECT 隧道探测 / 监听端口解析 / 候选发现
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        判定标准：只有真的把请求带出 www.gstatic.com:443 并拿到 200 才算可用，端口开着不算。
 * @note        握手协议与判定标准与原 Python 脚本 opencode_proxy.py 保持一致。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>代理探测器实现：CONNECT 隧道探测 / 监听端口解析 / 候选发现
 * </table>
 */

#include "proxydetector.h"

#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTcpSocket>
#include <QUrl>

namespace {
/**
 * @brief       常见本地代理端口（与原脚本 COMMON_PORTS 一致）
 *
 */
const QList<int> kCommonPorts = {7890, 7897, 7891, 10809, 10808, 1080, 1087, 8889,
                                 8080, 8888, 20171, 2080, 4780, 6152, 7892, 10801, 8118};
const char kProbeHost[] = "www.gstatic.com";  ///< 探测目标主机
const quint16 kProbePort = 443;  ///< 探测目标端口
} // namespace

/**
 * @brief       构造代理探测器（无额外初始化）
 *
 * @param[in]   {parent} QObject* 父对象，一般为空
 * @return      none（构造/析构函数，无返回值）
 * @details     只把 parent 交给 QObject。探测器本身不持有 socket 或状态，
 *              每次探测都在函数内部建临时 QTcpSocket，因此同一个对象可以反复调用。
 * @doxygen_sig_fp 966f5cebbf3fb21f
 */
ProxyDetector::ProxyDetector(QObject *parent)
    : QObject(parent)
{
}

// ===== URL 解析与脱敏 =====

/**
 * @brief       把代理 URL 解析成 Proxy 结构
 *
 * @param[in]   {url} const QString& 待解析的代理地址
 * @param[out]  {ok} bool* 解析是否成功，可为空
 * @return      解析结果；失败时返回各字段为默认值的空结构
 * @details     解析规则与原 Python 脚本 parse_proxy 一致：
 *              ① 先 trim，空串直接判失败；
 *              ② 没有「://」时按 http:// 补全，容忍用户只写 127.0.0.1:7890；
 *              ③ QUrl 解析后要求 host 非空，否则判失败；
 *              ④ scheme 与 host 统一转小写，便于后续按前缀分派；
 *              ⑤ 端口缺省时 socks* 取 1080，其余取 8080；
 *              ⑥ url 字段保留用户原始写法（trim 后），日志展示与回写都用它。
 * @doxygen_sig_fp d9f81bed351c451e
 */
ProxyDetector::Proxy ProxyDetector::parse_url(const QString &url, bool *ok)
{
    Proxy p;
    QString s = url.trimmed();
    if (s.isEmpty()) {
        if (ok)
            *ok = false;
        return p;
    }

    if (!s.contains(QStringLiteral("://")))
        s = QStringLiteral("http://") + s;

    const QUrl u(s);
    if (!u.isValid() || u.host().isEmpty()) {
        if (ok)
            *ok = false;
        return p;
    }

    p.host = u.host().toLower();
    p.scheme = (u.scheme().isEmpty() ? QStringLiteral("http") : u.scheme().toLower());
    if (u.port() >= 0) {
        p.port = u.port();
    } else {
        p.port = p.scheme.startsWith(QStringLiteral("socks")) ? 1080 : 8080;
    }
    p.url = url.trimmed();

    if (ok)
        *ok = true;
    return p;
}

/**
 * @brief       把 URL 里的用户凭据替换成 ***
 *
 * @param[in]   {url} const QString& 原始 URL
 * @return      脱敏后的 URL；没有凭据时原样返回
 * @details     用 QUrl 取 userInfo：为空说明没有 user:pass，直接返回原始字符串（保留用户写法）。
 *              否则按 scheme + "://" + "***@" + host[:port][path] 重新拼接。
 *              日志与状态表格里的代理地址统一走这里，避免把凭据写进日志文件或界面。
 * @doxygen_sig_fp c1d8f13f0edfbf25
 */
QString ProxyDetector::mask_url(const QString &url)
{
    QString s = url.trimmed();
    if (!s.contains(QStringLiteral("://")))
        s = QStringLiteral("http://") + s;

    const QUrl u(s);
    if (!u.isValid() || u.userInfo().isEmpty())
        return url;

    QString masked = u.scheme() + QStringLiteral("://***@") + u.host();
    if (u.port() >= 0)
        masked += QStringLiteral(":") + QString::number(u.port());
    if (!u.path().isEmpty())
        masked += u.path();
    return masked;
}

// ===== 隧道探测 =====

/**
 * @brief       HTTP/HTTPS 代理的 CONNECT 隧道探测
 *
 * @param[in]   {host} const QString& 代理主机
 * @param[in]   {port} int 代理端口
 * @param[in]   {timeoutMs} int 单步 socket 超时（毫秒）
 * @return      true = 代理确实把请求带出去了
 * @details     连上代理后发 CONNECT www.gstatic.com:443 HTTP/1.1，附 Host 与 Proxy-Connection: keep-alive。
 *              然后读到出现 \r\n 或累计 512 字节为止，取状态行用 ^HTTP/1\.[01]\s+200 判定。
 *              只有 200 才算隧道打通：端口开着但连不出去的代理会返回 407/502，或干脆超时，一律判失败。
 * @doxygen_sig_fp affc20791e204baa
 */
bool ProxyDetector::probe_http(const QString &host, int port, int timeoutMs)
{
    QTcpSocket s;
    s.connectToHost(host, static_cast<quint16>(port));
    if (!s.waitForConnected(timeoutMs))
        return false;

    const QByteArray req =
        "CONNECT " + QByteArray(kProbeHost) + ":443 HTTP/1.1\r\n"
        "Host: " + QByteArray(kProbeHost) + ":443\r\n"
        "Proxy-Connection: keep-alive\r\n\r\n";
    if (s.write(req) != req.size())
        return false;
    if (!s.waitForBytesWritten(timeoutMs))
        return false;

    QByteArray buf;
    while (!buf.contains("\r\n") && buf.size() < 512) {
        if (!s.waitForReadyRead(timeoutMs))
            return false;
        const QByteArray chunk = s.read(256);
        if (chunk.isEmpty())
            break;
        buf += chunk;
    }

    const int lineEnd = buf.indexOf("\r\n");
    const QByteArray head = lineEnd >= 0 ? buf.left(lineEnd) : buf;
    const QRegularExpression re(QStringLiteral("^HTTP/1\\.[01]\\s+200"));
    return re.match(QString::fromLatin1(head)).hasMatch();
}

/**
 * @brief       SOCKS5 代理握手探测
 *
 * @param[in]   {host} const QString& 代理主机
 * @param[in]   {port} int 代理端口
 * @param[in]   {timeoutMs} int 单步 socket 超时（毫秒）
 * @return      true = 握手与 CONNECT 都成功
 * @details     两步握手，任一环节不符即判失败：
 *              ① no-auth 协商：发 05 01 00，期望回 05 00；
 *              ② CONNECT 请求：VER=5、CMD=1、RSV=0、ATYP=3（域名），随后是域名长度 + 域名 + 端口（大端）；
 *              ③ 读 4 字节应答头，VER=0x05 且 REP=0x00 表示成功。
 * @doxygen_sig_fp 2d9c6412c6515a20
 */
bool ProxyDetector::probe_socks5(const QString &host, int port, int timeoutMs)
{
    QTcpSocket s;
    s.connectToHost(host, static_cast<quint16>(port));
    if (!s.waitForConnected(timeoutMs))
        return false;

    const QByteArray greeting = QByteArray::fromHex("050100");
    if (s.write(greeting) != greeting.size())
        return false;
    if (!s.waitForReadyRead(timeoutMs) || s.bytesAvailable() < 2)
        return false;
    if (s.read(2) != QByteArray::fromHex("0500"))
        return false;

    const QByteArray target(kProbeHost);
    QByteArray req;
    req += static_cast<char>(0x05);
    req += static_cast<char>(0x01);
    req += static_cast<char>(0x00);
    req += static_cast<char>(0x03);
    req += static_cast<char>(target.size());
    req += target;
    req += static_cast<char>((kProbePort >> 8) & 0xff);
    req += static_cast<char>(kProbePort & 0xff);
    if (s.write(req) != req.size())
        return false;
    if (!s.waitForReadyRead(timeoutMs) || s.bytesAvailable() < 4)
        return false;

    const QByteArray resp = s.read(4);
    return resp.size() == 4 && resp.at(0) == 0x05 && resp.at(1) == 0x00;
}

/**
 * @brief       按 scheme 分派到 HTTP 或 SOCKS5 探测
 *
 * @param[in]   {scheme} const QString& 协议（http / socks5 …）
 * @param[in]   {host} const QString& 代理主机
 * @param[in]   {port} int 代理端口
 * @param[in]   {timeoutMs} int 单步 socket 超时（毫秒）
 * @return      true = 隧道可用
 * @details     scheme 以 socks 开头（socks5 / socks5h / socks4）走 probe_socks5，其余按 HTTP CONNECT 处理。
 *              上层统一从这个入口探测，不必自己判断协议。
 * @doxygen_sig_fp b6ac7ac4ccf5693e
 */
bool ProxyDetector::probe(const QString &scheme, const QString &host, int port,
                           int timeoutMs)
{
    if (scheme.startsWith(QStringLiteral("socks")))
        return probe_socks5(host, port, timeoutMs);
    return probe_http(host, port, timeoutMs);
}

// ===== 监听端口 =====

/**
 * @brief       列出本机正在监听的端口
 *
 * @param[in]   {timeoutMs} int netstat 进程超时（毫秒）
 * @return      监听端口列表（已去重，保持首次出现顺序）
 * @details     执行 netstat -ano -p TCP，用正则 ^\s*TCP\s+(\S+):(\d+)\s+\S+\s+LISTENING 提取行
 *              （忽略大小写 + 多行模式）。
 *              只保留 127.0.0.1 / 0.0.0.0 / [::] / [::1] / ::1 上的端口，其余地址（真实网卡 IP）跳过，
 *              避免把局域网服务当成候选代理。
 *              用 QSet 去重并按出现顺序入列；进程起不来或超时返回空表，不报错、不阻塞调用方。
 * @doxygen_sig_fp 2b14de49f9452e76
 */
QList<int> ProxyDetector::listening_ports(int timeoutMs)
{
    QList<int> ports;

    QProcess proc;
    proc.start(QStringLiteral("netstat"), QStringList() << QStringLiteral("-ano")
                                                        << QStringLiteral("-p")
                                                        << QStringLiteral("TCP"));
    if (!proc.waitForStarted(timeoutMs))
        return ports;
    if (!proc.waitForFinished(timeoutMs))
        return ports;

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QRegularExpression re(QStringLiteral(
        "^\\s*TCP\\s+(\\S+):(\\d+)\\s+\\S+\\s+LISTENING"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

    QSet<int> seen;
    QRegularExpressionMatchIterator it = re.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString addr = m.captured(1);
        if (addr != QStringLiteral("127.0.0.1") && addr != QStringLiteral("0.0.0.0")
            && addr != QStringLiteral("[::]") && addr != QStringLiteral("[::1]")
            && addr != QStringLiteral("::1"))
            continue;

        const int port = m.captured(2).toInt();
        if (port > 0 && !seen.contains(port)) {
            seen.insert(port);
            ports.append(port);
        }
    }
    return ports;
}

// ===== 候选发现 =====

/**
 * @brief       去重后探测单个候选代理并回调结果
 *
 * @param[in]   {url} const QString& 待探测的代理 URL
 * @param[in,out] {tried} QList<Proxy>* 已探测的 (scheme,host,port) 集合
 * @param[out]  {good} QList<Proxy>* 通过者追加到这里
 * @param[in]   {onProbe} const std::function<void(const QString&, bool)>& 每个候选探测后的回调，可为空
 * @return      none
 * @details     流程：parse_url 失败直接返回 -> 若 (scheme, host, port) 已在 tried 中则跳过，避免
 *              环境变量与常见端口重复探测同一代理 -> probe() 实测 -> 回调 onProbe(url, 是否通过)
 *              供上层逐条打日志 -> 通过则追加到 good。
 * @doxygen_sig_fp 3ad85cc6b84cfb58
 */
void ProxyDetector::try_one(const QString &url, QList<Proxy> *tried, QList<Proxy> *good,
                            const std::function<void(const QString &, bool)> &onProbe)
{
    bool ok = false;
    const Proxy p = parse_url(url, &ok);
    if (!ok)
        return;

    for (const Proxy &t : *tried) {
        if (t.scheme == p.scheme && t.host == p.host && t.port == p.port)
            return;
    }
    tried->append(p);

    const bool pass = probe(p.scheme, p.host, p.port);
    if (onProbe)
        onProbe(p.url, pass);
    if (pass)
        good->append(p);
}

/**
 * @brief       按候选顺序逐个真实探测，返回可用代理
 *
 * @param[in]   {explicitUrl} const QString& 显式代理地址（空 = 自动探测）
 * @param[in]   {livePorts} const QList<int>& listening_ports() 的结果
 * @param[in]   {onProbe} const std::function<void(const QString&, bool)>& 每个候选探测后的回调，可为空
 * @return      全部通过探测的代理（首个为选用项，可能为空）
 * @details     显式地址：只探测它，不通就返回空，不回退到自动探测（用户指定了就不该被替换）。
 *              自动模式按四轮推进，每轮首个通过即停：
 *              ① 环境变量 HTTPS_PROXY / https_proxy / ALL_PROXY / all_proxy / HTTP_PROXY / http_proxy；
 *              ② kCommonPorts 与 livePorts 的交集，按常见端口顺序；
 *              ③ livePorts 里其余 1024-65535 的端口；
 *              ④ 以上都不通过时盲探 kCommonPorts（对应原脚本的最后兜底）。
 *              顺序与原 Python 脚本 detect_proxies 完全一致，保证探测结果可复现。
 * @doxygen_sig_fp fd68157937e34ddb
 */
QList<ProxyDetector::Proxy> ProxyDetector::detect(
    const QString &explicitUrl, const QList<int> &livePorts,
    const std::function<void(const QString &, bool)> &onProbe)
{
    QList<Proxy> tried;
    QList<Proxy> good;

    if (!explicitUrl.trimmed().isEmpty()) {
        try_one(explicitUrl, &tried, &good, onProbe);
        return good;
    }

    const QStringList envKeys = {QStringLiteral("HTTPS_PROXY"), QStringLiteral("https_proxy"),
                                 QStringLiteral("ALL_PROXY"),   QStringLiteral("all_proxy"),
                                 QStringLiteral("HTTP_PROXY"),  QStringLiteral("http_proxy")};
    for (const QString &key : envKeys) {
        const QString v = qEnvironmentVariable(key.toUtf8().constData());
        if (!v.isEmpty())
            try_one(v, &tried, &good, onProbe);
    }

    for (int p : kCommonPorts) {
        if (livePorts.contains(p)) {
            try_one(QStringLiteral("http://127.0.0.1:%1").arg(p), &tried, &good, onProbe);
            if (!good.isEmpty())
                return good;
        }
    }

    for (int p : livePorts) {
        if (p >= 1024 && p <= 65535 && !kCommonPorts.contains(p)) {
            try_one(QStringLiteral("http://127.0.0.1:%1").arg(p), &tried, &good, onProbe);
            if (!good.isEmpty())
                return good;
        }
    }

    for (int p : kCommonPorts) {
        try_one(QStringLiteral("http://127.0.0.1:%1").arg(p), &tried, &good, onProbe);
        if (!good.isEmpty())
            return good;
    }

    return good;
}