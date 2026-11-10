/**
 * @file        mainwindow.cpp
 * @brief       主窗口实现：界面搭建 / QSS 主题 / 信号接线 / 忙态互斥
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        界面上的所有结果都来自工作线程的 ProxyManager，靠队列连接回到 UI 线程。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>主窗口实现：界面搭建 / QSS 主题 / 信号接线 / 忙态互斥
 * </table>
 */

#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "appinfo.h"
#include "helpdialog.h"
#include "proxydetector.h"
#include "updatedialog.h"

// ===== 日志颜色常量（与参考工程一致的柔和色板）=====
namespace {
const QColor kColorInfo(0x53, 0x61, 0x74);  ///< 常规信息灰
const QColor kColorOk(0x15, 0x94, 0x47);  ///< 成功绿
const QColor kColorWarn(0xd5, 0x6a, 0x16);  ///< 提醒橙
const QColor kColorErr(Qt::red);  ///< 错误红
const QColor kColorDim(0x8a, 0x93, 0xa3);  ///< 弱化灰

/**
 * @brief       常见代理端口（下拉框用，顺序同原脚本 COMMON_PORTS）
 *
 */
const QList<int> kPortPresets = {7890, 7897, 7891, 10809, 10808, 1080, 1087, 8889,
                                 8080, 8888, 20171, 2080, 4780, 6152, 7892, 10801, 8118};
} // namespace

// ===== 构造 / 析构 =====

/**
 * @brief       构造主窗口：起工作线程、建界面、接信号
 *
 * @param[in]   {parent} QWidget* 父窗口，一般为空
 * @return      none（构造/析构函数，无返回值）
 * @details     构造顺序决定了启动时不会出现空指针：
 *              ① 标题取 APP_FULL_TITLE、图标取 :/icon/app_256.ico；
 *              ② new ProxyManager()（故意不给 parent）后 moveToThread(m_worker) 并启动工作线程，
 *                 UI 线程从此只通过信号拿结果；
 *              ③ setup_ui() 建界面，再连接 log_message / detected / env_state_changed / verify_result /
 *                 finished / proxy_state 六个信号，跨线程自动走队列连接；
 *              ④ CLI 输入框回填 m_manager->cli()；右下角先显示橙色「转发检测中…」再 trigger_state_check()
 *                 做首次实测；
 *              ⑤ 默认 1180x720，最小 860x520。
 * @doxygen_sig_fp 755a2b28c15df9d3
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral(APP_FULL_TITLE));
    setWindowIcon(QIcon(QStringLiteral(":/icon/app_256.ico")));

    m_manager = new ProxyManager();
    m_worker = new QThread(this);
    m_manager->moveToThread(m_worker);
    m_worker->start();

    setup_ui();

    connect(m_manager, &ProxyManager::log_message, this, &MainWindow::on_log);
    connect(m_manager, &ProxyManager::detected, this, &MainWindow::on_detected);
    connect(m_manager, &ProxyManager::env_state_changed, this, &MainWindow::on_env_state);
    connect(m_manager, &ProxyManager::verify_result, this, &MainWindow::on_verify);
    connect(m_manager, &ProxyManager::finished, this, &MainWindow::on_finished);
    connect(m_manager, &ProxyManager::proxy_state, this, &MainWindow::on_proxy_state);

    m_summaryLabel->setText(QStringLiteral("就绪 — 点击「开始配置」自动探测并写入代理"));
    m_cliEdit->setText(m_manager->cli());
    show_proxy_state(QStringLiteral("转发检测中…"), kColorWarn);
    trigger_state_check();

    resize(1180, 720);
    setMinimumSize(860, 520);
}

/**
 * @brief       析构时兜底停止工作线程
 *
 * @return      none（构造/析构函数，无返回值）
 * @details     正常关窗由 closeEvent 收尾，这里再兜一次底：线程仍在跑就 request_abort() -> quit() ->
 *              wait(10000)，超时 terminate()。
 *              顺序不能省：先让流水线有机会退出，再退事件循环，最后才强杀，避免 m_manager 被销毁时
 *              槽还在执行。
 * @doxygen_sig_fp a84ef10cd3716d34
 */
MainWindow::~MainWindow()
{
    if (m_worker && m_worker->isRunning()) {
        m_manager->request_abort();
        m_worker->quit();
        if (!m_worker->wait(10000))
            m_worker->terminate();
    }
}

// ===== UI 构建 =====

/**
 * @brief       搭建界面骨架并套用整套 QSS
 *
 * @return      none
 * @details     central 自上而下依次放：
 *              ① 工具栏卡片（setup_tool_bar）；
 *              ② 参数表单（setup_form）；
 *              ③ 竖向 QSplitter：上半「配置项状态」表格、下半「日志」，初始 280/240、两栏各占一半拉伸；
 *              ④ 彩色图例（setup_legend）；
 *              ⑤ 状态栏（setup_status_bar）。
 *              最后给 central 设一整套内联浅色 QSS：#f5f7fa 背景、#panelCard 白卡片圆角 6px、
 *              按钮/输入框/下拉框/表格/表头/日志/状态栏/进度条样式，主按钮 #primaryButton 蓝底白字。
 *              样式集中在这一处，改配色只需改这段字符串。
 * @doxygen_sig_fp 86fc58ce8be6edb3
 */
