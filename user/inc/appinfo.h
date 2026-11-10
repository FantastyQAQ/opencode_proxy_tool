/**
 * @file appinfo.h
 * @brief 应用元信息集中定义：工具名 / 版本号 / 作者 / 邮箱 / 构建日期
 *
 * 这些宏被主窗口标题、「关于」对话框与 Windows VERSIONINFO 资源共同引用，
 * 改品牌信息只改本文件，避免多处硬编码、改一处漏一处。
 */

#ifndef APPINFO_H
#define APPINFO_H

#ifdef __cplusplus
extern "C" {
#endif

/// @name 用户可见的品牌信息
/// @{

/// 工具名称（窗口标题、exe 属性、关于对话框都显示它）
#define APP_NAME            "OpenCode 代理配置工具"

/// 原始文件名（VERSIONINFO OriginalFilename，必须与最终 exe 名字一致）
#define APP_ORIGINAL_FILENAME  "opencode_proxy_tool.exe"

/// 发布者（VERSIONINFO CompanyName / LegalCopyright 用）
#define APP_AUTHOR_NAME     "lch"

/// 作者邮箱
#define APP_AUTHOR_MAIL     "fantasylch@foxmail.com"

/// 版权年份
#define APP_COPYRIGHT_YEAR  "2026"

/// 显示用的简短版本号（窗口标题、关于对话框都用这个）
#define APP_VERSION         "SV01.001"

/// 数字版本号（exe 属性的文件版本 / 产品版本，四段：主.次.补丁.构建）
/// @note 必须与 APP_VERSION 同步，末三位一一对应（SV01.001 对应 1,0,0,1）
#define APP_VERSION_NUM     1,0,0,1

/// 构建日期标签（形如 261110，即 26 年 11 月 10 日）
#define APP_BUILD_DATE      "261110"

/// @}

/// @name 派生宏
/// @{

/// 完整标题：应用名 + 版本 + 构建日期
#define APP_FULL_TITLE      APP_NAME " " APP_VERSION " [" APP_BUILD_DATE "]"

/// 产品版本（VERSIONINFO ProductVersion，带构建日期标签）
#define APP_PRODUCT_VERSION APP_VERSION " [" APP_BUILD_DATE "]"

/// @}

#ifdef __cplusplus
}
#endif

#endif // APPINFO_H
