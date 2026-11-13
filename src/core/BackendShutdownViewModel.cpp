// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BackendShutdownViewModel.h"
namespace dsdr::core {
BackendShutdownViewModel::BackendShutdownViewModel(QObject *parent) : QObject(parent) {
    m_timer.setInterval(20);
    connect(&m_timer, &QTimer::timeout, this, &BackendShutdownViewModel::poll);
}
BackendShutdown::BeginResult BackendShutdownViewModel::beginForAuditedBackend(hal::IRadioBackend *backend, BackendShutdown::ChildPolicy policy) {
    const auto result = m_shutdown.beginForAuditedBackend(backend, policy);
    if (result == BackendShutdown::BeginResult::Accepted) { m_timer.start(); poll(); }
    return result;
}
QString BackendShutdownViewModel::statusText() const {
    using S = BackendShutdown::Status;
    switch (m_status) {
    case S::Idle: return tr("Chiusura non avviata.");
    case S::Running: return tr("Attendo la chiusura della radio.");
    case S::PttActive: return tr("Il PTT risulta ancora attivo. Verifica la radio e riprova.");
    case S::BackendStillOpen: return tr("La radio risulta ancora aperta. Verifica la connessione e riprova.");
    case S::Exception: return tr("Errore durante la chiusura della radio. Riprova.");
    case S::QueueFailed: return tr("Impossibile inviare la richiesta di chiusura. Riprova.");
    case S::Closed: return m_ready ? tr("Radio chiusa.") : tr("Attendo la conclusione del thread radio.");
    }
    return {};
}
bool BackendShutdownViewModel::retryAllowed() const noexcept {
    using S = BackendShutdown::Status;
    return m_status == S::PttActive || m_status == S::BackendStillOpen || m_status == S::Exception || m_status == S::QueueFailed;
}
void BackendShutdownViewModel::retry() { if (retryAllowed() && m_shutdown.retry()) poll(); }
void BackendShutdownViewModel::poll() {
    const auto status = m_shutdown.status();
    const bool ready = m_shutdown.readyToExit();
    if (m_status != status || m_ready != ready) { m_status = status; m_ready = ready; emit changed(); }
    if (m_ready) m_timer.stop();
}
} // namespace dsdr::core