void MainWindow::setup_ui()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(central);

    auto *top = new QVBoxLayout(central);
    top->setContentsMargins(14, 10, 14, 8);
    top->setSpacing(10);

    setup_tool_bar();
    top->addWidget(m_toolBarWidget);

    setup_form();
    top->addLayout(m_formLayout);

    auto *splitter = new QSplitter(Qt::Vertical, central);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({280, 240});
    setup_table_panel(splitter);
    setup_log_panel(splitter);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    top->addWidget(splitter, 1);

    setup_legend();
    top->addWidget(m_legend);

    setup_status_bar();

    central->setStyleSheet(QStringLiteral(
        "#centralWidget { background: #f5f7fa; }"
        "#panelCard { background: #ffffff; border: 1px solid #c2ccdb; border-radius: 6px; }"

        "QLabel { color: #536174; }"
        "QLabel#fieldLabel, QLabel#panelTitle { color: #344256; font-weight: bold; }"

        "QPushButton {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  padding: 6px 13px;"
        "  color: #344256;"
        "}"
        "QPushButton:hover {"
        "  background: #f0f5ff;"
        "  border-color: #82a9e8;"
        "  color: #2166c1;"
        "}"
        "QPushButton:pressed { background: #e4efff; }"
        "QPushButton:disabled {"
        "  background: #f0f2f5;"
        "  border-color: #d7dee8;"
        "  color: #a2acb9;"
        "}"

        "#toolBar { background: #ffffff; border: 1px solid #d7dee8; border-radius: 6px; }"
        "#toolBar QPushButton { padding: 7px 16px; min-height: 22px; }"
        "#primaryButton {"
        "  background: #2f73d9;"
        "  border-color: #2f73d9;"
        "  color: #ffffff;"
        "}"
        "#primaryButton:hover {"
        "  background: #2363c5;"
        "  border-color: #2363c5;"
        "  color: #ffffff;"
        "}"
        "#primaryButton:pressed { background: #1a4fa0; }"

        "QLineEdit {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  padding: 5px 8px;"
        "  color: #344256;"
        "}"
        "QLineEdit:focus { border-color: #82a9e8; }"

        "QComboBox {"
        "  background: #ffffff;"
        "  border: 1px solid #d7dee8;"
        "  border-radius: 4px;"
        "  padding: 5px 8px;"
        "  color: #344256;"
        "}"
        "QComboBox:hover { border-color: #82a9e8; }"

        "QTableWidget {"
        "  background: #ffffff;"
        "  alternate-background-color: #f8fafc;"
        "  border: none;"
        "  gridline-color: #f0f2f5;"
        "  selection-background-color: #e4efff;"
        "  selection-color: #243447;"
        "}"
        "QTableWidget::item { padding: 4px 7px; border-bottom: 1px solid #f0f2f5; }"
        "QTableWidget::item:selected { background: #e4efff; color: #243447; }"
        "QHeaderView::section {"
        "  background: #f0f3f7;"
        "  border: none;"
        "  border-bottom: 1px solid #d7dee8;"
        "  padding: 4px 7px;"
        "  color: #536174;"
        "  font-weight: bold;"
        "}"

        "QTextEdit {"
        "  background: #ffffff;"
        "  border: none;"
        "  color: #536174;"
        "}"

        "QStatusBar { background: #edf1f6; border-top: 1px solid #d7dee8; }"
        "QProgressBar {"
        "  border: none;"
        "  background: #e0e6ee;"
        "  border-radius: 4px;"
        "  max-height: 8px;"
        "}"
        "QProgressBar::chunk {"
        "  background: #2f73d9;"
        "  border-radius: 4px;"
        "}"
        "QSplitter::handle { width: 10px; height: 10px; background: transparent; }"));
}

// ===== 工具栏 =====

/**
 * @brief       建工具栏：四个动作按钮 + 右侧说明类按钮
 *
 * @return      none
 * @details     左起 开始配置（primaryButton 主按钮）、仅探测、撤销代理、停止（初始禁用），
 *              然后 addStretch 把余量吃掉，右侧放 说明 / 更新说明 / 关于（浅蓝底）与 退出（浅红底）。
 *              每个按钮都设手型光标并连到对应槽。
 *              最后把八个按钮的字体统一加大一号（pointSizeF 有效就用字号，否则退化用 pixelSize），
 *              避免工具栏文字比正文小显得局促。
 * @doxygen_sig_fp a28f8956e8b3edd7
 */
