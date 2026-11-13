// SPDX-License-Identifier: GPL-3.0-or-later
#include "Vdl2SessionBinding.h"
#include "SessionManager.h"
#include <stdexcept>

namespace dsdr::core {
namespace {
DspEngine *requireEngine(SessionManager *session, bool reserved) {
    if (!session || session->shutdownPending() || session->externalShutdownPending() != reserved
        || !session->dspEngineForOptionalDecoder())
        throw std::invalid_argument("VDL2 requires an active session and DSP engine");
    return session->dspEngineForOptionalDecoder();
}
}
Vdl2SessionBinding::Vdl2SessionBinding(SessionManager *session, unsigned rate, qint64 center,
                                       double offset, double floor, QObject *parent)
    : Vdl2SessionBinding(session, std::make_unique<Vdl2Resources>(rate, center, offset, floor), parent) {}
Vdl2SessionBinding::Vdl2SessionBinding(SessionManager *session, std::unique_ptr<Vdl2Resources> resources,
                                       QObject *parent, bool gateAlreadyReserved)
    : QObject(parent), m_session(session),
      m_lifecycle(requireEngine(session, gateAlreadyReserved), std::move(resources))
{
    if (!gateAlreadyReserved) session->setExternalShutdownPending(true);
    connect(session, &SessionManager::shutdownCommitted, this, [this] { m_lifecycle.requestShutdown(); });
    connect(session, &SessionManager::shutdownStateChanged, this, &Vdl2SessionBinding::updateShutdown);
    connect(&m_lifecycle, &Vdl2Lifecycle::changed, this, [this] { emit changed(); updateShutdown(); });
}
void Vdl2SessionBinding::updateShutdown()
{
    if (!m_session) return;
    if (m_session->coreShutdownFencePassed() && !m_backendAcknowledged) {
        m_backendAcknowledged = true;
        m_lifecycle.acknowledgeBackendStopped();
    }
    if (m_lifecycle.ready())
        m_session->setExternalShutdownPending(false);
}
}
