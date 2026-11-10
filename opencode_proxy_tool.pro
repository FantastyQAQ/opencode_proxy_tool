QT       += core gui widgets network

TARGET    = opencode_proxy_tool
TEMPLATE  = app
CONFIG   += c++17

# 只生成 release Makefile；避免 clean 时尝试清理不存在的 debug 产物而刷屏「找不到」
CONFIG   -= debug debug_and_release
CONFIG   += release

# 禁用 debug_and_release 后 qmake 默认把中间产物放到根目录，这里显式保持 release/ 目录约定
DESTDIR     = release
OBJECTS_DIR = release
MOC_DIR     = release
RCC_DIR     = release
UI_DIR      = release

# clean 时文件不存在属于正常情况，屏蔽 Windows del 的「找不到」错误输出
QMAKE_DEL_FILE = del 2>nul

# 源码与头文件统一放在 user 目录下
SOURCES += \
    user/src/main.cpp \
    user/src/mainwindow.cpp \
    user/src/proxymanager.cpp \
    user/src/proxydetector.cpp \
    user/src/aboutdialog.cpp \
    user/src/helpdialog.cpp \
    user/src/updatedialog.cpp

HEADERS += \
    user/inc/mainwindow.h \
    user/inc/proxymanager.h \
    user/inc/proxydetector.h \
    user/inc/aboutdialog.h \
    user/inc/helpdialog.h \
    user/inc/updatedialog.h \
    user/inc/appinfo.h

# 头文件搜索路径（main.cpp 中 #include "mainwindow.h" 依赖此配置）
INCLUDEPATH += user/inc

# 应用程序图标（编译进 exe，窗口/任务栏图标自动继承）
RC_FILE += opencode_proxy_tool_resource.rc

# qmake 生成的资源规则只让 res.o 依赖 .rc，换掉 .ico 后 make 会判定「Nothing to be done」，
# exe 里仍是旧图标。把 .ico 挂成主目标的额外依赖，改图标即触发重新链接。
PRE_TARGETDEPS += icon/app_256.ico

# 应用程序清单：由 .rc 以资源类型 24 编入 exe，声明 requestedExecutionLevel="asInvoker"
DISTFILES += app.manifest

# 版本资源（.rc）也要 include user/inc/appinfo.h，windres 需要这个搜索路径
RC_INCLUDEPATH += user/inc

# qmake 生成的规则只让 res.o 依赖 .rc 文件本身，appinfo.h 改动不会触发版本资源重建，
# 会导致 exe 属性里的版本号滞后于窗口标题。这里在链接前始终重新生成一次资源对象，
# 保证「改一处、全都一致」。$(RES_FILE) 是 Makefile 里的变量，由 make 展开。
QMAKE_PRE_LINK += $$QMAKE_RC -i $$RC_FILE -o $(RES_FILE) --include-dir=$$PWD --include-dir=$$PWD/user/inc

# 写注册表 HKCU\Environment（advapi32）+ 广播 WM_SETTINGCHANGE（user32）
LIBS += -ladvapi32 -luser32
