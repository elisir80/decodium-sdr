// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QString>
#include <cstdint>
#include <deque>

namespace dsdr::core {
class AcarsReceiver;
class AcarsHistoryModel final : public QAbstractListModel {
    Q_OBJECT
public:
    static constexpr int Capacity = 200;
    struct Entry { QString mode, address, ack, label, blockId, text, ending; };
    enum Role { ModeRole=Qt::UserRole+1, AddressRole, AckRole, LabelRole, BlockIdRole, TextRole, EndingRole };
    explicit AcarsHistoryModel(QObject *parent=nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent={}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    void append(const Entry &entry);
    void clear();
private:
    std::deque<Entry> m_entries;
};
// UI thread only; receiver must outlive this object and its timer.
class AcarsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(qint64 frequencyHz READ frequencyHz NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *messages READ messages CONSTANT)
    Q_PROPERTY(qulonglong messageCount READ messageCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
    Q_PROPERTY(qulonglong workerErrors READ workerErrors NOTIFY changed)
public:
    static constexpr int PollBatch=32;
    explicit AcarsController(AcarsReceiver *receiver,QObject *parent=nullptr);
    bool enabled() const;
    void setEnabled(bool value);
    void detach() noexcept;
    qint64 frequencyHz() const { return m_frequencyHz; }
    Q_INVOKABLE bool setFrequencyHz(qint64 hz);
    QString status() const;
    QAbstractItemModel *messages() { return &m_messages; }
    qulonglong messageCount() const { return m_count; }
    qulonglong droppedMessages() const;
    qulonglong droppedBlocks() const;
    qulonglong workerErrors() const;
    Q_INVOKABLE void clearMessages();
public slots:
    void poll();
signals:
    void changed();
    void frequencyChanged();
private:
    AcarsReceiver *m_receiver;
    AcarsHistoryModel m_messages;
    std::uint64_t m_generation=0;
    qint64 m_frequencyHz=0;
    qulonglong m_count=0,m_oldMessages=0,m_oldBlocks=0,m_oldErrors=0;
    QString m_oldStatus;
};
}
