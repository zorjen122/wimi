#pragma once

#include "domain/ClientSnapshot.h"

#include <QAbstractListModel>

namespace wimi::client
{

class RequestListModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countsChanged)
    Q_PROPERTY(int resolvedCount READ resolvedCount NOTIFY countsChanged)

  public:
    enum Role {
        RequestIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        MessageRole,
        AvatarColorRole,
        KindRole,
        StatusRole,
    };
    Q_ENUM(Role)

    explicit RequestListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int pendingCount() const;
    int resolvedCount() const;

    void SetRecords(QVector<RequestRecord> records);
    const RequestRecord* RecordAt(int index) const;
    bool SetStatus(int index, const QString& status);

  signals:
    void countsChanged();

  private:
    QVector<RequestRecord> records_;
};

} // namespace wimi::client
