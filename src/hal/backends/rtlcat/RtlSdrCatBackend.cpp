// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlcat/RtlSdrCatBackend.h"

#include "hal/HalLog.h"
#include "hal/backends/audiorig/CatController.h"
#include "hal/backends/audiorig/CatDriverFactory.h"
#include "hal/backends/audiorig/LocalRigctldDriver.h"
#include "hal/backends/rtlcat/RtlSdrTxSafety.h"

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>

namespace dsdr::hal::rtlcat {

namespace {

// Ventimillisecondi più il singolo round-trip CAT: abbastanza rapido da
// togliere l'IF prima che una trasmissione possa proseguire, ma senza
// tempestare una seriale lenta con letture complete di VFO, modo e S-meter.
constexpr int kProtectionPttPollMs = 20;

} // namespace

RtlSdrCatBackend::RtlSdrCatBackend(QObject *parent)
    : RtlSdrBackend(parent)
{
}

RtlSdrCatBackend::~RtlSdrCatBackend()
{
    close();
}

QString RtlSdrCatBackend::displayName() const
{
    const QString source = RtlSdrBackend::displayName();
    return m_radioModel.isEmpty()
        ? QStringLiteral("%1 + CAT").arg(source)
        : QStringLiteral("%1 + %2").arg(source, m_radioModel);
}

BackendCapabilities RtlSdrCatBackend::capabilities() const
{
    BackendCapabilities caps = RtlSdrBackend::capabilities();
    // Una sola radio ha un solo VFO che determina l'IF. Esporre quattro VFO
    // virtuali qui li farebbe sembrare quattro ricevitori indipendenti, ma
    // sul ferro si muoverebbero tutti con la stessa manopola.
    caps.maxRxChannels = 1;
    caps.coherentRx = false;
    caps.manualDeviceEntry = true;
    caps.vfoFollowsRadio = true;
    if (!caps.nativePanels.contains(QStringLiteral("RtlSdrDevicePanel")))
        caps.nativePanels.append(QStringLiteral("RtlSdrDevicePanel"));
    caps.nativePanels.append(QStringLiteral("RtlCatPanel"));
    return caps;
}

void RtlSdrCatBackend::open(const DeviceDescriptor &device)
{
    if (m_catProfile.isEmpty()) {
        reportError(BackendError::NotFound,
                    tr("Configura e salva prima il profilo CAT/Hamlib della radio."), true);
        return;
    }

    // Fail closed: finche' il CAT non ha letto esplicitamente RX, l'IQ non
    // viene consegnato. Se la porta CAT cade durante un QSO vale la stessa
    // regola: un panadapter muto e' preferibile a un RTL-SDR esposto.
    m_catStateKnown = false;
    m_radioTransmitting = false;
    m_pttUnavailableReported = false;
    setIqInputBlocked(shouldBlockRtlInput(m_catStateKnown, m_radioTransmitting));
    RtlSdrBackend::open(device);
    if (!isOpen())
        return;
    startCat();
}

void RtlSdrCatBackend::close()
{
    setIqInputBlocked(true);
    stopCat();
    RtlSdrBackend::close();
}

void RtlSdrCatBackend::setCenterFrequency(qint64 hz)
{
    RtlSdrBackend::setCenterFrequency(hz);
    if (centerFrequency() == hz)
        requestRadioFrequency(hz);
}

ChannelId RtlSdrCatBackend::createRxChannel(const RxChannelConfig &config)
{
    RxChannelConfig synchronised = config;
    if (m_catStateKnown)
        synchronised.mode = m_radioMode;
    return RtlSdrBackend::createRxChannel(synchronised);
}

void RtlSdrCatBackend::setFrequency(ChannelId channel, qint64 hz)
{
    if (!channels().contains(channel)) {
        RtlSdrBackend::setFrequency(channel, hz);
        return;
    }
    RtlSdrBackend::setFrequency(channel, hz);
    requestRadioFrequency(hz);
}

void RtlSdrCatBackend::setDemod(ChannelId channel, DemodMode mode)
{
    RtlSdrBackend::setDemod(channel, mode);
    requestRadioMode(mode);
}

void RtlSdrCatBackend::startCat()
{
    stopCat();
    const QString driverId = m_catProfile.value(QStringLiteral("driver")).toString();
    auto *controller = new audiorig::CatController(audiorig::makeCatDriver(driverId));
    // Il thread non e' figlio del backend: durante un cambio sorgente il CAT
    // puo' essere nel mezzo di una richiesta seriale. Deve potersi ritirare
    // da solo senza costringere il thread grafico ad aspettare quel timeout.
    m_catThread = new QThread;
    m_catThread->setObjectName(QStringLiteral("dsdr-rtlcat-control"));
    controller->moveToThread(m_catThread);
    connect(m_catThread, &QThread::finished, controller, &QObject::deleteLater);
    connect(m_catThread, &QThread::finished, m_catThread, &QObject::deleteLater);
    connect(controller, &audiorig::CatController::stateRead,
            this, &RtlSdrCatBackend::onCatState);
    connect(controller, &audiorig::CatController::lost,
            this, &RtlSdrCatBackend::onCatLost);
    connect(controller, &audiorig::CatController::opened, this,
            [this](const QString &model, const QString &port, int baud) {
                m_radioModel = model;
                qCInfo(dsdrHal) << "rtlcat: CAT collegato" << model << "su" << port
                                << "baud" << baud;
                emit capabilitiesChanged();
            });
    m_cat = controller;
    m_catThread->start();

    QMetaObject::invokeMethod(controller, "open", Qt::QueuedConnection,
                              Q_ARG(QString, m_catProfile.value(QStringLiteral("port")).toString()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("baud"), 0).toInt()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("dataBits"), 8).toInt()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("parity"), 0).toInt()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("stopBits"), 1).toInt()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("flowControl"), -1).toInt()),
                              Q_ARG(bool, m_catProfile.value(QStringLiteral("dtr"), false).toBool()),
                              Q_ARG(bool, m_catProfile.value(QStringLiteral("rts"), false).toBool()),
                              Q_ARG(int, m_catProfile.value(QStringLiteral("hamlibModel"), 0).toInt()));
    // Stato radio completo a 5 Hz; PTT in un percorso dedicato a 20 ms. Così
    // una lenta lettura di VFO/S-meter non si interpone fra TX e il blocco
    // fisico dello stream USB dell'RTL-SDR.
    QMetaObject::invokeMethod(controller, "setPttPollInterval", Qt::QueuedConnection,
                              Q_ARG(int, kProtectionPttPollMs));
}

