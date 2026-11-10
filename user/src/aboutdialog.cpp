/**
 * @file        aboutdialog.cpp
 * @brief       「关于」对话框实现：应用名 / 版本 / 编译时间 / 作者信息
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        文案全部取自 appinfo.h 的宏，改版本或作者只需改那一处。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>「关于」对话框实现：应用名 / 版本 / 编译时间 / 作者信息
 * </table>
 */

#include "aboutdialog.h"

#include "appinfo.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

/**
 * @brief       构造「关于」对话框
 *
 * @param[in]   {parent} QWidget* 父窗口，可为空
 * @return      none（构造/析构函数，无返回值）
 * @details     组装纯展示的模态窗口：
 *              ① 去掉 Windows 默认的问号帮助按钮，标题固定为「关于」；
 *              ② 顶部大标题居中显示 APP_FULL_TITLE（应用名 + 版本 + 构建日期）；
 *              ③ 依次居中列出编译时间（__DATE__ / __TIME__）、Qt 版本（qVersion）、作者与邮箱；
 *              ④ 底部「确定」按钮 120x36，点击连 QDialog::accept；窗口默认 360x260。
 *              所有文案都取自 appinfo.h 的宏，改版本 / 作者只需动那一处。
 * @doxygen_sig_fp eb29c6acf32f318f
 */
AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("关于"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral(APP_FULL_TITLE), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    layout->addSpacing(6);

    const QString qtVersion = QStringLiteral("Qt %1").arg(QLatin1String(qVersion()));
    const QString buildTime = QStringLiteral(__DATE__) + QStringLiteral(" ")
                              + QStringLiteral(__TIME__);

    auto addLine = [this, layout](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color: #536174;"));
        layout->addWidget(label);
    };

    addLine(QStringLiteral("编译时间：") + buildTime);
    addLine(qtVersion);
    addLine(QStringLiteral("作者：") + QStringLiteral(APP_AUTHOR_NAME));
    addLine(QStringLiteral("邮箱：") + QStringLiteral(APP_AUTHOR_MAIL));

    layout->addSpacing(12);

    auto *okBtn = new QPushButton(QStringLiteral("确定"), this);
    okBtn->setFixedSize(120, 36);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okBtn, 0, Qt::AlignHCenter);

    resize(360, 260);
}