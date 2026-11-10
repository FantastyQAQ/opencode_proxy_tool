/**
 * @file        updatedialog.cpp
 * @brief       「更新说明」对话框实现：富文本展示版本更新记录
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        更新记录与 docs/README.zh_CN.md 的「更新记录」一节同步维护。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>「更新说明」对话框实现：富文本展示版本更新记录
 * </table>
 */

#include "updatedialog.h"

#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

/**
 * @brief       生成更新记录正文 HTML
 *
 * @return      更新记录 HTML（编译期拼好的静态字符串）
 * @details     目前列 SV01.001 [261110] 首个可用版本的三条特性。
 *              内容与 docs/README.zh_CN.md 的「更新记录」一节同步维护，发版时两处一起改。
 *              样式与说明页一致：9.5pt、#344256、行高 150%，属性用单引号避免转义。
 * @doxygen_sig_fp 363e6a424bfec0b8
 */
QString build_updates_html()
{
    return QStringLiteral(
        "<html><body style='font-size:9.5pt; color:#344256; line-height:150%;'>"

        "<h3>SV01.001 [261110]</h3>"
        "<ul>"
        "<li>首个可用版本：探测、写入、重启、撤销、验证与日志</li>"
        "<li>新增「说明」「更新说明」按钮</li>"
        "<li>状态栏右下角显示「转发已配置/未配置」与本次配置运行时长</li>"
        "</ul>"

        "</body></html>");
}

} // namespace

// ===== 构造 =====

/**
 * @brief       构造「更新说明」对话框
 *
 * @param[in]   {parent} QWidget* 父窗口，可为空
 * @return      none（构造/析构函数，无返回值）
 * @details     结构与 HelpDialog 相同：去问号按钮、QTextBrowser 承载 build_updates_html() 正文并开启外部链接、
 *              底部「关闭」按钮 120x36 连 QDialog::accept；默认尺寸 520x380。
 * @doxygen_sig_fp 49130ae30dd82c79
 */
UpdateDialog::UpdateDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("更新说明"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setHtml(build_updates_html());
    layout->addWidget(browser, 1);

    auto *okBtn = new QPushButton(QStringLiteral("关闭"), this);
    okBtn->setFixedSize(120, 36);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignHCenter);

    resize(520, 380);
}