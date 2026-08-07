// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/BackendRegistry.h"
#include "hal/HalLog.h"
#include "hal/IRadioBackend.h"

#ifdef DSDR_BACKEND_DEMO
#include "hal/backends/demo/DemoBackend.h"
#endif

namespace dsdr::hal {

BackendRegistry &BackendRegistry::instance()
{
    static BackendRegistry registry;
    return registry;
}

void BackendRegistry::registerBackend(const BackendInfo &info, Factory factory)
{
    if (info.id.isEmpty() || !factory) {
        qCWarning(dsdrHal) << "registrazione backend rifiutata: id o factory mancanti";
        return;
    }
    if (m_factories.contains(info.id)) {
        qCWarning(dsdrHal) << "backend già registrato, ignorato:" << info.id;
        return;
    }

    m_factories.insert(info.id, std::move(factory));
    m_info.insert(info.id, info);
    m_order.append(info.id);
    qCInfo(dsdrHal) << "backend registrato:" << info.id;
}

QStringList BackendRegistry::backendIds() const
{
    return m_order;
}

QList<BackendInfo> BackendRegistry::backends() const
{
    QList<BackendInfo> list;
    list.reserve(m_order.size());
    for (const QString &id : m_order)
        list.append(m_info.value(id));
    return list;
}

BackendInfo BackendRegistry::info(const QString &id) const
{
    return m_info.value(id);
}

bool BackendRegistry::contains(const QString &id) const
{
    return m_factories.contains(id);
}

IRadioBackend *BackendRegistry::create(const QString &id, QObject *parent) const
{
    const auto it = m_factories.constFind(id);
    if (it == m_factories.constEnd()) {
        qCWarning(dsdrHal) << "backend non disponibile in questa build:" << id;
        return nullptr;
    }
    return (*it)(parent);
}

void registerBuiltinBackends()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    // I descrittori attraversano connessioni queued fra il thread di ingest e
    // il core: registrarli qui evita di scoprire a runtime che un signal è
    // stato silenziosamente scartato.
    qRegisterMetaType<dsdr::BackendState>("dsdr::BackendState");
    qRegisterMetaType<dsdr::DemodMode>("dsdr::DemodMode");
    qRegisterMetaType<DeviceDescriptor>("dsdr::hal::DeviceDescriptor");
    qRegisterMetaType<BackendCapabilities>("dsdr::hal::BackendCapabilities");
    qRegisterMetaType<BackendError>("dsdr::hal::BackendError");
    qRegisterMetaType<IqFrame>("dsdr::hal::IqFrame");
    qRegisterMetaType<AudioFrame>("dsdr::hal::AudioFrame");
    qRegisterMetaType<SpectrumFrame>("dsdr::hal::SpectrumFrame");
    qRegisterMetaType<MeterFrame>("dsdr::hal::MeterFrame");

#ifdef DSDR_BACKEND_DEMO
    BackendRegistry::instance().registerBackend(
        BackendInfo{QStringLiteral("demo"),
                    QStringLiteral("Demo (segnali sintetici)"),
                    QStringLiteral("Banda simulata con stazioni CW, SSB e AM: "
                                   "l'applicazione è pienamente operativa senza hardware.")},
        [](QObject *parent) -> IRadioBackend * { return new DemoBackend(parent); });
#endif

    // Gli altri backend si aggiungono qui, uno per fase (§9). Ogni blocco è
    // protetto dalla propria opzione di build: il core non cambia mai.
}

} // namespace dsdr::hal
