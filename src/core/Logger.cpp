#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

namespace cwn {
namespace Log {

namespace {

constexpr qint64 kMaxLogSize = 1024 * 1024; // 与上游 rotation="1 MB" 对齐
constexpr int kMaxLogFiles = 7;             // retention="7 days" 的简化：最多 7 个轮转文件

QMutex g_mutex;
QString g_logsDir;
bool g_fileLogging = false;
QFile g_file;

QString levelTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO ");
    case QtWarningMsg:  return QStringLiteral("WARN ");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG  ");
}

void rotateIfNeeded()
{
    if (!g_fileLogging || !g_file.isOpen())
        return;
    if (g_file.size() < kMaxLogSize)
        return;

    const QString base = g_logsDir + QStringLiteral("/ClassWidgetsNext");
    // ClassWidgetsNext.log -> ClassWidgetsNext.1.log -> ... 逐个后移
    g_file.close();
    QFile::remove(base + QStringLiteral(".%1.log").arg(kMaxLogFiles));
    for (int i = kMaxLogFiles - 1; i >= 1; --i) {
        const QString from = base + QStringLiteral(".%1.log").arg(i);
        const QString to = base + QStringLiteral(".%1.log").arg(i + 1);
        QFile::rename(from, to);
    }
    QFile::rename(base + QStringLiteral(".log"), base + QStringLiteral(".1.log"));

    g_file.setFileName(base + QStringLiteral(".log"));
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void writeLine(const QString &line)
{
    QMutexLocker locker(&g_mutex);
    QTextStream(stderr) << line << '\n';

    if (g_fileLogging) {
        if (!g_file.isOpen()) {
            QDir().mkpath(g_logsDir);
            g_file.setFileName(g_logsDir + QStringLiteral("/ClassWidgetsNext.log"));
            g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }
        if (g_file.isOpen()) {
            rotateIfNeeded();
            QTextStream ts(&g_file);
            ts.setEncoding(QStringConverter::Utf8);
            ts << line << '\n';
            ts.flush();
        }
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString where;
    if (context.file && *context.file)
        where = QStringLiteral(" (%1:%2)").arg(QFileInfo(context.file).fileName()).arg(context.line);

    writeLine(QStringLiteral("[%1] [%2]%3 %4")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                       levelTag(type), where, message));
}

} // namespace

void install(const QString &logsDir, bool fileLogging)
{
    QMutexLocker locker(&g_mutex);
    g_logsDir = logsDir;
    g_fileLogging = fileLogging;
    qInstallMessageHandler(messageHandler);
}

void reportQmlWarnings(const QList<QQmlError> &warnings)
{
    for (const QQmlError &e : warnings)
        writeLine(QStringLiteral("[%1] [QML ] %2").arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
            e.toString()));
}

void debug(const QString &message)
{
    messageHandler(QtDebugMsg, QMessageLogContext(), message);
}

void info(const QString &message)
{
    messageHandler(QtInfoMsg, QMessageLogContext(), message);
}

void warn(const QString &message)
{
    messageHandler(QtWarningMsg, QMessageLogContext(), message);
}

void error(const QString &message)
{
    messageHandler(QtCriticalMsg, QMessageLogContext(), message);
}

} // namespace Log
} // namespace cwn
