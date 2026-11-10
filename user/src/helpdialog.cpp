/**
 * @file        helpdialog.cpp
 * @brief       「使用说明」对话框实现：富文本展示作用、按钮、参数与状态指示
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        正文 HTML 与 docs/README.zh_CN.md 保持一致，改说明时两处一起改。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>「使用说明」对话框实现：富文本展示作用、按钮、参数与状态指示
 * </table>
 */

#include "helpdialog.h"

#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

/**
 * @brief       生成使用说明正文 HTML
 *
 * @return      说明页 HTML（编译期拼好的静态字符串）
 * @details     正文分五节：本工具做什么 / 工具栏按钮 / 参数说明 / 右下角状态指示 / 注意事项。
 *              文案与 docs/README.zh_CN.md 保持一致，改说明时两处一起改。
 *              实现上每行一个字符串字面量由编译器自动拼接，HTML 属性统一用单引号，避免 C++ 转义。
 * @doxygen_sig_fp 6e1835e28d168681
 */
QString build_help_html()
{
    return QStringLiteral(
        "<html><body style='font-size:9.5pt; color:#344256; line-height:150%;'>"

        "<h3>本工具做什么</h3>"
        "<p>OpenCode 只认标准代理环境变量，本身没有代理配置入口。本工具自动探测本机<b>真实可用</b>"
        "的 HTTP/SOCKS5 代理（发起 CONNECT 隧道握手验证，端口开着但连不出去的不算可用），"
        "把代理写入 OpenCode 后台服务与 Windows 用户环境变量，重启服务生效并做三层验证；"
        "也可一键撤销全部代理设置，恢复直连。</p>"

        "<h3>工具栏按钮</h3>"
        "<ul>"
        "<li><b>开始配置</b>：探测可用代理 → 写后台服务环境变量 → 写用户环境变量 → 重启服务 → "
        "三层验证（回读配置、CLI↔服务连通、出站隧道复测）。</li>"
        "<li><b>仅探测</b>：只探测并展示可用代理，不写入任何东西。</li>"
        "<li><b>撤销代理</b>：清空后台服务与用户环境变量中的代理（含小写键与 NODE_EXTRA_CA_CERTS），"
        "按选项重启服务。</li>"
        "<li><b>停止</b>：请求中止流水线，只在步骤之间生效；service restart 执行期间无法中断。</li>"
        "</ul>"

        "<h3>参数说明</h3>"
        "<ul>"
        "<li><b>代理地址</b>：留空自动探测（环境变量 → 常见端口 ∩ 监听端口 → 监听端口 → 盲探常见端口）；"
        "显式填写则只探测该地址，不通则报错、不做任何修改。</li>"
        "<li><b>常见端口</b>：把 http://127.0.0.1:&lt;所选端口&gt; 快捷填入代理地址框。</li>"
        "<li><b>CLI</b>：opencode 可执行文件。留空自动定位"
        "（%LOCALAPPDATA%\\Programs\\*.opencode*\\resources\\opencode-cli.exe → ~/.opencode/bin → PATH）；"
        "也可用「浏览…」手动指定。</li>"
        "<li><b>CA 证书</b>：可选。企业 SSL 拦截场景的 PEM 证书路径，写入 NODE_EXTRA_CA_CERTS，"
        "让 CLI 信任企业证书。</li>"
        "<li><b>写入用户环境变量</b>：把代理写入 HKCU\\Environment 并广播 WM_SETTINGCHANGE，"
        "新开的终端/应用直接继承；已开着的窗口需重开。</li>"
        "<li><b>写完重启后台服务</b>：修改服务环境变量会停掉正在跑的服务，必须重启才生效。</li>"
        "<li><b>仅演练</b>：只打印将要执行的命令，不写入、不重启、不验证。</li>"
        "</ul>"

        "<h3>右下角状态指示</h3>"
        "<ul>"
        "<li><b>转发已配置 / 未配置</b>：启动时后台实测一次（读 OpenCode 服务环境变量与用户注册表）；"
        "配置、撤销成功后即时更新；失败或中止后自动重测。</li>"
        "<li><b>已运行时长</b>：本次运行中「开始配置」真实成功后开始计时（仅演练不算）；"
        "关闭工具后清零，重新打开工具只显示状态、不显示时长。</li>"
        "</ul>"

        "<h3>注意事项</h3>"
        "<ul>"
        "<li>NO_PROXY 固定写 localhost,127.0.0.1,::1,0.0.0.0，保证 CLI 与本地服务通信不被塞进代理。</li>"
        "<li>桌面版 OpenCode 若在运行，配置完成后建议完全退出再重开一次，其拉起的服务才会继承新环境。</li>"
        "<li>日志与表格中的代理 URL 已脱敏（用户凭据显示为 ***）。</li>"
        "</ul>"

        "</body></html>");
}

} // namespace

// ===== 构造 =====

/**
 * @brief       构造「使用说明」对话框
 *
 * @param[in]   {parent} QWidget* 父窗口，可为空
 * @return      none（构造/析构函数，无返回值）
 * @details     去掉问号帮助按钮后，用 QTextBrowser 承载 build_help_html() 的正文并开启外部链接，
 *              内容超出窗口时可滚动；底部「关闭」按钮 120x36 连 QDialog::accept；默认尺寸 660x560。
 * @doxygen_sig_fp 7ac8333298335580
 */
HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("使用说明"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setHtml(build_help_html());
    layout->addWidget(browser, 1);

    auto *okBtn = new QPushButton(QStringLiteral("关闭"), this);
    okBtn->setFixedSize(120, 36);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignHCenter);

    resize(660, 560);
}