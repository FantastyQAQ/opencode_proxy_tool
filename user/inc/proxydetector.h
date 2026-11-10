/**
 * @file proxydetector.h
 * @brief 代理探测器声明：HTTP/SOCKS5 隧道探测、监听端口解析、候选代理发现
 */

#ifndef PROXYDETECTOR_H
#define PROXYDETECTOR_H

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <functional>

/// @brief 代理探测器：判断本地代理「真的能用」而不是端口开着就算（实现见 proxydetector.cpp）
class ProxyDetector : public QObject
{
    Q_OBJECT

public:
    /// @brief 一个可用代理的完整描述
    struct Proxy
    {
        QString url;    ///< 完整 URL（保留用户原始写法，如 http://127.0.0.1:7890）
        QString scheme; ///< http / https / socks5 / socks5h …（已转小写）
        QString host;   ///< 主机（已转小写）
        int port;       ///< 端口

        Proxy()
            : port(0)
        {}
    };

    /// @brief 构造函数（无额外初始化）
    explicit ProxyDetector(QObject *parent = nullptr);

    /// @brief 对一个代理做真实隧道探测（阻塞，只允许在工作线程调用）
    static bool probe(const QString &scheme, const QString &host, int port,
                       int timeoutMs = 5000);

    /// @brief 列出正在监听的本机回环端口（阻塞，netstat -ano -p TCP）
    static QList<int> listening_ports(int timeoutMs = 20000);

    /// @brief 把 URL 里的用户凭据替换为 ***（日志/表格展示用）
    static QString mask_url(const QString &url);

    /// @brief 解析代理 URL 为 Proxy 结构，失败返回空 Proxy
    static Proxy parse_url(const QString &url, bool *ok = nullptr);

    /// @brief 按原脚本 detect_proxies() 的候选顺序逐个真实探测
    QList<Proxy> detect(const QString &explicitUrl, const QList<int> &livePorts,
                        const std::function<void(const QString &, bool)> &onProbe);

private:
    /// @brief 去重后探测单个候选代理并回调结果
    void try_one(const QString &url, QList<Proxy> *tried, QList<Proxy> *good,
                 const std::function<void(const QString &, bool)> &onProbe);

    /// @brief HTTP/HTTPS 代理的 CONNECT 隧道探测
    static bool probe_http(const QString &host, int port, int timeoutMs);

    /// @brief SOCKS5 代理握手探测
    static bool probe_socks5(const QString &host, int port, int timeoutMs);
};

Q_DECLARE_METATYPE(ProxyDetector::Proxy)

#endif // PROXYDETECTOR_H
