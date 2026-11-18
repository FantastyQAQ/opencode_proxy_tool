/**
 * @file        update_notes_dialog.h
 * @brief       「更新说明」窗口声明：非模态展示 README 的更新记录
 */

#ifndef UPDATENOTESDIALOG_H
#define UPDATENOTESDIALOG_H

#include <QWidget>

class QTextBrowser;

/// @brief 非模态「更新说明」窗口：正文取自 :/docs/README.zh_CN.md 的「# 更新记录」一节
class UpdateNotesDialog : public QWidget
{
    Q_OBJECT

public:
    /// @brief 构造函数：搭界面并载入 README 更新记录
    explicit UpdateNotesDialog(QWidget *parent = nullptr);

private:
    /// @brief 读取资源中的 README，截取「# 更新记录」之后的内容返回
    QString load_update_notes();
    /// @brief 把更新记录 Markdown 转成 QTextBrowser 可显示的 HTML
    QString notes_to_html(const QString &markdown);

    QTextBrowser *m_browser = nullptr;   ///< 正文浏览器
};

#endif // UPDATENOTESDIALOG_H
