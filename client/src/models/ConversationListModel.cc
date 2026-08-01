#include "models/ConversationListModel.h"

#include <utility>

namespace wimi::client
{

ConversationListModel::ConversationListModel(QObject* parent) : QAbstractListModel(parent) {}

int ConversationListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : records_.size(); }

QVariant ConversationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= records_.size()) {
        return {};
    }

    const auto& record = records_.at(index.row());
    switch (role) {
    case ConversationIdRole:
        return record.conversationId;
    case SourceIndexRole:
        return index.row();
    case TitleRole:
        return record.title;
    case PreviewRole:
        return record.preview;
    case TimestampRole:
        return record.timestamp;
    case AvatarColorRole:
        return record.avatarColor;
    case RemoteConversationIdRole:
        return record.remoteConversationId.has_value() ? QVariant::fromValue<qlonglong>(*record.remoteConversationId)
                                                       : QVariant{};
    case UnreadCountRole:
        return record.unreadCount;
    case PinnedRole:
        return record.pinned;
    case MutedRole:
        return record.muted;
    case OnlineRole:
        return record.online;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {
        {ConversationIdRole, "conversationId"},
        {SourceIndexRole, "sourceIndex"},
        {TitleRole, "title"},
        {PreviewRole, "preview"},
        {TimestampRole, "timestamp"},
        {AvatarColorRole, "avatarColor"},
        {RemoteConversationIdRole, "remoteConversationId"},
        {UnreadCountRole, "unreadCount"},
        {PinnedRole, "pinned"},
        {MutedRole, "muted"},
        {OnlineRole, "online"},
    };
}

void ConversationListModel::SetRecords(QVector<ConversationRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

void ConversationListModel::Prepend(ConversationRecord record)
{
    beginInsertRows({}, 0, 0);
    records_.prepend(std::move(record));
    endInsertRows();
}

const ConversationRecord* ConversationListModel::RecordAt(int index) const
{
    if (index < 0 || index >= records_.size()) {
        return nullptr;
    }
    return &records_.at(index);
}

int ConversationListModel::IndexOf(const QString& conversationId) const
{
    for (int index = 0; index < records_.size(); ++index) {
        if (records_[index].conversationId == conversationId) {
            return index;
        }
    }
    return -1;
}

bool ConversationListModel::SetRemoteConversationId(const QString& conversationId, std::int64_t remoteConversationId)
{
    for (int row = 0; row < records_.size(); ++row) {
        auto& record = records_[row];
        if (record.conversationId != conversationId || record.remoteConversationId == remoteConversationId) {
            continue;
        }
        record.remoteConversationId = remoteConversationId;
        const auto modelIndex = index(row);
        emit dataChanged(modelIndex, modelIndex, {RemoteConversationIdRole});
        return true;
    }
    return false;
}

void ConversationListModel::UpdatePreview(const QString& conversationId, const QString& preview,
                                          const QString& timestamp)
{
    for (int row = 0; row < records_.size(); ++row) {
        auto& record = records_[row];
        if (record.conversationId != conversationId) {
            continue;
        }
        record.preview = preview;
        record.timestamp = timestamp;
        const auto modelIndex = index(row);
        emit dataChanged(modelIndex, modelIndex, {PreviewRole, TimestampRole});
        return;
    }
}

bool ConversationListModel::SetUnreadCount(const QString& conversationId, int unreadCount)
{
    for (int row = 0; row < records_.size(); ++row) {
        auto& record = records_[row];
        if (record.conversationId != conversationId || record.unreadCount == unreadCount) {
            continue;
        }
        record.unreadCount = unreadCount;
        const auto modelIndex = index(row);
        emit dataChanged(modelIndex, modelIndex, {UnreadCountRole});
        return true;
    }
    return false;
}

bool ConversationListModel::TogglePinned(int row)
{
    if (row < 0 || row >= records_.size()) {
        return false;
    }
    records_[row].pinned = !records_[row].pinned;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {PinnedRole});
    return true;
}

bool ConversationListModel::ToggleMuted(int row)
{
    if (row < 0 || row >= records_.size()) {
        return false;
    }
    records_[row].muted = !records_[row].muted;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {MutedRole});
    return true;
}

bool ConversationListModel::MarkRead(int row)
{
    if (row < 0 || row >= records_.size() || records_[row].unreadCount == 0) {
        return false;
    }
    records_[row].unreadCount = 0;
    const auto modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, {UnreadCountRole});
    return true;
}

} // namespace wimi::client