void MainWindow::setup_tool_bar()
{
    m_toolBarWidget = new QWidget();
    m_toolBarWidget->setObjectName(QStringLiteral("toolBar"));

    auto *bar = new QHBoxLayout(m_toolBarWidget);
    bar->setContentsMargins(10, 6, 10, 6);
    bar->setSpacing(6);

    m_startBtn = new QPushButton(QStringLiteral("开始配置"));
    m_startBtn->setObjectName(QStringLiteral("primaryButton"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::on_start);
    bar->addWidget(m_startBtn);

    m_probeBtn = new QPushButton(QStringLiteral("仅探测"));
    m_probeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_probeBtn, &QPushButton::clicked, this, &MainWindow::on_probe_only);
    bar->addWidget(m_probeBtn);

    m_unsetBtn = new QPushButton(QStringLiteral("撤销代理"));
    m_unsetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_unsetBtn, &QPushButton::clicked, this, &MainWindow::on_unset);
    bar->addWidget(m_unsetBtn);

    m_stopBtn = new QPushButton(QStringLiteral("停止"));
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::on_stop);
    bar->addWidget(m_stopBtn);

    bar->addStretch(1);

    const QString lightBlue = QStringLiteral(
        "QPushButton { background: #eaf1fc; border-color: #c3d8f5; color: #2166c1; }"
        "QPushButton:hover { background: #d6e4fa; border-color: #82a9e8; }");

    auto *helpBtn = new QPushButton(QStringLiteral("说明"));
    helpBtn->setCursor(Qt::PointingHandCursor);
    helpBtn->setStyleSheet(lightBlue);
    connect(helpBtn, &QPushButton::clicked, this, &MainWindow::on_help);
    bar->addWidget(helpBtn);

    auto *updatesBtn = new QPushButton(QStringLiteral("更新说明"));
    updatesBtn->setCursor(Qt::PointingHandCursor);
    updatesBtn->setStyleSheet(lightBlue);
    connect(updatesBtn, &QPushButton::clicked, this, &MainWindow::on_updates);
    bar->addWidget(updatesBtn);

    auto *aboutBtn = new QPushButton(QStringLiteral("关于"));
    aboutBtn->setCursor(Qt::PointingHandCursor);
    aboutBtn->setStyleSheet(lightBlue);
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::on_about);
    bar->addWidget(aboutBtn);

    auto *exitBtn = new QPushButton(QStringLiteral("退出"));
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #fdecec; border-color: #f4c7c7; color: #b93a3a; }"
        "QPushButton:hover { background: #fadcdc; border-color: #e88a8a; }"));
    connect(exitBtn, &QPushButton::clicked, this, &MainWindow::on_exit);
    bar->addWidget(exitBtn);

    QFont f = QApplication::font();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(f.pointSizeF() + 1);
    else
        f.setPixelSize(f.pixelSize() + 1);
    for (auto *w : {static_cast<QWidget *>(m_startBtn), static_cast<QWidget *>(m_probeBtn),
                    static_cast<QWidget *>(m_unsetBtn), static_cast<QWidget *>(m_stopBtn),
                    static_cast<QWidget *>(helpBtn), static_cast<QWidget *>(updatesBtn),
                    static_cast<QWidget *>(aboutBtn), static_cast<QWidget *>(exitBtn)})
        w->setFont(f);
}

// ===== 表单 =====

/**
 * @brief       建参数表单（三行）
 *
 * @return      none
 * @details     行 1：代理地址输入框（占位提示「留空自动探测」）+ 常见端口下拉 + CLI 路径 + 浏览…。
 *                下拉由 kPortPresets 填充，首项「常见端口」data=-1；选中具体端口时把
 *                http://127.0.0.1:<port> 直接填进地址框，省去手打。
 *              行 2：CA 证书输入框 + 浏览…（过滤 *.pem / *.crt / *.cer）。
 *              行 3：三个复选框——写入用户环境变量（默认勾）、写完重启后台服务（默认勾）、
 *                仅演练（默认不勾，避免用户误以为已经配置成功）。
 *              两个「浏览…」都用 QFileDialog 取绝对路径并转原生分隔符后回填。
 * @doxygen_sig_fp 9b06a5a4c5b92975
 */
void MainWindow::setup_form()
{
    m_formLayout = new QVBoxLayout();
    m_formLayout->setSpacing(6);

    auto *row1 = new QHBoxLayout();
    row1->setSpacing(8);

    auto *proxyLabel = new QLabel(QStringLiteral("代理地址："));
    proxyLabel->setObjectName(QStringLiteral("fieldLabel"));
    row1->addWidget(proxyLabel);

    m_proxyEdit = new QLineEdit();
    m_proxyEdit->setPlaceholderText(QStringLiteral("留空自动探测，如 http://127.0.0.1:7890"));
    row1->addWidget(m_proxyEdit, 2);

    m_portCombo = new QComboBox();
    for (int p : kPortPresets)
        m_portCombo->addItem(QString::number(p), p);
    m_portCombo->insertItem(0, QStringLiteral("常见端口"), -1);
    m_portCombo->setCurrentIndex(0);
    connect(m_portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const int port = m_portCombo->itemData(index).toInt();
                if (port > 0)
                    m_proxyEdit->setText(
                        QStringLiteral("http://127.0.0.1:%1").arg(port));
            });
    m_portCombo->setFixedWidth(120);
    row1->addWidget(m_portCombo);

    auto *cliLabel = new QLabel(QStringLiteral("CLI："));
    cliLabel->setObjectName(QStringLiteral("fieldLabel"));
    row1->addWidget(cliLabel);

    m_cliEdit = new QLineEdit();
    m_cliEdit->setPlaceholderText(
        QStringLiteral("留空自动定位 opencode-cli.exe"));
    row1->addWidget(m_cliEdit, 2);

    auto *cliBrowseBtn = new QPushButton(QStringLiteral("浏览…"));
    cliBrowseBtn->setCursor(Qt::PointingHandCursor);
    connect(cliBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 opencode 可执行文件"), QString(),
            QStringLiteral("可执行文件 (*.exe *.cmd);;所有文件 (*.*)"));
        if (!p.isEmpty())
            m_cliEdit->setText(QDir::toNativeSeparators(p));
    });
    row1->addWidget(cliBrowseBtn);

    m_formLayout->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    row2->setSpacing(8);

    auto *caLabel = new QLabel(QStringLiteral("CA 证书："));
    caLabel->setObjectName(QStringLiteral("fieldLabel"));
    row2->addWidget(caLabel);

    m_caEdit = new QLineEdit();
    m_caEdit->setPlaceholderText(
        QStringLiteral("可选。企业 SSL 拦截场景的 PEM 证书（写入 NODE_EXTRA_CA_CERTS）"));
    row2->addWidget(m_caEdit, 1);

    auto *caBrowseBtn = new QPushButton(QStringLiteral("浏览…"));
    caBrowseBtn->setCursor(Qt::PointingHandCursor);
    connect(caBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 CA 证书"), QString(),
            QStringLiteral("证书 (*.pem *.crt *.cer);;所有文件 (*.*)"));
        if (!p.isEmpty())
            m_caEdit->setText(QDir::toNativeSeparators(p));
    });
    row2->addWidget(caBrowseBtn);

    m_formLayout->addLayout(row2);

    auto *row3 = new QHBoxLayout();
    row3->setSpacing(18);

    m_userEnvCb = new QCheckBox(QStringLiteral("写入用户环境变量"));
    m_userEnvCb->setChecked(true);
    row3->addWidget(m_userEnvCb);

    m_restartCb = new QCheckBox(QStringLiteral("写完重启后台服务"));
    m_restartCb->setChecked(true);
    row3->addWidget(m_restartCb);

    m_dryRunCb = new QCheckBox(QStringLiteral("仅演练（只打印命令，不写入）"));
    m_dryRunCb->setChecked(false);
    row3->addWidget(m_dryRunCb);

    row3->addStretch(1);

    m_formLayout->addLayout(row3);
}

