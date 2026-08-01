#include "models/RequestListModel.h"

#include <algorithm>
#include <utility>

namespace wimi::client
{
namespace
{

bool IsPendingStatus(const QString& status)
{
    return status == QStringLiteral("pending") || status == QStringLiteral("accepting") ||
           status == QStringLiteral("declining");
}

} // namespace

RequestListModel::RequestListModel(QObject* parent) : QAbstractListModel(parent) {}

int RequestListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : records_.size(); }

QVariant RequestListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= records_.size()) {
        return {};
    }

    const auto& record = records_.at(index.row());
    switch (role) {
    case RequestIdRole:
        return record.requestId;
    case DisplayNameRole:
        return record.displayName;
    case MessageRole:
        return record.message;
    case AvatarColorRole:
        return record.avatarColor;
    case KindRole:
        return record.kind;
    case StatusRole:
        return record.status;
    default:
        return {};
    }
}

QHash<int, QByteArray> RequestListModel::roleNames() const
{
    return {
        {RequestIdRole, "requestId"},     {DisplayNameRole, "displayName"}, {MessageRole, "requestMessage"},
        {AvatarColorRole, "avatarColor"}, {KindRole, "requestKind"},        {StatusRole, "requestStatus"},
    };
}

int RequestListModel::pendingCount() const
{
    return static_cast<int>(std::count_if(records_.cbegin(), records_.cend(),
                                          [](const RequestRecord& record) { return IsPendingStatus(record.status); }));
}

int RequestListModel::resolvedCount() const { return records_.size() - pendingCount(); }

void RequestListModel::SetRecords(QVector<RequestRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
    emit countsChanged();
}

const RequestRecord* RequestListModel::RecordAt(int index) const
{
    if (index < 0 || index >= records_.size()) {
        return nullptr;
    }
    return &records_.at(index);
}

bool RequestListModel::SetStatus(int row, const QString& status)
{
    if (row < 0 || row >= records_.size() || records_[row].status == status) {
        return false;
    }
    records_[row].status = status;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {StatusRole});
    emit countsChanged();
    return true;
}

} // namespace wimi::client
