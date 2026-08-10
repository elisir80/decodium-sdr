// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/hermes/HermesWorker.h"
#include "dsp/SpscRing.h"
#include "hal/HalLog.h"

#include <QUdpSocket>

namespace dsdr::hal::hermes {

HermesWorker::HermesWorker(dsp::SpscRing<float> *ring, QObject *parent)
    : QObject(parent)
    , m_ring(ring)
{
    // Due fotogrammi da 63 coppie: il massimo che un pacchetto possa portare.
    m_decoded.resize(kSamplesPerFrame * 2 * 2);
}

HermesWorker::~HermesWorker()
{
    stop();
}

void HermesWorker::configure(const QHostAddress &address, double sampleRate, qint64 centerHz)
{
    m_address = address;
    m_sampleRate = sampleRate;
    m_centerHz = centerHz;
}

void HermesWorker::start()
{
    if (m_running)
        return;

    m_socket = std::make_unique<QUdpSocket>();
    // Porta effimera: la radio risponde da dove le si è scritto, e legarsi
    // alla 1024 farebbe litigare due programmi sulla stessa macchina.
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        emit failed(tr("Socket non apribile: %1").arg(m_socket->errorString()));
        m_socket.reset();
        return;
    }
    connect(m_socket.get(), &QUdpSocket::readyRead, this, &HermesWorker::readPending);

    m_txSequence = 0;
    m_lost = 0;
    m_haveSequence = false;
    m_commandTurn = 0;
    m_running = true;

    m_socket->writeDatagram(buildStartStop(true), m_address, kPort);

    // Un primo pacchetto di comando subito: la radio comincia a mandare
    // campioni prima ancora di sapere su che frequenza deve stare, e senza
    // questo il primo decimo di secondo arriverebbe dalla frequenza sbagliata.
    sendCommand();

    qCInfo(dsdrHal) << "hermes: streaming avviato verso" << m_address.toString()
                    << m_sampleRate << "S/s";
}

void HermesWorker::stop()
{
    if (!m_running)
        return;
    m_running = false;

    if (m_socket) {
        // L'arresto va detto: una radio lasciata a trasmettere campioni verso
        // una porta chiusa continua per minuti, e il programma dopo la trova
        // occupata.
        m_socket->writeDatagram(buildStartStop(false), m_address, kPort);
        m_socket->waitForBytesWritten(200);
        m_socket.reset();
    }
}

Command HermesWorker::nextCommand()
{
    const bool ptt = m_ptt.load(std::memory_order_relaxed);

    // I registri si alternano: un pacchetto ne porta uno solo, e la radio
    // ricorda l'ultimo valore di ciascuno. Il registro zero torna più spesso
    // degli altri perché è quello che porta la velocità, e una velocità
    // arrivata in ritardo si sente come un flusso alla frequenza sbagliata.
    switch (m_commandTurn++ % 4) {
    case 0:
        return speedCommand(m_sampleRate, 1, ptt);
    case 1:
        return frequencyCommand(0x02, static_cast<quint32>(m_centerHz), ptt);
    case 2:
        return gainCommand(m_gainDb, ptt);
    default:
        // Il registro della trasmissione si tiene allineato al ricevitore
        // anche in sola ricezione: se un giorno il PTT parte, la radio non
        // deve trovarsi su una frequenza vecchia.
        return frequencyCommand(0x01, static_cast<quint32>(m_centerHz), ptt);
    }
}

void HermesWorker::sendCommand()
{
    if (!m_socket)
        return;

    const Command first = nextCommand();
    const Command second = nextCommand();
    m_socket->writeDatagram(buildCommandPacket(m_txSequence++, first, second),
                            m_address, kPort);
}

void HermesWorker::readPending()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const qint64 size = m_socket->pendingDatagramSize();
        QByteArray datagram(static_cast<int>(size), '\0');
        m_socket->readDatagram(datagram.data(), datagram.size());

        if (!isDataPacket(datagram))
            continue;

        // ── Buchi nella numerazione ─────────────────────────────────────
        const quint32 sequence = packetSequence(datagram);
        if (m_haveSequence && sequence != m_expectedRx) {
            // La differenza è senza segno di proposito: alla ripartenza del
            // contatore un conto con segno darebbe quattro miliardi di
            // pacchetti persi in un colpo solo.
            const quint32 gap = sequence - m_expectedRx;
            m_lost += gap;
            emit packetsLost(m_lost);
        }
        m_expectedRx = sequence + 1;
        m_haveSequence = true;

        const std::size_t frames = decodeIq(datagram, m_decoded.data());
        if (frames == 0)
            continue;

        const std::size_t written = m_ring ? m_ring->write(m_decoded.data(), frames * 2) : 0;
        const std::size_t writtenFrames = written / 2;

        emit framesProduced(static_cast<quint32>(writtenFrames),
                            static_cast<quint32>(frames - writtenFrames),
                            hasAdcOverload(datagram));

        // Un pacchetto ricevuto, un pacchetto mandato. È così che il
        // protocollo tiene il passo senza un timer: il ritmo lo detta la
        // radio, che è l'unica che ha un quarzo in mezzo al flusso.
        sendCommand();
    }
}

void HermesWorker::setCenterFrequency(qint64 hz)
{
    m_centerHz = hz;
}

void HermesWorker::setSampleRate(double rate)
{
    m_sampleRate = rate;
}

void HermesWorker::setGainDb(double db)
{
    m_gainDb = db;
}

void HermesWorker::setPtt(bool transmit)
{
    m_ptt.store(transmit, std::memory_order_relaxed);
}

} // namespace dsdr::hal::hermes
