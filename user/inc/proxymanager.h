/**
 * @file proxymanager.h
 * @brief 代理管理器声明：定位 CLI、探测代理、写环境变量、重启服务、三层验证
 */

#ifndef PROXYMANAGER_H
#define PROXYMANAGER_H

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

#include "proxydetector.h"

/// @brief 代理管理器：把 opencode_proxy.py 的主流程移植为 C++，整个对象跑在工作线程里
class ProxyManager : public QObject
{
    Q_OBJECT

public:
    /// @brief 构造函数：中止标志复位
    explicit ProxyManager(QObject *parent = nullptr);
    /// @brief 析构函数：无额外资源需要释放
    ~ProxyManager() override;

    /// @brief 显式指定 opencode 可执行文件路径（必须经队列调用 set_cli）
    Q_INVOKABLE void set_cli(const QString &path);

    /// @brief 当前 opencode 可执行文件路径（空串 = 尚未定位）
    QString cli() const;

    /// @brief 请求中止当前流水线（只置原子标志，不阻塞）
    void request_abort();

public slots:
    /// @brief 探测代理并写入配置（对应脚本默认主流程）
    void detect_and_apply(const QString &explicitUrl, const QString &caPath,
                          bool userEnv, bool restart, bool dryRun);

    /// @brief 只探测可用代理，不写入任何东西（对应 --list）
    void probe_only(const QString &explicitUrl);

    /// @brief 撤销全部代理设置（对应 --off）
    void unset_all(bool restart, bool dryRun);

    /// @brief 静默实测当前转发配置状态（结果经 proxy_state 发出）
    void check_state();

signals:
    /// @brief 日志消息（颜色由调用方给定）
    void log_message(const QString &text, const QColor &color);

    /// @brief 探测结果（首个为选用项，其余为备选）
    void detected(const QList<ProxyDetector::Proxy> &proxies);

    /// @brief 单个配置项的写入状态（value 已脱敏）
    void env_state_changed(const QString &key, const QString &value, bool ok);

    /// @brief 单条验证结果
    void verify_result(const QString &name, bool ok, const QString &detail);

    /// @brief 整条流水线结束（ok = 是否整体成功）
    void finished(bool ok);

    /// @brief check_state() 的实测结果
    void proxy_state(bool configured);

private:
    // ---- 流水线步骤 ----
    /// @brief 定位 opencode 可执行文件（verbose=false 时静默失败）
    bool find_cli(bool verbose = true);
    /// @brief 探测可用代理并逐条打印过程
    bool do_detect(const QString &explicitUrl, QList<ProxyDetector::Proxy> *found);
    /// @brief 把代理写进后台服务环境变量
    bool apply_service_env(const QString &proxyUrl, const QString &caPath, bool dryRun);
    /// @brief 把代理写进 Windows 用户环境变量
    bool apply_user_env(const QString &proxyUrl, const QString &caPath, bool dryRun);
    /// @brief 重启后台服务（restart 失败时回退 stop + start）
    bool do_restart(bool dryRun);
    /// @brief 三层验证并逐条上报结果
    void do_verify();

    // ---- CLI 命令执行 ----
    /// @brief 同步执行一条 opencode 命令并收集输出
    static bool run_cli(const QString &cli, const QStringList &args, int timeoutMs,
                        int *exitCode, QString *output);

    // ---- 用户环境变量（Win32 API，含 WM_SETTINGCHANGE 广播）----
    /// @brief 写一个用户环境变量并广播刷新
    static bool write_user_env(const QString &key, const QString &value);
    /// @brief 删除一个用户环境变量并广播刷新（本就不存在也算成功）
    static bool delete_user_env(const QString &key);

    /// @brief 只读查询一个用户环境变量（不广播、不修改）
    static bool read_user_env(const QString &key, QString *value);

    // ---- 辅助 ----
    /// @brief 查询是否已请求中止
    bool aborted() const;
    /// @brief 发一条带颜色的日志
    void log(const QString &text, const QColor &color);
    /// @brief 上报单个配置项的写入状态（表格 + 日志）
    void report_env(const QString &key, const QString &value, bool ok);

private:
    QString m_cli;                    ///< opencode 可执行文件路径
    std::atomic<bool> m_abort;        ///< 中止标志
    bool m_running = false;           ///< 流水线进行中（防重入）
};

#endif // PROXYMANAGER_H
