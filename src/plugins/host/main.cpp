// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — l'ospite dei plugin VST3 (SPEC-005 §4.5).
//
// **Questo eseguibile esiste per poter morire.**
//
// Un plugin VST3 è codice di qualcun altro, e quando sbaglia sbaglia dentro il
// processo che lo ospita. Il programma che si porterebbe dietro non è un
// editor audio: è una radio, e magari sta trasmettendo. Quindi non gira lì
// dentro — gira qui, e se salta salta questo. Dall'altra parte si vede una
// pipe che si chiude, il blocco va in bypass, e la stazione resta in aria.
//
// Di conseguenza questo file è scritto con un criterio diverso dal resto del
// progetto: **niente qui deve poter far morire il chiamante**, e tutto quello
// che va storto deve uscire come una riga di testo su stdout invece che come
// un'eccezione o un codice di ritorno.
//
// Il protocollo sta in `plugins/PluginProtocol.h`, insieme al perché è così
// scarno.
//
// Non c'è la finestra del plugin, ed è una scelta dichiarata: incastrare un
// editor VST3 dentro una scena QML è un problema a sé — è una finestra nativa
// dentro un albero di elementi grafici che non lo è — e senza di essa il
// plugin resta comunque governabile dai suoi parametri, che è come lo governa
// qualunque automazione. Chi vuole la manopola disegnata dal costruttore
// apre il plugin nel suo programma e ne salva il preset.
#include "plugins/PluginProtocol.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <QCoreApplication>
#include <QFile>
#include <QSharedMemory>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cstring>
#include <iostream>
#include <memory>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace dsdr::plugins;

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void say(const QString &line)
{
    out() << line << '\n';
    out().flush();
}

void fail(const QString &why)
{
    say(QStringLiteral("%1 %2").arg(kRepError, why));
}

/// Tutto quello che serve a tenere in piedi un plugin caricato.
///
/// Sta insieme perché si scarica insieme: mezzo plugin scaricato è il modo in
/// cui un ospite si porta via se stesso al secondo caricamento.
struct Loaded
{
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    QString name;
    bool active = false;

    void unload()
    {
        if (component && active) {
            component->setActive(false);
            active = false;
        }
        processor.reset();
        controller.reset();
        component.reset();
        provider.reset();
        module.reset();
        name.clear();
    }
};

Loaded g_loaded;
HostApplication g_host;

/// I parametri cambiati da quando è passato l'ultimo blocco.
///
/// Non si applicano subito, e non è pigrizia. In VST3 il controller e il
/// processore sono due oggetti distinti, e il secondo — quello che elabora —
/// riceve i cambi **dentro** `process()`, in una coda che porta anche il
/// campione a cui vanno applicati. Scriverli solo sul controller li farebbe
/// vedere alla finestra del plugin e non al segnale: la manopola si muove, il
/// suono no, e non c'è niente che lo spieghi a chi lo sta guardando.
ParameterChanges g_pending;

QString fromVst(const Steinberg::Vst::String128 &text)
{
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(text));
}

// ── scan ─────────────────────────────────────────────────────────────────

void doScan()
{
    // I percorsi li conosce l'SDK: sono quelli del sistema operativo, e
    // inventarseli qui vorrebbe dire sbagliarli su una delle tre piattaforme.
    for (const std::string &path : VST3::Hosting::Module::getModulePaths()) {
        std::string error;
        auto module = VST3::Hosting::Module::create(path, error);
        if (!module) {
            // Un plugin che non si apre non ferma la scansione: su una
            // macchina con cento plugin installati ce n'è sempre uno rotto, e
            // fermarsi lì vorrebbe dire non vedere gli altri novantanove.
            continue;
        }

        const auto factory = module->getFactory();
        for (const auto &info : factory.classInfos()) {
            if (info.category() != kVstAudioEffectClass)
                continue;
            say(QStringLiteral("%1 %2|%3|%4|%5")
                    .arg(kRepPlugin)
                    .arg(QString::fromStdString(path))
                    .arg(QString::fromStdString(info.name()))
                    .arg(QString::fromStdString(info.vendor()))
                    .arg(QString::fromStdString(info.subCategoriesString())));
        }
    }
    say(kRepOk);
}