// ===== 状态表格面板 =====

/**
 * @brief       建「配置项状态」表格面板并加入分栏
 *
 * @param[in]   {splitter} QSplitter* 上下分栏容器
 * @return      none
 * @details     面板用 #panelCard 卡片外观，必须开 WA_StyledBackground 才能吃到 QSS 的圆角背景。
 *              表格四列「配置项 / 目标 / 状态 / 说明」：隐藏竖表头、行高 34、整行选择 + ExtendedSelection、
 *              禁止编辑、交替行色、不显示网格线、不自动换行。
 *              列宽固定 190 / 330 / 110，末列拉伸吃掉剩余宽度，长路径靠 tooltip 查看。
 * @doxygen_sig_fp d807e9cc42c9cfd7
 */
void MainWindow::setup_table_panel(QSplitter *splitter)
{
    auto *panel = new QWidget();
    panel->setObjectName(QStringLiteral("panelCard"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("配置项状态"));
    title->setObjectName(QStringLiteral("panelTitle"));
    lay->addWidget(title);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("配置项"),
                                        QStringLiteral("目标"),
                                        QStringLiteral("状态"),
                                        QStringLiteral("说明")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);

    QHeaderView *header = m_table->horizontalHeader();
    for (int c = 0; c < ColCount; ++c)
        header->setSectionResizeMode(c, QHeaderView::Interactive);
    header->setStretchLastSection(true);
    m_table->setColumnWidth(ColKey, 190);
    m_table->setColumnWidth(ColTarget, 330);
    m_table->setColumnWidth(ColStatus, 110);
    m_table->setWordWrap(false);

    lay->addWidget(m_table);
    splitter->addWidget(panel);
}

// ===== 日志面板 =====

/**
 * @brief       建日志面板并加入分栏
 *
 * @param[in]   {splitter} QSplitter* 上下分栏容器
 * @return      none
 * @details     同款卡片外观 + 标题「日志」；QTextEdit 只读、占位「操作日志会显示在这里」、最小高 90。
 *              面板最小高 130，保证用户把分栏拖到底时日志区不会被压没。
 * @doxygen_sig_fp 967c6072a86c20ea
 */
void MainWindow::setup_log_panel(QSplitter *splitter)
{
    auto *panel = new QWidget();
    panel->setObjectName(QStringLiteral("panelCard"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("日志"));
    title->setObjectName(QStringLiteral("panelTitle"));
    lay->addWidget(title);

    m_logEdit = new QTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(QStringLiteral("操作日志会显示在这里"));
    m_logEdit->setMinimumHeight(90);
    lay->addWidget(m_logEdit);

    panel->setMinimumHeight(130);
    splitter->addWidget(panel);
}

// ===== 图例 =====

/**
 * @brief       建彩色图例
 *
 * @return      none
 * @details     用 RichText 单行显示四个圆点及含义：成功 #159447、提醒 #d56a16、错误 #e11d48、
 *              弱化 #7c8798，与日志与表格里的着色一一对应，方便用户对照日志判断严重程度。
 * @doxygen_sig_fp 80888ab40ef7555f
 */
void MainWindow::setup_legend()
{
    m_legend = new QLabel();
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setText(QStringLiteral(
        "<span style='color:#159447;'>●</span> 成功 &nbsp;&nbsp;"
        "<span style='color:#d56a16;'>●</span> 提醒 &nbsp;&nbsp;"
        "<span style='color:#e11d48;'>●</span> 错误 &nbsp;&nbsp;"
        "<span style='color:#7c8798;'>●</span> 弱化信息"));
}

// ===== 状态栏 =====

/**
 * @brief       建状态栏与运行时长定时器
 *
 * @return      none
 * @details     左侧 m_summaryLabel 占满可伸缩区域，显示「就绪 / 完成 / 结束」等一句话摘要。
 *              右侧常驻三块：转发状态标签（启动实测后显示已配置/未配置）、运行时长标签（初始隐藏）、
 *              进度条（宽 160、range 0..1，忙时切成不确定态）。
 *              同时建一个 1 秒周期的 m_runTimer 连 on_run_timer，并 new QElapsedTimer 供计时使用。
 * @doxygen_sig_fp ae25a04a32918f73
 */
void MainWindow::setup_status_bar()
{
    m_summaryLabel = new QLabel(QStringLiteral("就绪"));
    statusBar()->addWidget(m_summaryLabel, 1);

    m_proxyStateLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_proxyStateLabel);

    m_runTimeLabel = new QLabel(this);
    m_runTimeLabel->hide();
    statusBar()->addPermanentWidget(m_runTimeLabel);

    m_progress = new QProgressBar();
    m_progress->setFixedWidth(160);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    statusBar()->addPermanentWidget(m_progress);

    m_runTimer = new QTimer(this);
    m_runTimer->setInterval(1000);
    connect(m_runTimer, &QTimer::timeout, this, &MainWindow::on_run_timer);

    m_runElapsed = new QElapsedTimer();
}

