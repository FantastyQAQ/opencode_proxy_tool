/**
 * @file mainwindow.h
 * @brief 主窗口声明：工具栏 + 表单 + 状态表格/日志分栏 + 图例 + 状态栏
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QMainWindow>

#include "proxymanager.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QElapsedTimer;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTableWidget;
class QTextEdit;
class QTimer;
class QThread;
class QVBoxLayout;
class QWidget;

/// @brief 主窗口：驱动 ProxyManager，展示配置项状态表格与彩色日志（实现见 mainwindow.cpp）
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// @brief 构造函数：起工作线程、建界面、接信号
    explicit MainWindow(QWidget *parent = nullptr);
    /// @brief 析构函数：兜底停止工作线程
    ~MainWindow() override;

protected:
    /// @brief 关窗时若流水线还在跑，先请求中止并等待工作线程退出
    void closeEvent(QCloseEvent *event) override;

private slots:
    /// @brief 开始配置：探测 -> 写环境变量 -> 重启 -> 验证
    void on_start();
    /// @brief 仅探测：只列可用代理，不写任何配置
    void on_probe_only();
    /// @brief 撤销代理：清空服务与用户环境里的代理设置
    void on_unset();
    /// @brief 请求中止当前流水线（当前步骤结束后才停）
    void on_stop();
    /// @brief 弹出「关于」对话框
    void on_about();
    /// @brief 弹出「使用说明」对话框
    void on_help();
    /// @brief 弹出「更新说明」对话框
    void on_updates();
    /// @brief 关闭主窗口
    void on_exit();

    // ProxyManager 信号
    /// @brief 接收工作线程日志并落到日志面板
    void on_log(const QString &text, const QColor &color);
    /// @brief 展示探测到的可用代理（首个为选用项）
    void on_detected(const QList<ProxyDetector::Proxy> &proxies);
    /// @brief 展示单个配置项的写入结果
    void on_env_state(const QString &key, const QString &value, bool ok);
    /// @brief 展示单条验证结果
    void on_verify(const QString &name, bool ok, const QString &detail);
    /// @brief 流水线结束的统一收尾（更新指示、计时与摘要）
    void on_finished(bool ok);
    /// @brief 更新右下角「转发已配置/未配置」指示
    void on_proxy_state(bool configured);
    /// @brief 定时器回调：每秒刷新一次运行时长
    void on_run_timer();

private:
    /// @brief 搭建界面骨架并套用整套 QSS
    void setup_ui();
    /// @brief 建工具栏：四个动作按钮 + 右侧说明类按钮
    void setup_tool_bar();
    /// @brief 建参数表单（代理地址 / CA 证书 / 选项）
    void setup_form();
    /// @brief 建「配置项状态」表格面板并加入分栏
    void setup_table_panel(QSplitter *splitter);
    /// @brief 建日志面板并加入分栏
    void setup_log_panel(QSplitter *splitter);
    /// @brief 建彩色图例
    void setup_legend();
    /// @brief 建状态栏与运行时长定时器
    void setup_status_bar();
    /// @brief 往日志面板追加一行彩色文本
    void append_log(const QString &text, const QColor &color);
    /// @brief 切换忙态，互斥工具栏按钮
    void set_busy(bool busy);
    /// @brief 往状态表格追加一行
    void add_status_row(const QString &key, const QString &target, const QString &status,
                        const QColor &color, const QString &note);
    // ---- 右下角「转发状态 / 运行时长」指示 ----
    /// @brief 把右下角状态指示设为指定文案（已配置=绿 / 未配置=灰 / 检测中=橙）
    void show_proxy_state(const QString &text, const QColor &color);
    /// @brief 开始运行时长计时（仅本次进程有效）
    void start_run_time();
    /// @brief 停止计时并隐藏时长
    void stop_run_time();
    /// @brief 刷新「已运行」时长显示
    void refresh_run_time();
    /// @brief 让工作线程静默实测一次当前转发配置状态
    void trigger_state_check();
    // UI 成员
    QWidget *m_toolBarWidget = nullptr;  ///< 工具栏卡片容器
    QVBoxLayout *m_formLayout = nullptr; ///< 表单布局
    QLineEdit *m_proxyEdit = nullptr;   ///< 代理地址输入框
    QComboBox *m_portCombo = nullptr;   ///< 常见端口下拉
    QLineEdit *m_cliEdit = nullptr;     ///< opencode CLI 路径
    QLineEdit *m_caEdit = nullptr;      ///< CA 证书路径
    QCheckBox *m_userEnvCb = nullptr;   ///< 写用户环境变量
    QCheckBox *m_restartCb = nullptr;   ///< 写完重启后台服务
    QCheckBox *m_dryRunCb = nullptr;    ///< 仅演练

    QPushButton *m_startBtn = nullptr;   ///< 开始配置
    QPushButton *m_probeBtn = nullptr;   ///< 仅探测
    QPushButton *m_unsetBtn = nullptr;   ///< 撤销代理
    QPushButton *m_stopBtn = nullptr;    ///< 停止

    QTableWidget *m_table = nullptr;     ///< 状态表格
    QTextEdit *m_logEdit = nullptr;      ///< 日志面板
    QLabel *m_legend = nullptr;          ///< 图例
    QLabel *m_summaryLabel = nullptr;    ///< 状态栏摘要
    QProgressBar *m_progress = nullptr;  ///< 状态栏进度条

    QLabel *m_proxyStateLabel = nullptr; ///< 右下角「转发已配置/未配置」指示
    QLabel *m_runTimeLabel = nullptr;    ///< 右下角「已运行 HH:MM:SS」时长
    QTimer *m_runTimer = nullptr;        ///< 运行时长刷新定时器（1 秒一跳）
    QElapsedTimer *m_runElapsed = nullptr; ///< 运行时长起算时刻
    bool m_counting = false;             ///< 是否正在计时（配置成功且未撤销）
    bool m_lastDryRun = false;           ///< 最近一次「开始配置/撤销」是否仅演练

    /// @brief 最近一次触发流水线的入口（决定 on_finished 里指示/时长怎么更新）
    enum class LastAction {
        None,   ///< 尚未触发过任何操作
        Start,  ///< 开始配置
        Probe,  ///< 仅探测
        Unset   ///< 撤销代理
    };
    LastAction m_lastAction = LastAction::None;  ///< 当前入口（默认无）

    // 逻辑成员
    ProxyManager *m_manager = nullptr;   ///< 代理管理器（工作线程内）
    QThread *m_worker = nullptr;         ///< 工作线程
    bool m_busy = false;                 ///< 流水线进行中

    /// @brief 状态表格的列序号
    enum Col {
        ColKey = 0,   ///< 配置项
        ColTarget,    ///< 目标
        ColStatus,    ///< 状态
        ColNote,      ///< 说明
        ColCount
    };

    static constexpr int kMaxLogBlocks = 2000;  ///< 日志面板最大块数
};

#endif // MAINWINDOW_H