// ── load ─────────────────────────────────────────────────────────────────

void doLoad(const QString &path)
{
    g_loaded.unload();

    std::string error;
    auto module = VST3::Hosting::Module::create(path.toStdString(), error);
    if (!module) {
        fail(QStringLiteral("Il plugin non si apre: %1")
                 .arg(QString::fromStdString(error)));
        return;
    }

    const auto factory = module->getFactory();
    for (const auto &info : factory.classInfos()) {
        if (info.category() != kVstAudioEffectClass)
            continue;

        auto provider = owned(new PlugProvider(factory, info, true));
        if (!provider->initialize())
            continue;

        auto component = provider->getComponentPtr();
        auto processor = FUnknownPtr<IAudioProcessor>(component);
        if (!component || !processor)
            continue;

        g_loaded.module = module;
        g_loaded.provider = provider;
        g_loaded.component = component;
        g_loaded.processor = processor;
        g_loaded.controller = provider->getControllerPtr();
        g_loaded.name = QString::fromStdString(info.name());
        break;
    }

    if (!g_loaded.processor) {
        fail(QStringLiteral("Nel file non c'è nessun effetto audio utilizzabile."));
        g_loaded.unload();
        return;
    }

    // I parametri, che sono l'unico modo di governare il plugin senza la sua
    // finestra. Si prendono i primi: un plugin serio ne dichiara anche
    // duecento, e mostrarli tutti vuol dire non mostrarne nessuno.
    if (g_loaded.controller) {
        const int32 count = std::min(g_loaded.controller->getParameterCount(),
                                     static_cast<int32>(kMaxParameters));
        for (int32 i = 0; i < count; ++i) {
            ParameterInfo info{};
            if (g_loaded.controller->getParameterInfo(i, info) != kResultOk)
                continue;
            say(QStringLiteral("%1 %2|%3|%4|%5")
                    .arg(kRepParameter)
                    .arg(info.id)
                    .arg(fromVst(info.title))
                    .arg(fromVst(info.units))
                    .arg(g_loaded.controller->getParamNormalized(info.id)));
        }
    }

    say(QStringLiteral("%1 %2").arg(kRepOk, g_loaded.name));
}

// ── prepare ──────────────────────────────────────────────────────────────

void doPrepare(double sampleRate, int frames)
{
    if (!g_loaded.processor) {
        fail(QStringLiteral("Non c'è nessun plugin da preparare."));
        return;
    }

    if (g_loaded.active) {
        g_loaded.component->setActive(false);
        g_loaded.active = false;
    }

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = frames;
    setup.sampleRate = sampleRate;

    if (g_loaded.processor->setupProcessing(setup) != kResultOk) {
        fail(QStringLiteral("Il plugin rifiuta %1 Hz a blocchi di %2 campioni.")
                 .arg(sampleRate, 0, 'f', 0).arg(frames));
        return;
    }

    // Stereo in ingresso e in uscita. Un plugin che non ne ha si arrangia da
    // sé: `setBusArrangements` che dice di no non è fatale, perché molti
    // rispondono `kResultFalse` e poi elaborano lo stesso.
    SpeakerArrangement arrangement = SpeakerArr::kStereo;
    g_loaded.processor->setBusArrangements(&arrangement, 1, &arrangement, 1);

    g_loaded.component->activateBus(kAudio, kInput, 0, true);
    g_loaded.component->activateBus(kAudio, kOutput, 0, true);
    g_loaded.component->setActive(true);
    g_loaded.processor->setProcessing(true);
    g_loaded.active = true;

    say(kRepOk);
}

// ── process ──────────────────────────────────────────────────────────────

QSharedMemory *g_shared = nullptr;

