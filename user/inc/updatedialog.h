/**
 * @file updatedialog.h
 * @brief 「更新说明」对话框声明
 */

#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>

/// @brief 模态「更新说明」对话框：按版本列出功能更新记录
class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    /// @brief 构造函数：用 QTextBrowser 展示更新记录正文
    explicit UpdateDialog(QWidget *parent = nullptr);
};

#endif // UPDATEDIALOG_H
