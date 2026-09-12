#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QVariantMap>
#include <QVector>

namespace cwn {
namespace utils {

// 对应上游 src/core/utils/log_list_model.py:4-80 LogListModel —— 暴露给 QML
// Debugger/contents/Dashboard.qml 的日志模型。
//
// 角色（log_list_model.py:16-18）：
//   time    日志时间字符串（HH:mm:ss）
//   level   日志级别（DEBUG/INFO/WARNING/ERROR/SUCCESS）
//   message 日志正文
//
// 用 QAbstractListModel 而非 QVariantList，使 ListView 在新日志到达时收到
// rowsInserted 而不重置 contentY（滚动位置/可见性得以保留，见上游类注释）。
class LogListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    // log_list_model.py:16-18
    enum Roles
    {
        TimeRole = Qt::UserRole + 1,
        LevelRole = Qt::UserRole + 2,
        MessageRole = Qt::UserRole + 3,
    };

    // log_list_model.py:20 MAX_LOG_LINES —— 环形容量，超出即丢最旧条目
    static constexpr int kMaxLogLines = 200;

    explicit LogListModel(QObject *parent = nullptr);

    // QAbstractListModel 契约（log_list_model.py:26-51）
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // log_list_model.py:53-66 append_entry：末尾追加；超出容量先移除最旧一条。
    // 必须在 GUI 线程调用（内部触发 begin/endInsertRows）——UtilsBackend 通过
    // 队列信号把日志条目 marshal 回 GUI 线程后再调用本方法。
    void appendEntry(const QVariantMap &entry);

    // log_list_model.py:68-73 snapshot：返回最近 limit 条日志的浅拷贝
    QVariantList snapshot(int limit = kMaxLogLines) const;

    // log_list_model.py:75-80 clear：清空模型
    void clear();

private:
    QVector<QVariantMap> m_entries;
};

// 对应上游 log_list_model.py:83-125 LogFilterProxyModel —— 对 LogListModel 做
// 客户端过滤，两种维度可叠加：
//   文本：不区分大小写子串匹配，命中 time/level/message 任一即保留
//   级别：精确匹配（DEBUG/INFO/WARNING/ERROR/SUCCESS），空串 = 不过滤
class LogFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit LogFilterProxyModel(QObject *parent = nullptr);

public slots:
    // log_list_model.py:97-99 set_filter_text
    void setFilterText(const QString &text);
    // log_list_model.py:101-103 set_filter_level
    void setFilterLevel(const QString &level);

protected:
    // log_list_model.py:105-125 filterAcceptsRow
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_filterText; // 已转小写
    QString m_filterLevel;
};

} // namespace utils
} // namespace cwn