// ===== 右下角状态指示 / 运行时长 =====

/**
 * @brief       设置右下角转发状态文案与颜色
 *
 * @param[in]   {text} const QString& 状态文案
 * @param[in]   {color} const QColor& 文案颜色
 * @return      none
 * @details     用带 color 与 font-weight:bold 的 span 写 m_proxyStateLabel。
 *              语义配色由调用点决定：已配置绿、未配置灰、检测中橙。
 * @doxygen_sig_fp 1f0a95000aa8e913
 */
void MainWindow::show_proxy_state(const QString &text, const QColor &color)
{
    m_proxyStateLabel->setText(QStringLiteral("<span style='color:%1; font-weight:bold;'>%2</span>")
                                   .arg(color.name(), text));
}

/**
 * @brief       请求工作线程静默实测一次转发状态
 *
 * @return      none
 * @details     先把指示切成橙色「转发检测中…」，再用 QMetaObject::invokeMethod 队列调用
 *              m_manager 的 check_state——不阻塞 UI 线程；
 *              结果经 proxy_state 信号回到 on_proxy_state 更新文案，全程不写日志。
 * @doxygen_sig_fp fb0592759c66bc63
 */
void MainWindow::trigger_state_check()
{
    show_proxy_state(QStringLiteral("转发检测中…"), kColorWarn);
    QMetaObject::invokeMethod(m_manager, "check_state");
}

/**
 * @brief       开始运行时长计时
 *
 * @return      none
 * @details     m_counting 置 true、m_runElapsed 重启、定时器启动、标签显示，并立即刷一次，
 *              避免第一秒显示空白。
 *              计时只在本次进程内有效：关掉工具就清零，重开只显示状态、不显示时长。
 * @doxygen_sig_fp d230c5d57a3f93c1
 */
void MainWindow::start_run_time()
{
    m_counting = true;
    m_runElapsed->restart();
    m_runTimer->start();
    m_runTimeLabel->show();
    refresh_run_time();
}

/**
 * @brief       停止计时并隐藏时长
 *
 * @return      none
 * @details     m_counting 置 false、停表、清空并隐藏标签；撤销成功后调用。
 * @doxygen_sig_fp e28fed0ec5afc5b4
 */
void MainWindow::stop_run_time()
{
    m_counting = false;
    m_runTimer->stop();
    m_runTimeLabel->clear();
    m_runTimeLabel->hide();
}

/**
 * @brief       刷新「已运行」时长显示
 *
 * @return      none
 * @details     未在计时时直接返回（可能收到上一轮挂起的回调）。
 *              按 m_runElapsed 换算天/时/分/秒：时:分:秒固定两位补零，超过 24 小时前置「N天+」。
 *              数值用深色加粗、前缀「已运行」用弱化色，长度变化不会跳动得太突兀。
 * @doxygen_sig_fp de35c788cf1265b5
 */
void MainWindow::refresh_run_time()
{
    if (!m_counting)
        return;

    const qint64 totalSec = m_runElapsed->elapsed() / 1000;
    const qint64 days = totalSec / 86400;
    const qint64 hours = (totalSec % 86400) / 3600;
    const qint64 mins = (totalSec % 3600) / 60;
    const qint64 secs = totalSec % 60;

    const QString hms = QStringLiteral("%1:%2:%3")
                            .arg(hours, 2, 10, QLatin1Char('0'))
                            .arg(mins, 2, 10, QLatin1Char('0'))
                            .arg(secs, 2, 10, QLatin1Char('0'));
    m_runTimeLabel->setText(
        QStringLiteral("<span style='color:#536174;'>已运行 </span>"
                       "<span style='color:#344256; font-weight:bold;'>%1%2</span>")
            .arg(days > 0 ? QStringLiteral("%1天+").arg(days) : QString(), hms));
}

/**
 * @brief       定时器回调，转去刷新时长
 *
 * @return      none
 * @details     只是 refresh_run_time 的槽壳，保留独立槽便于 connect，也方便后面加逻辑。
 * @doxygen_sig_fp 88128d38730e1cd7
 */
void MainWindow::on_run_timer()
{
    refresh_run_time();
}

/**
 * @brief       处理实测结果，更新右下角指示
 *
 * @param[in]   {configured} bool true = 已配置代理
 * @return      none
 * @details     已配置显示绿色「转发已配置」，未配置显示灰色「转发未配置」。
 *              只改这一个标签，不影响正在跑的流水线、也不动运行时长。
 * @doxygen_sig_fp 5113acb7290cabda
 */
