/**
 * @file helpdialog.h
 * @brief 「使用说明」对话框声明
 */

#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <QDialog>

/// @brief 模态「使用说明」对话框：软件作用 / 工具栏按钮 / 各参数含义 / 右下角状态指示
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    /// @brief 构造函数：用 QTextBrowser 展示说明正文
    explicit HelpDialog(QWidget *parent = nullptr);
};

#endif // HELPDIALOG_H
