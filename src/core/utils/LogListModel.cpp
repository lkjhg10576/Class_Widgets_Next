#include "LogListModel.h"

#include <QByteArray>

namespace cwn {
namespace utils {

// ── LogListModel（log_list_model.py:4-80）────────────────────────

LogListModel::LogListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LogListModel::rowCount(const QModelIndex &parent) const
{
    // log_list_model.py:26-29：树形父索引下无子行
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QHash<int, QByteArray> LogListModel::roleNames() const
{
    // log_list_model.py:31-36
    return {
        { TimeRole, QByteArrayLiteral("time") },
        { LevelRole, QByteArrayLiteral("level") },
        { MessageRole, QByteArrayLiteral("message") },
    };
}

QVariant LogListModel::data(const QModelIndex &index, int role) const
{
    // log_list_model.py:38-51
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_entries.size())
        return {};
    const QVariantMap &entry = m_entries.at(row);
    switch (role) {
    case TimeRole:
        return entry.value(QStringLiteral("time")).toString();
    case LevelRole:
        return entry.value(QStringLiteral("level")).toString();
    case MessageRole:
        return entry.value(QStringLiteral("message")).toString();
    default:
        return {};
    }
}

void LogListModel::appendEntry(const QVariantMap &entry)
{
    // log_list_model.py:53-66：超出容量先移除最旧一条再追加
    if (m_entries.size() >= kMaxLogLines) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
    }

    const int newIndex = m_entries.size();
    beginInsertRows(QModelIndex(), newIndex, newIndex);
    m_entries.append(entry);
    endInsertRows();
}

QVariantList LogListModel::snapshot(int limit) const
{
    // log_list_model.py:68-73：safe_limit = max(0, min(limit, MAX_LOG_LINES))
    int safeLimit = qBound(0, limit, kMaxLogLines);
    if (safeLimit == 0)
        return {};
    QVariantList out;
    out.reserve(safeLimit);
    for (int i = m_entries.size() - safeLimit; i < m_entries.size(); ++i)
        out.append(m_entries.at(i));
    return out;
}

void LogListModel::clear()
{
    // log_list_model.py:75-80
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

// ── LogFilterProxyModel（log_list_model.py:83-125）───────────────

LogFilterProxyModel::LogFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void LogFilterProxyModel::setFilterText(const QString &text)
{
    m_filterText = text.toLower();
    invalidateFilter();
}

void LogFilterProxyModel::setFilterLevel(const QString &level)
{
    m_filterLevel = level;
    invalidateFilter();
}

bool LogFilterProxyModel::filterAcceptsRow(int sourceRow,
                                          const QModelIndex &sourceParent) const
{
    // log_list_model.py:105-125
    const QAbstractItemModel *source = sourceModel();
    if (!source)
        return true;
    const QModelIndex index = source->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return true;

    const QString time = index.data(LogListModel::TimeRole).toString();
    const QString level = index.data(LogListModel::LevelRole).toString();
    const QString message = index.data(LogListModel::MessageRole).toString();

    if (!m_filterLevel.isEmpty() && level != m_filterLevel)
        return false;

    if (!m_filterText.isEmpty()) {
        const QString haystack =
            (time + QLatin1Char(' ') + level + QLatin1Char(' ') + message).toLower();
        if (!haystack.contains(m_filterText))
            return false;
    }

    return true;
}

} // namespace utils
} // namespace cwn
