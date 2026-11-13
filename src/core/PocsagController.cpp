// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PocsagController.h"
#include "core/PocsagReceiver.h"
#include <QTimer>
#include <algorithm>
#include <stdexcept>
namespace dsdr::core {
int PocsagHistoryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}
QVariant PocsagHistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto &entry = m_entries[static_cast<std::size_t>(index.row())];
    switch (role) {
    case AddressRole: return entry.address;
    case FunctionRole: return entry.function;
    case TextRole: return entry.text;
    case CorrectedBitsRole: return entry.correctedBits;
    default: return {};
    }
}
QHash<int, QByteArray> PocsagHistoryModel::roleNames() const {
    return {{AddressRole, "address"}, {FunctionRole, "functionCode"},
            {TextRole, "messageText"}, {CorrectedBitsRole, "correctedBits"}};
}
void PocsagHistoryModel::appendBatch(const std::vector<Entry> &entries) {
    if (entries.empty()) return;
    const auto keep = std::min<std::size_t>(entries.size(), Capacity);
    const auto removed = std::max(0, rowCount() + static_cast<int>(keep) - Capacity);
    if (removed) {
        beginRemoveRows({}, 0, removed - 1);
        for (int i=0;i<removed;++i) m_entries.pop_front();
        endRemoveRows();
    }
    const int first = rowCount();
    beginInsertRows({}, first, first + static_cast<int>(keep) - 1);
    m_entries.insert(m_entries.end(), entries.end() - static_cast<std::ptrdiff_t>(keep), entries.end());
    endInsertRows();
}
void PocsagHistoryModel::clear() {
    if (m_entries.empty()) return;
    beginResetModel(); m_entries.clear(); endResetModel();
}
PocsagController::PocsagController(PocsagReceiver *receiver, QObject *parent)
    : QObject(parent), m_receiver(receiver) {
    if (!receiver) throw std::invalid_argument("missing POCSAG receiver");
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PocsagController::poll);
    timer->start(100);
}
void PocsagController::setEnabled(bool value) {
    if (!m_receiver) return;
    if (m_enabled == value) return;
    m_enabled = value; emit enabledRequested(value); emit changed();
}
bool PocsagController::configureRequested(int baud, qint64 frequencyHz) {
    if (!m_receiver) return false;
    if ((baud != 512 && baud != 1200 && baud != 2400) || frequencyHz <= 0) return false;
    m_baud = baud; m_frequencyHz = frequencyHz;
    emit configurationRequested(baud, frequencyHz); emit changed(); return true;
}
void PocsagController::setActive(bool value) {
    if (!m_receiver) return;
    if (m_active == value) return;
    m_active = value; emit changed();
}
QString PocsagController::status() const {
    if (!m_receiver) return tr("Ricevitore POCSAG in arresto");
    if (m_active && !m_enabled) return tr("Arresto richiesto");
    if (m_active) return tr("Ricevitore attivo");
    if (m_enabled && m_receiver->managedStatus()==PocsagReceiver::ManagedStatus::InvalidConfiguration)
        return tr("Sorgente IQ, frequenza o campionamento non supportati");
    if (m_enabled && m_receiver->managedStatus()==PocsagReceiver::ManagedStatus::Error)
        return tr("Errore del decoder POCSAG");
    if (m_enabled) return tr("In attesa della sorgente IQ");
    return tr("Ricevitore disattivato");
}
void PocsagController::detach() {
    if (!m_receiver) return;
    m_receiver = nullptr; m_enabled = false; m_active = false;
    m_messages.clear(); m_count = 0;
    emit changed();
}
qulonglong PocsagController::droppedMessages() const { return m_receiver ? m_receiver->droppedMessages() : 0; }
qulonglong PocsagController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : 0; }
void PocsagController::poll() {
    if (!m_receiver) return;
    bool dirty = false;
    const auto currentStatus=status();
    if (currentStatus!=m_lastStatus) { m_lastStatus=currentStatus; dirty=true; }
    const auto generation = m_receiver->generation();
    if (generation != m_generation) { m_generation = generation; m_count = 0; m_messages.clear(); dirty = true; }
    PocsagReceiver::Message message;
    std::vector<PocsagHistoryModel::Entry> entries;
    entries.reserve(PollBatch);
    for (int i=0;i<PollBatch;++i) {
        const auto result = m_receiver->readOne(message);
        if (result == PocsagReceiver::ReadResult::Empty) break;
        if (result == PocsagReceiver::ReadResult::Stale) continue;
        entries.push_back({message.address, message.function,
            QString::fromLatin1(message.text.data(), static_cast<qsizetype>(message.size)), message.correctedBits});
    }
    if (!entries.empty() && m_receiver->generation()==m_generation) {
        m_messages.appendBatch(entries);
        if(m_receiver->generation()==m_generation) m_count += entries.size();
        dirty = true;
    }
    const auto after=m_receiver->generation();
    if(after!=m_generation) { m_generation=after; m_count=0; m_messages.clear(); dirty=true; }
    if (m_oldDroppedMessages != droppedMessages() || m_oldDroppedBlocks != droppedBlocks()) {
        m_oldDroppedMessages = droppedMessages(); m_oldDroppedBlocks = droppedBlocks(); dirty = true;
    }
    if (dirty) emit changed();
}
}