void MainWindow::on_proxy_state(bool configured)
{
    show_proxy_state(configured ? QStringLiteral("转发已配置") : QStringLiteral("转发未配置"),
                     configured ? kColorOk : kColorDim);
}

// ===== 日志 =====

/**
 * @brief       往日志面板追加一行彩色文本
 *
 * @param[in]   {text} const QString& 日志正文
 * @param[in]   {color} const QColor& 日志颜色
 * @return      none
 * @details     先 text.toHtmlEscaped() 再包 span：CLI 输出里可能带 <>& 与 URL 查询串，
 *              不转义会被当成 HTML 吃掉。
 *              追加后若块数超过 kMaxLogBlocks(2000)，用 QTextCursor 从文档头部删掉多出的块，
 *              避免长时间运行后文档无限膨胀、滚动变卡。
 * @doxygen_sig_fp 7460c47fa016223c
 */
void MainWindow::append_log(const QString &text, const QColor &color)
{
    const QString html = QStringLiteral("<span style='color:%1;'>%2</span>")
                             .arg(color.name(), text.toHtmlEscaped());
    m_logEdit->append(html);

    QTextDocument *doc = m_logEdit->document();
    if (doc->blockCount() > kMaxLogBlocks) {
        QTextCursor cursor(doc->begin());
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor,
                            doc->blockCount() - kMaxLogBlocks);
        cursor.removeSelectedText();
    }
}

// ===== 状态行 =====

/**
 * @brief       往状态表格追加一行
 *
 * @param[in]   {key} const QString& 配置项名称
 * @param[in]   {target} const QString& 目标（如代理地址）
 * @param[in]   {status} const QString& 状态文案
 * @param[in]   {color} const QColor& 状态颜色
 * @param[in]   {note} const QString& 补充说明
 * @return      none
 * @details     取当前行数后插入新行，四列分别填配置项、目标、状态（按 color 上色）、说明。
 *              目标与说明都设 tooltip：目标常是很长的路径或脱敏 URL，列宽放不下时靠悬停看全。
 * @doxygen_sig_fp 7599bc49862d9010
 */
void MainWindow::add_status_row(const QString &key, const QString &target, const QString &status,
                                const QColor &color, const QString &note)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *keyItem = new QTableWidgetItem(key);
    m_table->setItem(row, ColKey, keyItem);

    auto *targetItem = new QTableWidgetItem(target);
    targetItem->setToolTip(target);
    m_table->setItem(row, ColTarget, targetItem);

    auto *statusItem = new QTableWidgetItem(status);
    statusItem->setForeground(color);
    m_table->setItem(row, ColStatus, statusItem);

    auto *noteItem = new QTableWidgetItem(note);
    noteItem->setToolTip(note);
    m_table->setItem(row, ColNote, noteItem);
}

// ===== 工具栏动作 =====

/**
 * @brief       「开始配置」入口
 *
 * @return      none
 * @details     m_busy 时直接返回，防止流水线重入。
 *              先把 CLI 框内容经队列 set_cli 同步进工作线程；再记住本次是否演练与入口 Start
 *              （on_finished 靠这两个值决定右下角指示和计时怎么更新）。
 *              然后 set_busy(true)、清空表格、打一行带时间戳的起始日志，最后队列调用
 *              detect_and_apply(代理地址, CA 路径, 写用户环境, 重启服务, 是否演练)。
 * @doxygen_sig_fp d097e457eb554a2a
 */
