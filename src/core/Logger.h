#pragma once

#include <QQmlError>
#include <QStringList>

namespace cwn {
namespace Log {

// 安装全局消息处理器：stderr + logs/ClassWidgetsNext-<date>.log（约 1MB 轮转）。
// 对应上游 loguru 初始化与 central.py:_setup_logging 的 logs 目录约定。
void install(const QString &logsDir, bool fileLogging = true);

// 转发 QQmlApplicationEngine::warnings，带文件与行号（T4 验收项）。
void reportQmlWarnings(const QList<QQmlError> &warnings);

// 供各模块直接写日志行（自动带时间戳前缀）。
void debug(const QString &message);
void info(const QString &message);
void warn(const QString &message);
void error(const QString &message);

} // namespace Log
} // namespace cwn
