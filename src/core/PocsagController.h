// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <cstdint>
#include <deque>
#include <vector>

namespace dsdr::core {
class PocsagReceiver;
// UI-thread model: oldest first; evicts oldest when the limit is reached.
class PocsagHistoryModel final : public QAbstractListModel {
    Q_OBJECT
public:
    static constexpr int Capacity = 500;
    struct Entry { std::uint32_t address; unsigned function; QString text; unsigned correctedBits; };
    enum Role { AddressRole = Qt::UserRole + 1, FunctionRole, TextRole, CorrectedBitsRole };
    explicit PocsagHistoryModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void appendBatch(const std::vector<Entry> &entries);
    void clear();
private:
    std::deque<Entry> m_entries;
};
// UI-thread only. Host owns receiver and must outlive controller. Requests are
// signals: a host must dispatch blocking lifecycle work away from the UI thread.
class PocsagController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int baud READ baud NOTIFY changed)
    Q_PROPERTY(qint64 frequencyHz READ frequencyHz NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *messages READ messages CONSTANT)
    Q_PROPERTY(qulonglong messageCount READ messageCount NOTIFY changed)
    Q_PROPERTY(qulonglong droppedMessages READ droppedMessages NOTIFY changed)
    Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY changed)
public:
    static constexpr int PollBatch = 32;
    explicit PocsagController(PocsagReceiver *receiver, QObject *parent = nullptr);
    bool enabled() const { return m_enabled; }
    bool active() const { return m_active; }
    QString status() const;
    int baud() const { return m_baud; }
    qint64 frequencyHz() const { return m_frequencyHz; }
    Q_INVOKABLE bool configureRequested(int baud, qint64 frequencyHz);
    PocsagHistoryModel *messages() { return &m_messages; }
    qulonglong messageCount() const { return m_count; }
    qulonglong droppedMessages() const;
    qulonglong droppedBlocks() const;
    void setEnabled(bool enabled);
    void setActive(bool active);
    // UI thread: sever all receiver access before its shutdown fence.
    void detach();
public slots:
    void poll();
signals:
    void changed();
    void enabledRequested(bool enabled);
    void configurationRequested(int baud, qint64 frequencyHz);
private:
    PocsagReceiver *m_receiver;
    bool m_enabled = false, m_active = false;
    int m_baud = 1200;
    QString m_lastStatus;
    qint64 m_frequencyHz = 0;
    qulonglong m_count = 0, m_oldDroppedMessages = 0, m_oldDroppedBlocks = 0;
    std::uint64_t m_generation = 0;
    PocsagHistoryModel m_messages;
};
}