void doProcess()
{
    if (!g_shared || !g_shared->data()) {
        fail(QStringLiteral("Memoria condivisa non agganciata."));
        return;
    }

    auto *base = static_cast<char *>(g_shared->data());
    SharedHeader header{};
    std::memcpy(&header, base, sizeof(header));

    if (header.frames <= 0 || header.frames > kMaxBlockFrames) {
        fail(QStringLiteral("Blocco di misura impossibile: %1").arg(header.frames));
        return;
    }

    if (!g_loaded.processor || !g_loaded.active) {
        // Niente plugin: il segnale resta com'è, e si risponde comunque.
        // Tacere qui bloccherebbe il chiamante fino allo scadere del suo
        // tempo, a ogni blocco.
        say(QStringLiteral("%1 %2").arg(kRepDone).arg(header.frames));
        return;
    }

    auto *left = reinterpret_cast<float *>(base + sizeof(SharedHeader));
    auto *right = left + kMaxBlockFrames;
    float *channels[kChannels] = {left, right};

    AudioBusBuffers input{};
    input.numChannels = kChannels;
    input.channelBuffers32 = channels;

    AudioBusBuffers output{};
    output.numChannels = kChannels;
    // Ingresso e uscita sullo stesso buffer: VST3 lo consente, e una copia in
    // più a ogni blocco su un percorso che attraversa già due processi sarebbe
    // proprio quella di troppo.
    output.channelBuffers32 = channels;

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = header.frames;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &input;
    data.outputs = &output;

    ProcessContext context{};
    context.sampleRate = header.sampleRate;
    context.state = ProcessContext::kPlaying;
    data.processContext = &context;

    // I cambi di parametro accumulati dall'ultimo blocco entrano qui: è
    // l'unica strada che arriva al processore.
    ParameterChanges outgoing;
    data.inputParameterChanges = &g_pending;
    data.outputParameterChanges = &outgoing;

    g_loaded.processor->process(data);
    g_pending.clearQueue();
    say(QStringLiteral("%1 %2").arg(kRepDone).arg(header.frames));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        fail(QStringLiteral("Manca la chiave della memoria condivisa."));
        return 2;
    }

    QSharedMemory shared(args.at(1));
    if (!shared.attach()) {
        fail(QStringLiteral("Memoria condivisa «%1» non trovata: %2")
                 .arg(args.at(1), shared.errorString()));
        return 2;
    }
    g_shared = &shared;

    PluginContextFactory::instance().setPluginContext(&g_host);

    say(QStringLiteral("%1 %2").arg(kRepReady).arg(kProtocolVersion));

    // Un ciclo su stdin e niente altro. Le righe arrivano da un processo che
    // controlliamo noi, quindi non c'è da difendersi da un formato ostile —
    // c'è da non far cadere il programma su una riga inattesa, che è un'altra
    // cosa e si ottiene rispondendo `err` invece di tacere.
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly)) {
        fail(QStringLiteral("Non si legge dallo standard input."));
        return 2;
    }

    while (!in.atEnd()) {
        const QString line = QString::fromUtf8(in.readLine()).trimmed();
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split(QLatin1Char(' '));
        const QString verb = parts.constFirst();

        if (verb == kCmdQuit) {
            break;
        } else if (verb == kCmdProcess) {
            doProcess();
        } else if (verb == kCmdScan) {
            doScan();
        } else if (verb == kCmdLoad) {
            doLoad(line.mid(kCmdLoad.size() + 1));
        } else if (verb == kCmdUnload) {
            g_loaded.unload();
            say(kRepOk);
        } else if (verb == kCmdPrepare) {
            doPrepare(parts.value(1).toDouble(), parts.value(2).toInt());
        } else if (verb == kCmdParam) {
            const auto id = static_cast<ParamID>(parts.value(1).toUInt());
            const double value = parts.value(2).toDouble();

            // Al controller, perché sappia com'è messo…
            if (g_loaded.controller)
                g_loaded.controller->setParamNormalized(id, value);

            // …e in coda per il processore, che è quello che tocca il segnale.
            int32 queueIndex = 0;
            if (IParamValueQueue *queue = g_pending.addParameterData(id, queueIndex)) {
                int32 pointIndex = 0;
                queue->addPoint(0, value, pointIndex);
            }
            say(kRepOk);
        } else {
            fail(QStringLiteral("Comando sconosciuto: %1").arg(verb));
        }
    }

    g_loaded.unload();
    return 0;
}
