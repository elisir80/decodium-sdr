// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2AppManager.h"
#include "SessionManager.h"
#include "CapabilitiesInfo.h"
#include "Vdl2ResourcePreparation.h"
#include "Vdl2SessionBinding.h"
#include <exception>
#include <cmath>

namespace dsdr::core {
Vdl2AppManager::Vdl2AppManager(SessionManager *session, QObject *parent)
    : QObject(parent), m_session(session), m_state(tr("Disattivato")) {
    if (session) connect(session, &SessionManager::shutdownCommitted, this, [this] {
        if (m_preparation) { m_cancelled = true; setState(tr("Arresto in corso")); }
    });
}
Vdl2AppManager::~Vdl2AppManager() = default;
QObject *Vdl2AppManager::controller() const { return m_binding ? m_binding->controller() : nullptr; }
void Vdl2AppManager::setState(const QString &state) {
    if (m_state != state) { m_state = state; emit changed(); }
}
bool Vdl2AppManager::start(unsigned rate, qint64 center, double offset) {
    if (!m_session || m_session->shutdownPending() || m_session->externalShutdownPending()
        || m_preparation || m_binding) return false;
    m_session->setExternalShutdownPending(true);
    m_cancelled = false;
    m_checkSource = false;
    m_preparation = new Vdl2ResourcePreparation(rate, center, offset, 0.001, this);
    connect(m_preparation, &QThread::finished, this, &Vdl2AppManager::prepared);
    setState(tr("Preparazione in corso"));
    m_preparation->start();
    return true;
}
bool Vdl2AppManager::startForFrequency(qint64 frequency) {
    if (!m_session || !m_session->isConnected() || !m_session->capabilities()->clientDemod()) {
        setState(tr("Collega prima una sorgente IQ compatibile"));
        return false;
    }
    const double rate = m_session->sampleRate();
    const qint64 center = m_session->centerFrequency();
    if (!std::isfinite(rate) || rate < 84000 || rate > 2400000 || rate != std::round(rate)) {
        setState(tr("Frequenza di campionamento IQ non supportata"));
        return false;
    }
    if (frequency <= 0 || std::abs(double(frequency) - double(center)) > rate / 2 - 18000) {
        setState(tr("Frequenza VDL2 fuori dalla banda IQ disponibile"));
        return false;
    }
    const auto roundedRate = static_cast<unsigned>(std::llround(rate));
    if (!start(roundedRate, center, double(frequency - center))) return false;
    m_checkSource = true;
    m_expectedRate = roundedRate;
    m_expectedCenter = center;
    return true;
}
void Vdl2AppManager::stop() {
    if (m_preparation) { m_cancelled = true; setState(tr("Arresto in corso")); return; }
    if (m_binding) m_binding->stop();
}
void Vdl2AppManager::prepared() {
    auto *prep = m_preparation;
    m_preparation = nullptr;
    auto resources = prep->takeResources();
    const auto error = prep->error();
    prep->deleteLater();
    if (!m_session) { setState(tr("Sessione non disponibile")); return; }
    if (m_cancelled || m_session->shutdownPending()) {
        resources.reset();
        setState(tr("Arrestato"));
        m_session->setExternalShutdownPending(false);
        return;
    }
    if (m_checkSource && (!m_session->isConnected() || !m_session->capabilities()->clientDemod()
        || m_session->sampleRate() != m_expectedRate || m_session->centerFrequency() != m_expectedCenter)) {
        resources.reset();
        setState(tr("La sorgente IQ è cambiata: riprova"));
        m_session->setExternalShutdownPending(false);
        return;
    }
    if (!resources) {
        setState(tr("Impossibile preparare VDL2: %1").arg(error));
        m_session->setExternalShutdownPending(false);
        return;
    }
    try {
        m_binding = std::make_unique<Vdl2SessionBinding>(m_session, std::move(resources), nullptr, true);
        connect(m_binding.get(), &Vdl2SessionBinding::changed, this, [this] {
            setState(m_binding->state());
            if (m_binding->ready()) QMetaObject::invokeMethod(this, [this] {
                if (!m_binding || !m_binding->ready()) return;
                auto finished = std::move(m_binding);
                emit changed(); // release QML's reference before deleting the controller
            }, Qt::QueuedConnection);
        });
        m_binding->activate();
        emit changed();
    } catch (const std::exception &e) {
        if (!m_binding) m_session->setExternalShutdownPending(false);
        setState(tr("Impossibile avviare VDL2: %1").arg(QString::fromUtf8(e.what())));
    }
}
}
