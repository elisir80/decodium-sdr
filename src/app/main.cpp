// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — punto di ingresso.

#include "app/SessionSingleton.h"
#include "core/SessionManager.h"

#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSet>
#include <QSettings>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cmath>

#if defined(Q_OS_WIN)
#  include <cstdio>
#  include <windows.h>
#endif

namespace {

QtMessageHandler g_previousMessageHandler = nullptr;

bool isKnownDarwinSocketNotifierWarning(QtMsgType type, const QString &message)
{
#if defined(Q_OS_MACOS)
    // Qt crea internamente un notifier di eccezione per le porte seriali
    // Darwin, ma quel tipo non esiste sulla piattaforma. La porta continua a
    // funzionare (CAT usa soltanto lettura e scrittura); il warning e' quindi
    // rumore ripetuto, non una condizione d'errore dell'applicazione.
    return type == QtWarningMsg
        && message == QLatin1String("QSocketNotifier::Exception is not supported on iOS")
        && !qEnvironmentVariableIsSet("DSDR_SHOW_QT_DARWIN_SERIAL_WARNING");
#else
    Q_UNUSED(type)
    Q_UNUSED(message)
    return false;
#endif
}

void applicationMessageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message)
{
    if (isKnownDarwinSocketNotifierWarning(type, message))
        return;

    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, context, message);
        return;
    }

    const QByteArray formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fputs(formatted.constData(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

/// Riaggancia i flussi standard alla console che ci ha lanciati, se c'è.
///
/// Su Windows un programma è di sottosistema «console» o «grafico», e la
/// scelta si fa a compilazione. Da console apre una finestra nera accanto
/// all'applicazione — che non serve a nessuno e sembra un errore; da grafico
/// non ne apre nessuna, ma perde anche stdout e stderr, e con loro tutto il
/// log. Perdere il log non è accettabile: è la prima cosa che si chiede a chi
/// segnala un problema.
///
/// La via d'uscita: eseguibile grafico, e all'avvio ci si aggancia alla
/// console del processo che ci ha lanciati, se ne ha una. Chi fa doppio clic
/// non vede finestre; chi lancia da un terminale — o redirige su un file —
/// ritrova tutto quello che c'era prima.
void attachToParentConsole()
{
#if defined(Q_OS_WIN)
    // Una redirezione esplicita (`> log.txt`) ha già dato ai flussi standard
    // una destinazione valida: riaprirli su CONOUT$ la butterebbe via, ed è
    // proprio il modo in cui si raccoglie un log.
    const bool stdoutRedirected =
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN;

    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // nessuna console da cui siamo stati lanciati: doppio clic

    if (!stdoutRedirected) {
        FILE *stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }

    // Su Windows Qt manda i messaggi al debugger, non a stderr, quando non è
    // sicuro che qualcuno stia leggendo. È la scelta giusta per un programma
    // lanciato con il doppio clic e quella sbagliata qui: siamo stati lanciati
    // da un terminale, e chi l'ha fatto il log lo vuole vedere. Serviva
    // ricordarsi di mettere la variabile a mano — e chi segnala un problema
    // non la conosce.
    if (qEnvironmentVariableIsEmpty("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
#endif
}

void configureBundledSoapyModules()
{
    // I moduli Soapy non sono librerie linkate: il loader conosce soltanto il
    // prefisso del sistema che ha compilato SoapySDR. Un pacchetto portabile
    // deve invece usare il proprio albero, prima che SessionManager inizi
    // l'enumerazione. `SOAPY_SDR_ROOT` sostituisce il prefisso con cui
    // libSoapySDR è stato compilato: soltanto anteporre `PLUGIN_PATH` lascerebbe
    // attivo anche quel prefisso (per esempio Homebrew) e caricherebbe due
    // copie dello stesso driver nello stesso processo.
    const QDir executableDir(QCoreApplication::applicationDirPath());
    const QString bundledRoot =
        QDir(executableDir.absoluteFilePath(QStringLiteral(".."))).absolutePath();
    const QStringList bundledPaths{QDir(bundledRoot).absoluteFilePath(
        QStringLiteral("lib/SoapySDR/modules0.8"))};

    QStringList availablePaths;
    for (const QString &path : bundledPaths) {
        if (QFileInfo(path).isDir())
            availablePaths.append(QDir::cleanPath(path));
    }
    availablePaths.removeDuplicates();
    if (availablePaths.isEmpty())
        return;

    qputenv("SOAPY_SDR_ROOT", QDir::cleanPath(bundledRoot).toUtf8());

    // I moduli esterni restano consentiti, ma una directory che contiene lo
    // stesso file di un modulo incluso è esclusa. Il relativo `dlopen()` può
    // caricare due librerie vendor diverse e basta un driver duplicato per
    // rendere instabile discovery e chiusura dell'app.
    QSet<QString> bundledModuleNames;
    for (const QString &path : availablePaths) {
        const QDir moduleDir(path);
        for (const QString &name : moduleDir.entryList(QDir::Files))
            bundledModuleNames.insert(name);
    }

    const QByteArray inheritedPath = qgetenv("SOAPY_SDR_PLUGIN_PATH");
    QStringList externalPaths;
    for (const QString &path : QString::fromUtf8(inheritedPath).split(
             QDir::listSeparator(), Qt::SkipEmptyParts)) {
        const QFileInfo external(path);
        bool duplicatesBundledModule = bundledModuleNames.contains(external.fileName());
        if (external.isDir()) {
            const QDir externalDir(external.absoluteFilePath());
            for (const QString &name : bundledModuleNames) {
                if (externalDir.exists(name)) {
                    duplicatesBundledModule = true;
                    break;
                }
            }
        }
        if (duplicatesBundledModule) {
            qInfo().noquote() << QStringLiteral(
                "SoapySDR: percorso esterno ignorato perché duplica un modulo incluso: %1")
                                     .arg(path);
            continue;
        }
        externalPaths.append(path);
    }

    if (externalPaths.isEmpty())
        qunsetenv("SOAPY_SDR_PLUGIN_PATH");
    else
        qputenv("SOAPY_SDR_PLUGIN_PATH", externalPaths.join(QDir::listSeparator()).toUtf8());

    qInfo().noquote() << QStringLiteral("SoapySDR: root incluso %1; moduli %2")
                             .arg(QDir::cleanPath(bundledRoot),
                                  availablePaths.join(QDir::listSeparator()));
}

void repairInvalidSMeterCalibration()
{
    // La prima versione della tara automatica poteva campionare una
    // broadcast Wide-FM come se fosse fondo rumore e salvare S9 sopra 0 dBFS.
    // Il pannello può restare chiuso, perciò la riparazione deve avvenire qui
    // e non dipendere dalla sua istanziazione QML.
    constexpr double kDefaultS9ReferenceDb = -55.0;
    QSettings settings;
    bool validNumber = false;
    const double reference = settings.value(
        QStringLiteral("panels/strumento/s9ReferenceDb"), kDefaultS9ReferenceDb)
        .toDouble(&validNumber);
    if (validNumber && std::isfinite(reference) && reference >= -140.0 && reference <= 0.0)
        return;

    settings.setValue(QStringLiteral("panels/strumento/s9ReferenceDb"),
                      kDefaultS9ReferenceDb);
    settings.setValue(QStringLiteral("panels/strumento/calibrated"), false);
    settings.sync();
    qInfo() << "S-meter: riparata la taratura S9 non valida" << reference << "dBFS";
}

} // namespace

int main(int argc, char *argv[])
{
    attachToParentConsole();
    g_previousMessageHandler = qInstallMessageHandler(applicationMessageHandler);

    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName(QStringLiteral("DECODIUM SDR"));
    // Numero e nome insieme: `--version` deve dire tutte e due le cose,
    // perché è quello che si copia dentro una segnalazione di guasto e chi la
    // legge riconosce il nome molto prima del numero.
    QGuiApplication::setApplicationVersion(
        QStringLiteral(DSDR_VERSION) + QStringLiteral(" «")
        + QStringLiteral(DSDR_VERSION_NAME) + QStringLiteral("»"));
    QGuiApplication::setOrganizationName(QStringLiteral("DECODIUM"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("decodium.it"));
    repairInvalidSMeterCalibration();

    // La finestra e la barra delle applicazioni. Su Windows e macOS l'icona
    // del *file* arriva invece dalla risorsa dell'eseguibile e dal bundle: le
    // due strade sono indipendenti, e servono entrambe.
    QGuiApplication::setWindowIcon(
        QIcon(QStringLiteral(":/icons/it.decodium.sdr.png")));

    // Lo stile Basic è l'unico che non impone una propria palette: il tema
    // DECODIUM viene interamente dal singleton Theme (CONSTITUTION §6).
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Client SDR universale dell'ecosistema DECODIUM."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption backendOption(
        {QStringLiteral("b"), QStringLiteral("backend")},
        QStringLiteral("Backend da selezionare all'avvio (demo, soapy, …)."),
        QStringLiteral("id"));
    const QCommandLineOption autoConnectOption(
        QStringLiteral("auto-connect"),
        QStringLiteral("Connette automaticamente al primo device trovato."));
    const QCommandLineOption frequencyOption(
        {QStringLiteral("f"), QStringLiteral("frequency")},
        QStringLiteral("Sintonizza il canale attivo in MHz dopo la connessione."),
        QStringLiteral("MHz"));
    const QCommandLineOption modeOption(
        QStringLiteral("mode"),
        QStringLiteral("Modo del canale attivo (fm, nfm, am, usb, lsb, cw, ...)."),
        QStringLiteral("mode"));
    const QCommandLineOption noPanadapterOption(
        QStringLiteral("no-panadapter"),
        QStringLiteral("Non alimenta il panadattatore GPU (diagnostica prestazioni)."));
    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("Abilita i log dettagliati di HAL, DSP e audio."));
    const QCommandLineOption iqModuleOption(
        QStringLiteral("iq-module"),
        QStringLiteral("Carica un modulo IQ C ABI (.dylib/.so/.dll); ripetibile."),
        QStringLiteral("path"));
    parser.addOption(backendOption);
    parser.addOption(autoConnectOption);
    parser.addOption(frequencyOption);
    parser.addOption(modeOption);
    parser.addOption(noPanadapterOption);
    parser.addOption(verboseOption);
    parser.addOption(iqModuleOption);
    parser.process(app);

    qint64 startupFrequencyHz = 0;
    if (parser.isSet(frequencyOption)) {
        bool frequencyOk = false;
        const double frequencyMHz = parser.value(frequencyOption).toDouble(&frequencyOk);
        if (!frequencyOk || frequencyMHz <= 0.0) {
            qCritical() << "frequenza non valida:" << parser.value(frequencyOption);
            return 2;
        }
        startupFrequencyHz = static_cast<qint64>(frequencyMHz * 1'000'000.0);
    }

    QString startupMode = parser.value(modeOption).trimmed().toLower();
    if (startupMode.isEmpty() && startupFrequencyHz > 0)
        startupMode = QStringLiteral("fm");

    if (parser.isSet(verboseOption)) {
        qSetMessagePattern(QStringLiteral("[%{time hh:mm:ss.zzz}] %{type} %{category}: %{message}"));
        QLoggingCategory::setFilterRules(
            QStringLiteral("dsdr.*.debug=true\nqt.multimedia.*.debug=true\n"));
        qInfo() << "logging verboso attivo";
    }

    configureBundledSoapyModules();

    qInfo() << "avvio" << QCoreApplication::applicationVersion()
            << "backend richiesto" << parser.value(backendOption)
            << "auto-connect" << parser.isSet(autoConnectOption);

    dsdr::core::SessionManager session;
    dsdr::app::SessionSingleton::instance = &session;

    if (startupFrequencyHz > 0 || !startupMode.isEmpty()) {
        QObject::connect(&session, &dsdr::core::SessionManager::connectionChanged,
                         &session, [&session, startupFrequencyHz, startupMode] {
            if (!session.isConnected())
                return;
            // SessionManager emits connectionChanged just before it creates
            // the first RX channel. Defer the profile one event-loop turn so
            // tuneTo() can move both the center and the channel; otherwise a
            // startup frequency outside the initial 2.048 MHz span was only
            // written into a newly-created VFO and the device kept streaming
            // the old center frequency.
            QTimer::singleShot(0, &session,
                               [&session, startupFrequencyHz, startupMode] {
                if (!session.isConnected())
                    return;
                if (startupFrequencyHz > 0)
                    session.tuneTo(startupFrequencyHz);
                if (!startupMode.isEmpty()) {
                    const int row = session.channels()->currentIndex();
                    if (row >= 0) {
                        static const QHash<QString, dsdr::DemodMode> modes{
                            {QStringLiteral("fm"), dsdr::DemodMode::Fm},
                            {QStringLiteral("wfm"), dsdr::DemodMode::Fm},
                            {QStringLiteral("wide-fm"), dsdr::DemodMode::Fm},
                            {QStringLiteral("nfm"), dsdr::DemodMode::Nfm},
                            {QStringLiteral("am"), dsdr::DemodMode::Am},
                            {QStringLiteral("sam"), dsdr::DemodMode::Sam},
                            {QStringLiteral("usb"), dsdr::DemodMode::Usb},
                            {QStringLiteral("lsb"), dsdr::DemodMode::Lsb},
                            {QStringLiteral("cw"), dsdr::DemodMode::Cw},
                            {QStringLiteral("cwr"), dsdr::DemodMode::Cwr},
                            {QStringLiteral("iq"), dsdr::DemodMode::Iq},
                        };
                        const auto mode = modes.constFind(startupMode);
                        if (mode != modes.constEnd())
                            session.setChannelMode(row, static_cast<int>(mode.value()));
                        else
                            qWarning() << "modo di avvio non riconosciuto:" << startupMode;
                    }
                }
                qInfo() << "profilo di avvio applicato: frequenza"
                        << startupFrequencyHz << "modo" << startupMode;
            });
        });
    }

    session.loadIqModulesFromStandardPaths();
    for (const QString &modulePath : parser.values(iqModuleOption))
        session.loadIqModule(modulePath);

    if (parser.isSet(backendOption))
        session.selectBackend(parser.value(backendOption));

    if (parser.isSet(autoConnectOption)) {
        // Utile per la demo, per gli screenshot e per gli smoke test in CI:
        // niente interazione, si va in onda appena la discovery risponde.
        QObject::connect(&session, &dsdr::core::SessionManager::discoveringChanged, &session,
                         [&session] {
                             if (!session.isDiscovering() && !session.isConnected())
                                 session.connectToDevice(0);
                         });
        // La UI avvia già la discovery al caricamento; questo è solo la rete
        // di sicurezza se nessuno l'ha fatto entro mezzo secondo.
        QTimer::singleShot(500, &session, [&session] {
            if (!session.isDiscovering() && !session.isConnected())
                session.startDiscovery();
        });
    }

    QQmlApplicationEngine engine;

    // La lingua va scelta prima di caricare il QML: altrimenti la prima
    // schermata comparirebbe nella lingua sorgente e cambierebbe subito dopo.
    session.language()->attachEngine(&engine);
    session.language()->restoreSavedLanguage();

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("DecodiumSdr", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
