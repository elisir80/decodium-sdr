// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — punto di ingresso.

#include "app/SessionSingleton.h"
#include "core/SessionManager.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

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
    parser.addOption(backendOption);
    parser.addOption(autoConnectOption);
    parser.addOption(noPanadapterOption);
    parser.process(app);

    dsdr::core::SessionManager session;
    dsdr::app::SessionSingleton::instance = &session;

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
