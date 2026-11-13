// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AprsController.h"
#include "core/AprsReceiver.h"
#include <QByteArray>
#include <QTimer>
#include <QVariantMap>
namespace dsdr::core {
namespace {
QString address(const std::uint8_t *p) {
    QByteArray name; for (int i=0;i<6;++i) name.append(char(p[i] >> 1));
    auto result = QString::fromLatin1(name.trimmed());
    int ssid = (p[6] >> 1) & 15;
    if (ssid) result += QStringLiteral("-%1").arg(ssid);
    return result;
}
}
AprsController::AprsController(AprsReceiver *receiver, QObject *parent, PollHook hook, void* hookContext)
    : QObject(parent), m_receiver(receiver), m_pollHook(hook), m_hookContext(hookContext) {
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AprsController::poll); timer->start(100);
}
void AprsController::detach() {
    if (!m_receiver) return;
    m_receiver->setEnabled(false);
    m_receiver = nullptr;
    m_source.clear(); m_destination.clear(); m_information.clear(); m_lastFrame.clear(); m_position.reset();
    emit changed();
}
bool AprsController::enabled() const { return m_receiver && m_receiver->enabled(); }
void AprsController::setEnabled(bool value) { if (!m_receiver) return; m_receiver->setEnabled(value); poll(); emit changed(); }
int AprsController::selectedChannel() const { return m_receiver ? m_receiver->selectedChannel() : -1; }
void AprsController::selectChannel(int id) {
    if (!m_receiver) return;
    if (id >= 0) {
        bool found = false;
        for (const auto &v : m_channels) if (v.toMap().value(QStringLiteral("id")).toInt() == id) { found = true; break; }
        if (!found) return;
    }
    m_receiver->selectChannel(id); poll(); emit changed();
}
void AprsController::setChannels(const QVariantList &channels) {
    if (channels == m_channels) return;
    m_channels = channels;
    bool found = selectedChannel() < 0;
    for (const auto &v : m_channels) if (v.toMap().value(QStringLiteral("id")).toInt() == selectedChannel()) found = true;
    if (!found && m_receiver) m_receiver->selectChannel(-1);
    emit channelsChanged(); poll(); emit changed();
}
QString AprsController::status() const {
    if (!m_receiver) return tr("Decoder disattivato");
    switch (m_receiver->status()) {
    case AprsReceiver::Status::Disabled: return tr("Decoder disattivato");
    case AprsReceiver::Status::NoChannel: return tr("Seleziona un canale NFM");
    case AprsReceiver::Status::NoSource: return tr("Canale NFM o sorgente IQ non disponibili");
    case AprsReceiver::Status::NotNfm: return tr("Il canale selezionato non è NFM");
    case AprsReceiver::Status::WrongRate: return tr("Frequenza audio non supportata");
    case AprsReceiver::Status::Active: return tr("Ricezione APRS attiva");
    }
    return {};
}
qulonglong AprsController::droppedFrames() const { return m_receiver ? m_receiver->droppedFrames() : 0; }
qulonglong AprsController::droppedBlocks() const { return m_receiver ? m_receiver->stats().droppedBlocks : 0; }
void AprsController::poll() {
    if (!m_receiver) return;
    bool dirty = false;
    const auto generation = m_receiver->generation();
    if (generation != m_generation) {
        m_generation = generation; m_source.clear(); m_destination.clear(); m_information.clear(); m_lastFrame.clear(); m_position.reset(); dirty = true;
    }
    AprsReceiver::Message m; unsigned accepted=0;
    // Bound work on the GUI thread when a busy channel fills the queue.
    // The timer will drain remaining messages on the next event-loop pass.
    for (unsigned batch = 0; batch < 64; ++batch) {
        const auto result = m_receiver->readOne(m);
        if (result == AprsReceiver::ReadResult::Empty) break;
        if (result == AprsReceiver::ReadResult::Stale) continue;
        m_destination = address(m.bytes.data()); m_source = address(m.bytes.data()+7);
        std::size_t pos = 0;
        while (pos + 7 <= m.size) { bool last = m.bytes[pos+6] & 1; pos += 7; if (last) break; }
        pos += 2;
        m_information = QString::fromLatin1(reinterpret_cast<const char *>(m.bytes.data()+pos), int(m.size-pos));
        m_position = parseAprsPosition(QStringView(m_information));
        m_lastFrame = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(m.bytes.data()), int(m.size)).toHex().toUpper());
        ++accepted; dirty = true;
        if(m_pollHook) m_pollHook(m_hookContext);
    }
    const auto after=m_receiver->generation();
    if(after!=m_generation) {
        m_generation=after; m_source.clear(); m_destination.clear(); m_information.clear(); m_lastFrame.clear(); m_position.reset(); dirty=true;
    } else m_count+=accepted;
    const auto now = status();
    if (now != m_previousStatus || droppedFrames() != m_oldDroppedFrames || droppedBlocks() != m_oldDroppedBlocks) {
        m_previousStatus = now; m_oldDroppedFrames = droppedFrames(); m_oldDroppedBlocks = droppedBlocks(); dirty = true;
    }
    if (dirty) emit changed();
}
}
