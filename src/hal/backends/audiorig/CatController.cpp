// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/audiorig/CatController.h"
#include "hal/HalLog.h"

#include <QTimer>

namespace dsdr::hal::audiorig {

namespace {
/// SPEC-004 §2: 5 Hz per frequenza e modo. Più veloce non serve — il VFO di
/// una radio lo gira una mano — e ogni giro costa attese sulla seriale.
constexpr int kDefaultPollMs = 200;

/// Tre letture a vuoto di fila prima di dichiarare perso il CAT. Una sola
/// sarebbe troppo poco: basta un comando andato storto mentre la radio era
/// occupata a cambiare banda.
constexpr int kFailuresBeforeLost = 3;
} // namespace

CatController::CatController(std::unique_ptr<ICatDriver> driver, QObject *parent)
    : QObject(parent)
    , m_driver(std::move(driver))
{
}

CatController::~CatController()
{
    close();
}

void CatController::open(const QString &portName, int baudRate)
{
    if (!m_driver)
        return;

    const QList<int> candidates = m_driver->candidateBaudRates();

    bool ok = false;
    if (baudRate > 0) {
        ok = m_driver->open(portName, baudRate);
    } else if (candidates.isEmpty()) {
        // Un driver che non propone velocità non ha una linea seriale da
        // regolare — è il caso di rigctld, che parla su TCP. Si apre e basta:
        // il ciclo qui sotto, su un elenco vuoto, non aprirebbe mai e il
        // device fallirebbe con «la radio non risponde», che sarebbe falso.
        ok = m_driver->open(portName, 0);
    } else {
        for (int rate : candidates) {
            if (m_driver->open(portName, rate)) {
                baudRate = rate;
                ok = true;
                break;
            }
        }
    }

    if (!ok) {
        emit lost(m_driver->errorString());
        return;
    }

    emit opened(m_driver->radioModel(), portName, baudRate);

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kDefaultPollMs);
        connect(m_timer, &QTimer::timeout, this, &CatController::poll);
    }
    m_failures = 0;
    m_timer->start();
    poll();   // la prima lettura subito: il pannello non deve aspettare
}

void CatController::close()
{
    if (m_timer)
        m_timer->stop();
    if (m_driver)
        m_driver->close();
}

void CatController::setPollInterval(int milliseconds)
{
    if (m_timer && milliseconds > 0)
        m_timer->setInterval(milliseconds);
}

void CatController::poll()
{
    if (!m_driver || !m_driver->isOpen())
        return;

    if (!m_driver->poll(m_state)) {
        if (++m_failures >= kFailuresBeforeLost) {
            m_timer->stop();
            emit lost(tr("La radio ha smesso di rispondere al CAT."));
        }
        return;
    }

    m_failures = 0;
    emit stateRead(m_state.frequencyHz, static_cast<int>(m_state.mode),
                   m_state.transmitting, m_state.sMeterRaw, m_state.signalDbm);
}

void CatController::setFrequency(qint64 hz)
{
    if (m_driver && m_driver->isOpen() && !m_driver->setFrequency(hz))
        qCWarning(dsdrHal) << "audiorig: la radio ha rifiutato la frequenza" << hz;
}

void CatController::setMode(int mode)
{
    if (m_driver && m_driver->isOpen())
        m_driver->setMode(static_cast<DemodMode>(mode));
}

void CatController::setPtt(bool transmit)
{
    if (m_driver && m_driver->isOpen() && !m_driver->setPtt(transmit)) {
        // Un PTT che non arriva è la cosa peggiore che possa succedere qui: se
        // fallisce lo *spegnimento*, la radio resta in trasmissione.
        qCWarning(dsdrHal) << "audiorig: comando PTT non riuscito" << transmit;
    }
}

} // namespace dsdr::hal::audiorig
