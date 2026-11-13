// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbController.h"
#include "core/AdsbReceiver.h"
#include <QByteArray>
#include <QTimer>
namespace dsdr::core {
AdsbController::AdsbController(AdsbReceiver *receiver, QObject *parent)
    : QObject(parent), m_receiver(receiver) {
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AdsbController::poll);
    timer->start(200);
}
bool AdsbController::enabled() const { return m_receiver && m_receiver->enabled(); }
void AdsbController::setEnabled(bool value) {
    if (!m_receiver) return;
    m_receiver->setEnabled(value); m_cpr.clear(); m_position.reset(); m_lastFrame.clear(); m_message = {}; m_count = 0; poll(); emit changed();
}
QString AdsbController::status() const {
    if (!m_receiver) return tr("Ricevitore ADS-B in arresto");
    switch (m_receiver->status()) {
    case AdsbReceiver::Status::Disabled: return tr("Decoder disattivato");
    case AdsbReceiver::Status::NoSource: return tr("Collega una sorgente IQ");
    case AdsbReceiver::Status::AudioSource: return tr("Errore: è necessaria una sorgente IQ, non audio");
    case AdsbReceiver::Status::WrongRate: return tr("Errore: servono esattamente 2.000.000 campioni/s; attuali: %1").arg(m_receiver->sampleRate(), 0, 'g', 15);
    case AdsbReceiver::Status::WrongFrequency: return tr("Sintonizza manualmente il centro a 1090 MHz");
    case AdsbReceiver::Status::Active: return tr("Ricezione ADS-B attiva a 1090 MHz");
    }
    return {};
}
qulonglong AdsbController::dropped() const { return m_receiver ? m_receiver->dropped() : m_previousDropped; }
void AdsbController::detach() noexcept {
    if (!m_receiver) return;
    m_receiver->setEnabled(false);
    m_previousDropped=m_receiver->dropped(); m_receiver=nullptr;
    m_cpr.clear(); m_position.reset(); m_lastFrame.clear(); m_message={}; m_count=0;
    emit changed();
}
void AdsbController::poll() {
    if (!m_receiver) return;
    bool changedNow = false;
    const auto generation = m_receiver->generation();
    if (generation != m_generation) {
        m_generation = generation; m_cpr.clear(); m_position.reset(); m_message = {}; m_lastFrame.clear(); changedNow = true;
    }
    AdsbReceiver::Message received;
    // Keep each GUI update bounded even if the decoder keeps producing frames.
    // The next timer tick will process any remaining messages.
    for (int i=0; i<64 && m_receiver->read(received); ++i) {
        // A generation can change while poll() drains the queue. Never pair
        // CPR halves from different generations in the same polling pass.
        if (received.generation != m_generation) {
            m_generation = received.generation; m_cpr.clear(); m_position.reset(); m_message = {}; m_lastFrame.clear(); changedNow = true;
        }
        const auto &frame = received.frame;
        const auto nextMessage = AdsbMessage::decode(frame);
        if (nextMessage.icao != m_message.icao) m_position.reset();
        if (auto position = m_cpr.update(frame, received.timestamp)) m_position = position;
        m_message = nextMessage;
        m_lastFrame = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(frame.data()), int(frame.size())).toHex().toUpper());
        ++m_count; changedNow = true;
    }
    if (m_position && AdsbCpr::Clock::now() - m_position->timestamp > std::chrono::seconds(10)) {
        m_position.reset(); changedNow = true;
    }
    const auto current = status();
    if (current != m_previousStatus || dropped() != m_previousDropped) {
        m_previousStatus = current; m_previousDropped = dropped(); changedNow = true;
    }
    if (changedNow) emit changed();
}
}
