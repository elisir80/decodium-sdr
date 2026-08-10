// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsp/neural/DfnEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStandardPaths>

#include <algorithm>

namespace dsdr::dsp::neural {

/// I simboli della C-API, risolti a runtime. Le firme sono quelle generate da
/// cbindgen sul crate `deep_filter`: `path` per primo, poi l'attenuazione.
struct DfnEngine::Api
{
    using CreateFn = void *(*)(const char *path, float attenLim, const char *logLevel);
    using FrameLengthFn = std::size_t (*)(void *state);
    using ProcessFn = float (*)(void *state, float *input, float *output);
    using SetAttenFn = void (*)(void *state, float limDb);
    using FreeFn = void (*)(void *state);

    CreateFn create = nullptr;
    FrameLengthFn frameLength = nullptr;
    ProcessFn process = nullptr;
    SetAttenFn setAtten = nullptr;
    FreeFn free = nullptr;

    bool complete() const
    {
        // `setAtten` e `free` sono le due che si possono perdere senza
        // accorgersene: la prima renderebbe muto il cursore, la seconda
        // trasformerebbe ogni cambio di modello in memoria persa.
        return create && frameLength && process && setAtten && free;
    }
};

namespace {

/// I nomi che la libreria può avere. `cargo-c` li produce diversi per
/// piattaforma, e cercarne uno solo vorrebbe dire funzionare su una macchina
/// e non sull'altra senza una ragione visibile.
const QStringList &libraryNames()
{
    static const QStringList names = {
        QStringLiteral("deep_filter"),
        QStringLiteral("libdeep_filter"),
        QStringLiteral("deepfilter"),
    };
    return names;
}

} // namespace

DfnEngine::DfnEngine()
    : m_api(std::make_unique<Api>())
{
}

DfnEngine::~DfnEngine()
{
    if (m_state && m_api && m_api->free)
        m_api->free(m_state);
    m_state = nullptr;
}

QStringList DfnEngine::searchPaths()
{
    QStringList paths;
    // Accanto all'eseguibile per prima: è dove finisce quello che il
    // pacchetto si porta dietro.
    paths << QCoreApplication::applicationDirPath();
    // Poi dove uno la mette compilandola a mano.
    paths << QDir::current().absoluteFilePath(QStringLiteral("third_party/deepfilter"));
    paths << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/lib");
    // E infine i percorsi di sistema, che QLibrary consulta da sé con il nome
    // nudo: la stringa vuota lascia decidere a lei.
    paths << QString();
    return paths;
}

bool DfnEngine::prepare(const QString &modelPath, QString *error)
{
    if (m_state && m_api->free) {
        m_api->free(m_state);
        m_state = nullptr;
    }

    // ── La libreria ─────────────────────────────────────────────────────
    m_library.reset();
    for (const QString &directory : searchPaths()) {
        for (const QString &name : libraryNames()) {
            const QString candidate = directory.isEmpty()
                ? name
                : QDir(directory).absoluteFilePath(name);
            auto library = std::make_unique<QLibrary>(candidate);
            if (library->load()) {
                m_library = std::move(library);
                break;
            }
        }
        if (m_library)
            break;
    }

    if (!m_library) {
        if (error) {
            *error = QStringLiteral("Libreria DeepFilterNet non trovata. Cercata in: %1")
                         .arg(searchPaths().join(QStringLiteral(", ")));
        }
        return false;
    }

    m_api->create = reinterpret_cast<Api::CreateFn>(m_library->resolve("df_create"));
    m_api->frameLength =
        reinterpret_cast<Api::FrameLengthFn>(m_library->resolve("df_get_frame_length"));
    m_api->process = reinterpret_cast<Api::ProcessFn>(m_library->resolve("df_process_frame"));
    m_api->setAtten = reinterpret_cast<Api::SetAttenFn>(m_library->resolve("df_set_atten_lim"));
    m_api->free = reinterpret_cast<Api::FreeFn>(m_library->resolve("df_free"));

    if (!m_api->complete()) {
        if (error) {
            *error = QStringLiteral("La libreria trovata non espone la C-API di "
                                    "DeepFilterNet: forse è il plugin LADSPA, che "
                                    "è un'altra cosa.");
        }
        m_library.reset();
        return false;
    }

    // ── Il modello ──────────────────────────────────────────────────────
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        if (error) {
            *error = QStringLiteral("Modello DeepFilterNet non trovato: %1")
                         .arg(modelPath.isEmpty() ? QStringLiteral("(nessuno indicato)")
                                                  : modelPath);
        }
        m_library.reset();
        return false;
    }

    const QByteArray path = modelPath.toUtf8();
    m_state = m_api->create(path.constData(), m_attenuationDb, nullptr);
    if (!m_state) {
        if (error)
            *error = QStringLiteral("DeepFilterNet non ha accettato il modello.");
        m_library.reset();
        return false;
    }

    m_frameSamples = static_cast<int>(m_api->frameLength(m_state));
    if (m_frameSamples <= 0) {
        if (error)
            *error = QStringLiteral("DeepFilterNet dichiara un fotogramma di lunghezza nulla.");
        m_api->free(m_state);
        m_state = nullptr;
        m_library.reset();
        return false;
    }

    // L'unica allocazione, e sta qui: da `processFrame` in poi si lavora su
    // questo buffer. La C-API vuole ingresso e uscita separati.
    m_output.assign(static_cast<std::size_t>(m_frameSamples), 0.0f);
    m_modelPath = modelPath;
    m_modelName = QFileInfo(modelPath).completeBaseName();
    return true;
}

void DfnEngine::processFrame(float *samples)
{
    if (!m_state || !samples)
        return;

    const float snr = m_api->process(m_state, samples, m_output.data());
    m_lastSnrDb.store(snr, std::memory_order_relaxed);

    std::copy(m_output.begin(), m_output.end(), samples);
}

void DfnEngine::setAttenuationLimitDb(float db)
{
    m_attenuationDb = std::clamp(db, 0.0f, 100.0f);
    // A caldo, senza ricreare il motore: era la questione aperta №1 della
    // specifica, e la C-API la risolve.
    if (m_state && m_api->setAtten)
        m_api->setAtten(m_state, m_attenuationDb);
}

NrEngineInfo DfnEngine::info() const
{
    NrEngineInfo engineInfo;
    engineInfo.id = QStringLiteral("dfn3");
    engineInfo.modelName = m_modelName.isEmpty()
        ? QStringLiteral("DeepFilterNet3")
        : QStringLiteral("DeepFilterNet3 (%1)").arg(m_modelName);
    engineInfo.frameSamples = m_frameSamples;
    // Il ritardo algoritmico è il fotogramma: la rete lavora a salti di hop e
    // consegna quello che ha appena letto.
    engineInfo.latencySamples = m_frameSamples;
    return engineInfo;
}

void DfnEngine::reset()
{
    if (!m_state || !m_api->create || !m_api->free)
        return;

    // La C-API non espone un azzeramento degli stati ricorrenti: si ricrea lo
    // stato dal modello. Costa, e per questo succede solo fuori dal percorso
    // caldo — a un cambio di canale o di modo.
    m_api->free(m_state);
    m_state = nullptr;

    const QByteArray path = m_modelPath.toUtf8();
    m_state = m_api->create(path.constData(), m_attenuationDb, nullptr);
}

} // namespace dsdr::dsp::neural
