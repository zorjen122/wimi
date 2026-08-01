#include "models/ContactListModel.h"

#include <utility>

namespace wimi::client
{

ContactListModel::ContactListModel(QObject* parent) : QAbstractListModel(parent) {}

int ContactListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : records_.size(); }

QVariant ContactListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= records_.size()) {
        return {};
    }

    const auto& record = records_.at(index.row());
    switch (role) {
    case UserIdRole:
        return record.userId;
    case SourceIndexRole:
        return index.row();
    case DisplayNameRole:
        return record.displayName;
    case StatusTextRole:
        return record.statusText;
    case AvatarColorRole:
        return record.avatarColor;
    case OnlineRole:
        return record.online;
    case FavoriteRole:
        return record.favorite;
    default:
        return {};
    }
}

QHash<int, QByteArray> ContactListModel::roleNames() const
{
    return {
        {UserIdRole, "userId"},         {SourceIndexRole, "sourceIndex"}, {DisplayNameRole, "displayName"},
        {StatusTextRole, "statusText"}, {AvatarColorRole, "avatarColor"}, {OnlineRole, "online"},
        {FavoriteRole, "favorite"},
    };
}

void ContactListModel::SetRecords(QVector<ContactRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

const ContactRecord* ContactListModel::RecordAt(int index) const
{
    if (index < 0 || index >= records_.size()) {
        return nullptr;
    }
    return &records_.at(index);
}

bool ContactListModel::ToggleFavorite(int row)
{
    if (row < 0 || row >= records_.size()) {
        return false;
    }
    records_[row].favorite = !records_[row].favorite;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {FavoriteRole});
    return true;
}

} // namespace wimi::client
