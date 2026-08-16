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

void CatController::open(const QString &portName, int baudRate, int dataBits,
                         int parity, int stopBits, int flowControl,
                         bool dtr, bool rts, int hamlibModel)
{
    if (!m_driver)
        return;

    CatSerialConfig serial;
    serial.baudRate = baudRate;
    serial.dataBits = dataBits;
    serial.parity = parity;
    serial.stopBits = stopBits;
    serial.flowControl = flowControl;
    serial.dtr = dtr;
    serial.rts = rts;
    serial.hamlibModel = hamlibModel;

    const QList<int> candidates = m_driver->candidateBaudRates();

    bool ok = false;
    if (serial.baudRate > 0) {
        ok = m_driver->open(portName, serial);
    } else if (candidates.isEmpty()) {
        // Un driver che non propone velocità non ha una linea seriale da
        // regolare — è il caso di rigctld, che parla su TCP. Si apre e basta:
        // il ciclo qui sotto, su un elenco vuoto, non aprirebbe mai e il
        // device fallirebbe con «la radio non risponde», che sarebbe falso.
        ok = m_driver->open(portName, serial);
    } else {
        for (int rate : candidates) {
            CatSerialConfig attempt = serial;
            attempt.baudRate = rate;
            if (m_driver->open(portName, attempt)) {
                serial.baudRate = rate;
                ok = true;
                break;
            }
        }
    }

    if (!ok) {
        emit lost(m_driver->errorString());
        return;
    }

    emit opened(m_driver->radioModel(), portName, serial.baudRate);

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kDefaultPollMs);
        connect(m_timer, &QTimer::timeout, this, &CatController::poll);
    }
    if (!m_pttTimer) {
        m_pttTimer = new QTimer(this);
        // Il PTT protegge hardware reale: non lasciamo che il timer coarse
        // allarghi un intervallo breve per risparmiare energia.
        m_pttTimer->setTimerType(Qt::PreciseTimer);
        connect(m_pttTimer, &QTimer::timeout, this, &CatController::pollPtt);
    }
    m_failures = 0;
    m_pttReported = false;
    m_timer->start();
    poll();   // la prima lettura subito: il pannello non deve aspettare
    if (m_pttPollIntervalMs > 0)
        m_pttTimer->start(m_pttPollIntervalMs);
}

void CatController::close()
{
    stopPolling();
    if (m_driver)
        m_driver->close();
}

void CatController::setPollInterval(int milliseconds)
{
    if (m_timer && milliseconds > 0)
        m_timer->setInterval(milliseconds);
}

void CatController::setPttPollInterval(int milliseconds)
{
    m_pttPollIntervalMs = qMax(0, milliseconds);
    if (!m_pttTimer)
        return;
    if (m_pttPollIntervalMs == 0) {
        m_pttTimer->stop();
        return;
    }
    m_pttTimer->start(m_pttPollIntervalMs);
    qCInfo(dsdrHal) << "CAT: protezione PTT dedicata ogni" << m_pttPollIntervalMs << "ms";
}

void CatController::poll()
{
    if (!m_driver || !m_driver->isOpen())
        return;

    if (!m_driver->poll(m_state)) {
        // Per un panadapter IF un CAT che tace non è RX: al primo timeout si
        // ritira il consenso al flusso IQ, senza aspettare la diagnostica di
        // «CAT perso» dopo tre tentativi.
        reportPttUnknown();
        if (++m_failures >= kFailuresBeforeLost) {
            stopPolling();
            emit lost(tr("La radio ha smesso di rispondere al CAT."));
        }
        return;
    }

    m_failures = 0;
    emit stateRead(m_state.frequencyHz, static_cast<int>(m_state.mode),
                   m_state.transmitting, m_state.pttKnown,
                   m_state.sMeterRaw, m_state.signalDbm);
}

void CatController::pollPtt()
{
    if (!m_driver || !m_driver->isOpen())
        return;

    if (!m_driver->pollPtt(m_state)) {
        // Qui non c'è nessun compromesso fra continuità audio e sicurezza:
        // se non possiamo provare che la radio è in RX, l'RTL-SDR deve essere
        // spento subito. La riconnessione resta gestita dai tre fallimenti.
        reportPttUnknown();
        if (++m_failures >= kFailuresBeforeLost) {
            stopPolling();
            emit lost(tr("La radio ha smesso di rispondere al CAT."));
        }
        return;
    }

    m_failures = 0;
    const bool changed = !m_pttReported || m_state.pttKnown != m_lastPttKnown
        || (m_state.pttKnown && m_state.transmitting != m_lastTransmitting);
    if (!changed)
        return;

    m_pttReported = true;
    m_lastPttKnown = m_state.pttKnown;
    m_lastTransmitting = m_state.transmitting;
    // Frequenza zero contraddistingue il percorso di sicurezza: il consumer
    // reagisce al PTT ma non ridisegna né ritocca il VFO per una lettura che
    // deliberatamente non contiene lo stato completo.
    emit stateRead(0, static_cast<int>(m_state.mode),
                   m_state.transmitting, m_state.pttKnown,
                   m_state.sMeterRaw, m_state.signalDbm);
}

void CatController::reportPttUnknown()
{
    m_state.pttKnown = false;
    m_state.transmitting = false;
    if (m_pttReported && !m_lastPttKnown)
        return;
    m_pttReported = true;
    m_lastPttKnown = false;
    m_lastTransmitting = false;
    emit stateRead(0, static_cast<int>(m_state.mode), false, false,
                   m_state.sMeterRaw, m_state.signalDbm);
}

void CatController::stopPolling()
{
    if (m_timer)
        m_timer->stop();
    if (m_pttTimer)
        m_pttTimer->stop();
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
