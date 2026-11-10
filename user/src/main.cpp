/**
 * @file        main.cpp
 * @brief       程序入口：Fusion 风格 + 跨线程元类型注册 + 主窗口启动
 * @author      lch (fantasylch@foxmail.com)
 * @version     1.0
 * @date        20261110
 * @note        qRegisterMetaType 必须在 MainWindow 构造前完成，否则工作线程的 detected() 队列投递会失败。
 *
 * @copyright   Copyright (c) 2026 by lch, All Rights Reserved.
 *
 * @par         修改日志:
 * <table>
 * <tr><th>Date        <th>Version   <th>Author    <th>Description
 * <tr><td>20261110    <td>1.0       <td>lch       <td>程序入口：Fusion 风格 + 跨线程元类型注册 + 主窗口启动
 * </table>
 */

#include "mainwindow.h"
#include "proxydetector.h"

#include <QApplication>

/**
 * @brief       程序入口：注册跨线程元类型并启动主窗口
 *
 * @param[in]   {argc} int 命令行参数个数
 * @param[in]   {argv} char *[] 命令行参数数组
 * @return      QApplication::exec() 返回的进程退出码
 * @details     启动顺序有依赖，不能随意调换：
 *              ① setStyle(QStringLiteral("Fusion")) 统一各 Windows 主题下的控件绘制，界面配色才可预期；
 *              ② qRegisterMetaType<QList<ProxyDetector::Proxy>> 必须在 MainWindow 构造前完成——
 *                 ProxyManager 在工作线程发出的 detected() 经队列连接投递时要靠它还原类型，
 *                 未注册会导致该信号被丢弃；
 *              ③ 主窗口建在栈上，show() 后进入事件循环，退出码直接返回给系统。
 * @doxygen_sig_fp f4b9f73e14950a17
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStringLiteral("Fusion"));

    qRegisterMetaType<QList<ProxyDetector::Proxy>>("QList<ProxyDetector::Proxy>");

    MainWindow w;
    w.show();

    return a.exec();
}