void RtlSdrCatBackend::stopCat()
{
    // Non usare BlockingQueuedConnection qui. `rigctld` puo' stare
    // negoziando una radio che non risponde e, se il thread grafico aspetta
    // quella negoziazione, macOS dichiara la app non responsiva. Si richiede
    // invece l'interruzione, si scollegano gli aggiornamenti tardivi e il
    // thread si autodistrugge appena esce dalla chiamata CAT in corso.
    QPointer<audiorig::CatController> controller = m_cat;
    QPointer<QThread> thread = m_catThread;
    m_cat = nullptr;
    m_catThread = nullptr;

    if (!thread)
        return;
    if (controller) {
        disconnect(controller, nullptr, this, nullptr);
        QMetaObject::invokeMethod(controller, "close", Qt::QueuedConnection);
    }
    thread->requestInterruption();
    thread->quit();
}

void RtlSdrCatBackend::requestRadioFrequency(qint64 hz)
{
    if (!m_cat || hz <= 0 || m_lastRequestedFrequency == hz)
        return;
    m_lastRequestedFrequency = hz;
    QMetaObject::invokeMethod(m_cat, "setFrequency", Qt::QueuedConnection, Q_ARG(qint64, hz));
    qCDebug(dsdrHal) << "rtlcat: VFO -> radio" << hz;
}

void RtlSdrCatBackend::requestRadioMode(DemodMode mode)
{
    if (!m_cat)
        return;
    QMetaObject::invokeMethod(m_cat, "setMode", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(mode)));
}

void RtlSdrCatBackend::onCatState(qint64 frequencyHz, int mode, bool transmitting,
                                  bool pttKnown, int sMeterRaw, double signalDbm)
{
    Q_UNUSED(sMeterRaw)
    Q_UNUSED(signalDbm)

    const bool pttCapabilityChanged = pttKnown != m_catStateKnown;
    m_catStateKnown = pttKnown;
    const bool txChanged = pttKnown && transmitting != m_radioTransmitting;
    m_radioTransmitting = pttKnown ? transmitting : false;
    const bool blockInput = shouldBlockRtlInput(m_catStateKnown, m_radioTransmitting);
    if (blockInput != iqInputBlocked()) {
        setIqInputBlocked(blockInput);
        if (transmitting)
            qCWarning(dsdrHal) << "rtlcat: radio in TX: IQ bloccato";
        else
            qCInfo(dsdrHal) << "rtlcat: radio in RX: IQ riattivato";
    }
    if (!pttKnown && !m_pttUnavailableReported) {
        qCWarning(dsdrHal) << "rtlcat: CAT collegato ma PTT non disponibile; "
                               "IQ bloccato, serve il mute IF o un relè RX/TX esterno";
        m_pttUnavailableReported = true;
    }
    if (pttCapabilityChanged && pttKnown)
        m_pttUnavailableReported = false;
    if (txChanged)
        emit pttChanged(transmitting);

    // Anche una risposta CAT parziale puo' contenere il PTT. La protezione
    // vale subito; frequenza e modo aspettano invece un valore completo.
    if (frequencyHz <= 0)
        return;

    const DemodMode radioMode = static_cast<DemodMode>(mode);

    // Aggiorniamo prima la HAL IQ e poi comunichiamo lo stato al core. Il
    // core modifica UI e DSP ma non rinvia i comandi, percio' non si forma un
    // ciclo fra risposta CAT, VFO sullo schermo e nuova scrittura CAT.
    if (frequencyHz != centerFrequency()) {
        for (ChannelId channel : channels())
            RtlSdrBackend::setFrequency(channel, frequencyHz);
        RtlSdrBackend::setCenterFrequency(frequencyHz);
    }
    m_lastRequestedFrequency = frequencyHz;
    m_radioMode = radioMode;
    emit receiverStateChanged(frequencyHz, radioMode);
}

