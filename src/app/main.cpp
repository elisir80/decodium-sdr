// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — punto di ingresso.

#include "app/SessionSingleton.h"
#include "core/SessionManager.h"

#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

namespace {

void configureBundledSoapyModules()
{
#if defined(Q_OS_MACOS)
    // SoapySDR otherwise searches the Homebrew/system prefix compiled into
    // its library. A distributed .app has its plugins next to the bundle,
    // so point the loader there before SessionManager can enumerate devices.
    const QString modulePath =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../PlugIns/SoapySDR/modules0.8"));
    if (qEnvironmentVariableIsEmpty("SOAPY_SDR_PLUGIN_PATH")
        && QFileInfo(modulePath).isDir()) {
        qputenv("SOAPY_SDR_PLUGIN_PATH", modulePath.toUtf8());
    }
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName(QStringLiteral("DECODIUM SDR"));
    QGuiApplication::setApplicationVersion(QStringLiteral(DSDR_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("DECODIUM"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("decodium.it"));

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
    parser.addOption(noPanadapterOption);
    parser.addOption(verboseOption);
    parser.addOption(iqModuleOption);
    parser.process(app);

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