void MainWindow::on_start()
{
    if (m_busy)
        return;

    const QString cli = m_cliEdit->text().trimmed();
    QMetaObject::invokeMethod(m_manager, "set_cli", Q_ARG(QString, cli));

    m_lastDryRun = m_dryRunCb->isChecked();
    m_lastAction = LastAction::Start;

    set_busy(true);
    m_table->setRowCount(0);
    append_log(QStringLiteral("开始配置 — %1").arg(QDateTime::currentDateTime().toString(
                      QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
               kColorInfo);

    QMetaObject::invokeMethod(m_manager, "detect_and_apply",
                              Q_ARG(QString, m_proxyEdit->text().trimmed()),
                              Q_ARG(QString, m_caEdit->text().trimmed()),
                              Q_ARG(bool, m_userEnvCb->isChecked()),
                              Q_ARG(bool, m_restartCb->isChecked()),
                              Q_ARG(bool, m_dryRunCb->isChecked()));
}

/**
 * @brief       「仅探测」入口
 *
 * @return      none
 * @details     与 on_start 同款的防重入与 CLI 同步，入口记为 Probe 但不改 m_lastDryRun。
 *              清表格后队列调用 probe_only(代理地址)。
 *              探测不写任何配置，所以 on_finished 里不会动右下角指示与运行时长。
 * @doxygen_sig_fp 016fe8d4cf5b4fc2
 */
void MainWindow::on_probe_only()
{
    if (m_busy)
        return;

    QMetaObject::invokeMethod(m_manager, "set_cli",
                              Q_ARG(QString, m_cliEdit->text().trimmed()));

    m_lastAction = LastAction::Probe;

    set_busy(true);
    m_table->setRowCount(0);
    append_log(QStringLiteral("仅探测 — %1").arg(QDateTime::currentDateTime().toString(
                      QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
               kColorInfo);

    QMetaObject::invokeMethod(m_manager, "probe_only",
                              Q_ARG(QString, m_proxyEdit->text().trimmed()));
}

/**
 * @brief       「撤销代理」入口（带二次确认）
 *
 * @return      none
 * @details     撤销影响面大（会清空服务与用户环境里的代理并重启服务），所以先 QMessageBox::question
 *              二次确认，默认按钮是 No，误按回车不会撤销。
 *              确认后同步 CLI、记住演练标记与入口 Unset、清表格、打日志，再队列调用
 *              unset_all(是否重启, 是否演练)。
 * @doxygen_sig_fp b1fb13463b2919e4
 */
void MainWindow::on_unset()
{
    if (m_busy)
        return;

    const QMessageBox::StandardButton btn = QMessageBox::question(
        this, QStringLiteral("撤销代理"),
        QStringLiteral("即将清空 OpenCode 后台服务与用户环境变量中的代理设置，\n"
                       "并按选项重启后台服务。\n\n是否继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes)
        return;

    QMetaObject::invokeMethod(m_manager, "set_cli",
                              Q_ARG(QString, m_cliEdit->text().trimmed()));

    m_lastDryRun = m_dryRunCb->isChecked();
    m_lastAction = LastAction::Unset;

    set_busy(true);
    m_table->setRowCount(0);
    append_log(QStringLiteral("撤销代理 — %1").arg(QDateTime::currentDateTime().toString(
                      QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
               kColorInfo);

    QMetaObject::invokeMethod(m_manager, "unset_all",
                              Q_ARG(bool, m_restartCb->isChecked()),
                              Q_ARG(bool, m_dryRunCb->isChecked()));
}

/**
 * @brief       「停止」入口：请求中止流水线
 *
 * @return      none
 * @details     直接调 request_abort()（原子标志，可跨线程），并提示「当前步骤结束后停止」——
 *              service restart 期间无法中断，如实告知比假装立刻停下更好。
 *              按钮只在流水线运行时可点（由 set_busy 控制）。
 * @doxygen_sig_fp 4580e21835f4d255
 */
void MainWindow::on_stop()
{
    m_manager->request_abort();
    append_log(QStringLiteral("正在停止…（当前步骤结束后停止，service restart 期间无法中断）"),
               kColorWarn);
    m_summaryLabel->setText(QStringLiteral("正在停止…"));
}

/**
 * @brief       弹出「关于」对话框
 *
 * @return      none
 * @details     在栈上建 AboutDialog 后 exec()，模态阻塞到用户关闭，窗口随作用域自动释放。
 * @doxygen_sig_fp e119773229ac67b1
 */
void MainWindow::on_about()
{
    AboutDialog dlg(this);
    dlg.exec();
}

/**
 * @brief       弹出「使用说明」对话框
 *
 * @return      none
 * @details     在栈上建 HelpDialog 后 exec()，模态展示后自动释放。
 * @doxygen_sig_fp 9c88d3d17eeb7747
 */
void MainWindow::on_help()
{
    HelpDialog dlg(this);
    dlg.exec();
}

/**
 * @brief       弹出「更新说明」对话框
 *
 * @return      none
 * @details     在栈上建 UpdateDialog 后 exec()，模态展示后自动释放。
 * @doxygen_sig_fp 4bca556cc8a4d257
 */
void MainWindow::on_updates()
{
    UpdateDialog dlg(this);
    dlg.exec();
}

/**
 * @brief       关闭主窗口
 *
 * @return      none
 * @details     走 close() 而不是直接退出进程：让 closeEvent 有机会处理运行中的流水线与工作线程。
 * @doxygen_sig_fp 6ac6ca72c74e2d7b
 */
void MainWindow::on_exit()
{
    close();
}

// ===== ProxyManager 信号处理 =====

/**
 * @brief       接收工作线程日志并落到面板
 *
 * @param[in]   {text} const QString& 日志正文
 * @param[in]   {color} const QColor& 日志颜色
 * @return      none
 * @details     只是 append_log 的槽壳；槽在 UI 线程执行（跨线程自动队列连接）。
 * @doxygen_sig_fp 21f182b9b71342e0
 */
void MainWindow::on_log(const QString &text, const QColor &color)
{
    append_log(text, color);
}

/**
 * @brief       展示探测到的可用代理
 *
 * @param[in]   {proxies} const QList<ProxyDetector::Proxy>& 探测结果（首个为选用项）
 * @return      none
 * @details     首个标「代理（选用）」，其余标「代理（备选）」，最多显示 3 行，避免表格被候选刷屏。
 *              URL 经 mask_url 脱敏；状态列固定「隧道通过」绿色；说明列放 scheme://host:port，
 *              方便用户核对端口。
 * @doxygen_sig_fp 15ce0d5c4ecbf82a
 */
void MainWindow::on_detected(const QList<ProxyDetector::Proxy> &proxies)
{
    if (proxies.isEmpty())
        return;

    for (int i = 0; i < proxies.size() && i < 3; ++i) {
        add_status_row(i == 0 ? QStringLiteral("代理（选用）")
                              : QStringLiteral("代理（备选）"),
                       ProxyDetector::mask_url(proxies.at(i).url),
                       QStringLiteral("隧道通过"), kColorOk,
                       QStringLiteral("%1://%2:%3")
                           .arg(proxies.at(i).scheme, proxies.at(i).host)
                           .arg(proxies.at(i).port));
    }
}

/**
 * @brief       展示单个配置项的写入结果
 *
 * @param[in]   {key} const QString& 环境变量名
 * @param[in]   {value} const QString& 写入的值（已脱敏）
 * @param[in]   {ok} bool 是否成功
 * @return      none
 * @details     成功显示绿色「成功」，失败显示红色「失败」并在说明列提示「详见日志」。
 *              值已在工作线程脱敏，这里直接展示。
 * @doxygen_sig_fp 8a17f484c8f6a082
 */
void MainWindow::on_env_state(const QString &key, const QString &value, bool ok)
{
    add_status_row(key, value, ok ? QStringLiteral("成功") : QStringLiteral("失败"),
                   ok ? kColorOk : kColorErr,
                   ok ? QString() : QStringLiteral("详见日志"));
}

/**
 * @brief       展示单条验证结果
 *
 * @param[in]   {name} const QString& 验证项名称
 * @param[in]   {ok} bool 是否通过
 * @param[in]   {detail} const QString& 详细说明
 * @return      none
 * @details     配置项列加「验证：」前缀以示区分；通过显示绿色「通过」、未通过显示橙色「未通过」
 *              （未通过不等于出错，多为服务还没起好）。说明列放 detail，例如回读的 KEY=VALUE 或退出码。
 * @doxygen_sig_fp c12d4042cea86ace
 */
void MainWindow::on_verify(const QString &name, bool ok, const QString &detail)
{
    add_status_row(QStringLiteral("验证：%1").arg(name),
                   QString(),
                   ok ? QStringLiteral("通过") : QStringLiteral("未通过"),
                   ok ? kColorOk : kColorWarn,
                   detail);
}

/**
 * @brief       流水线结束的统一收尾
 *
 * @param[in]   {ok} bool 整体是否成功
 * @return      none
 * @details     先解除忙态、更新状态栏摘要与结束日志。
 *              演练直接返回：假装的写入不该改动任何真实显示。
 *              否则按入口决定:
 *              · Start 成功 -> 绿色「转发已配置」并开始计时；失败 -> 静默重测同步真实状态；
 *              · Unset 成功 -> 灰色「转发未配置」并停止计时；失败 -> 静默重测；
 *              · Probe -> 什么也不动（仅探测不写配置）。
 * @doxygen_sig_fp 0bd7dae65eb16249
 */
void MainWindow::on_finished(bool ok)
{
    set_busy(false);
    m_summaryLabel->setText(ok ? QStringLiteral("完成 — 配置成功")
                              : QStringLiteral("结束 — 存在失败项，详见日志"));
    append_log(ok ? QStringLiteral("操作完成。") : QStringLiteral("操作结束（有失败项，详见上方日志）。"),
               ok ? kColorOk : kColorWarn);

    if (m_lastDryRun)
        return;

    switch (m_lastAction) {
    case LastAction::None:
        break;
    case LastAction::Start:
        if (ok) {
            show_proxy_state(QStringLiteral("转发已配置"), kColorOk);
            start_run_time();
        } else {
            trigger_state_check();
        }
        break;
    case LastAction::Unset:
        if (ok) {
            show_proxy_state(QStringLiteral("转发未配置"), kColorDim);
            stop_run_time();
        } else {
            trigger_state_check();
        }
        break;
    case LastAction::Probe:
        break;
    }
}

// ===== 忙态互斥 =====

/**
 * @brief       切换忙态，互斥工具栏按钮
 *
 * @param[in]   {busy} bool true = 流水线进行中
 * @return      none
 * @details     忙时禁用 开始/探测/撤销、使能 停止，并让进度条 setRange(0,0) 进入不确定态再显示；
 *              闲时反向恢复并隐藏进度条。
 *              这是防止用户在流水线运行中再次触发操作的唯一闸门。
 * @doxygen_sig_fp a712d8eead556657
 */
void MainWindow::set_busy(bool busy)
{
    m_busy = busy;
    m_startBtn->setEnabled(!busy);
    m_probeBtn->setEnabled(!busy);
    m_unsetBtn->setEnabled(!busy);
    m_stopBtn->setEnabled(busy);
    m_progress->setRange(0, 0);
    m_progress->setVisible(busy);
}

// ===== 关窗 =====

/**
 * @brief       关窗时安全停止工作线程
 *
 * @param[in,out] {event} QCloseEvent* 关闭事件
 * @return      none
 * @details     流水线还在跑时先 request_abort 再弹确认，说明最长要等 restart 结束：
 *              选「否」则 event->ignore() 留在程序里。
 *              确认后 request_abort() -> m_worker->quit() -> wait(10000)；超时 terminate() 强杀，
 *              避免步骤卡死时进程残留、exe 无法被覆盖。最后 accept 放行关闭。
 * @doxygen_sig_fp 60adf201c06b52f6
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_busy) {
        m_manager->request_abort();
        const QMessageBox::StandardButton btn = QMessageBox::question(
            this, QStringLiteral("正在运行"),
            QStringLiteral("配置流水线正在运行。\n"
                           "停止需要等当前步骤结束（service restart 最多可能要 2 分钟）。\n\n"
                           "选择「是」将请求中止并等待最多 10 秒后退出；\n"
                           "选择「否」留在程序中。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    m_manager->request_abort();
    m_worker->quit();
    if (!m_worker->wait(10000))
        m_worker->terminate();
    event->accept();
}