void RtlSdrCatBackend::onCatLost(const QString &reason)
{
    m_catStateKnown = false;
    setIqInputBlocked(true);
    reportError(BackendError::TransportError,
                tr("CAT perso: ingresso RTL-SDR bloccato per sicurezza — %1").arg(reason), true);
}

QVariantList RtlSdrCatBackend::serialPorts() const
{
    QVariantList ports;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("port"), info.portName());
        entry.insert(QStringLiteral("description"), info.description());
        entry.insert(QStringLiteral("manufacturer"), info.manufacturer());
        entry.insert(QStringLiteral("serial"), info.serialNumber());
        QSerialPort probe(info);
        const bool free = probe.open(QIODevice::ReadWrite);
        if (free)
            probe.close();
        entry.insert(QStringLiteral("busy"), !free);
        ports.append(entry);
    }
    return ports;
}

QVariantList RtlSdrCatBackend::catDrivers()
{
    return QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("hamlib-local")},
                    {QStringLiteral("label"), QObject::tr("Hamlib · radio locale via USB/seriale")},
                    {QStringLiteral("requiresModel"), true}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("rigctld")},
                    {QStringLiteral("label"), QObject::tr("Hamlib · rigctld in rete")}},
    };
}

QVariant RtlSdrCatBackend::nativeCommand(const QString &command, const QVariantMap &args)
{
    if (command == QLatin1String("device.serialPorts"))
        return serialPorts();
    if (command == QLatin1String("device.catDrivers"))
        return catDrivers();
    if (command == QLatin1String("device.hamlibModels")) {
        QString error;
        const QVariantList models = audiorig::LocalRigctldDriver::availableModels(&error);
        if (models.isEmpty() && !error.isEmpty())
            qCWarning(dsdrHal) << "rtlcat:" << error;
        return models;
    }
    if (command == QLatin1String("device.catDefaults")) {
        const QString driver = args.value(QStringLiteral("driver")).toString();
        if (driver != QLatin1String("hamlib-local"))
            return {};
        const int model = args.value(QStringLiteral("hamlibModel"), 2011).toInt();
        return audiorig::LocalRigctldDriver::serialDefaultsForModel(model);
    }
    if (command == QLatin1String("device.manualEntryAction"))
        return tr("SALVA PROFILO CAT");
    if (command == QLatin1String("device.manualEntryHint"))
        return tr("Salva il CAT/Hamlib, poi scegli l'RTL-SDR nell'elenco e connettilo. Il VFO della radio restera' il riferimento dello spettro.");
    if (command == QLatin1String("device.declare")) {
        const QString driver = args.value(QStringLiteral("driver")).toString();
        const QString port = args.value(QStringLiteral("port")).toString().trimmed();
        if (driver.isEmpty() || port.isEmpty())
            return false;
        if (driver == QLatin1String("hamlib-local")
            && args.value(QStringLiteral("hamlibModel"), 0).toInt() <= 0)
            return false;
        m_catProfile = args;
        qCInfo(dsdrHal) << "rtlcat: profilo CAT salvato" << driver << port
                        << "model" << m_catProfile.value(QStringLiteral("hamlibModel"))
                        << "baud" << m_catProfile.value(QStringLiteral("baud"))
                        << "format" << m_catProfile.value(QStringLiteral("dataBits"))
                        << "parity" << m_catProfile.value(QStringLiteral("parity"))
                        << "stop" << m_catProfile.value(QStringLiteral("stopBits"))
                        << "flow" << m_catProfile.value(QStringLiteral("flowControl"));
        return true;
    }
    if (command == QLatin1String("rtlcat.status")) {
        return QVariantMap{{QStringLiteral("radio"), m_radioModel},
                           {QStringLiteral("catKnown"), m_catStateKnown},
                           {QStringLiteral("pttKnown"), m_catStateKnown},
                           {QStringLiteral("transmitting"), m_radioTransmitting},
                           {QStringLiteral("inputBlocked"), iqInputBlocked()},
                           {QStringLiteral("driver"), m_catProfile.value(QStringLiteral("driver"))},
                           {QStringLiteral("port"), m_catProfile.value(QStringLiteral("port"))},
                           {QStringLiteral("hamlibModel"), m_catProfile.value(QStringLiteral("hamlibModel"))}};
    }
    return RtlSdrBackend::nativeCommand(command, args);
}

} // namespace dsdr::hal::rtlcat
