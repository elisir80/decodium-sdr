// SPDX-License-Identifier: GPL-3.0-or-later
#define main fake_shutdown_reference_main
#include "tst_shutdown_fake_radio_standalone.cpp"
#undef main
#include "app/SessionSingleton.h"
#include "core/Vdl2AppManager.h"
#include "core/DspEngine.h"
#include "dsp/SpscRing.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>
#include <QFile>
#include <QDataStream>
Q_IMPORT_QML_PLUGIN(DecodiumSdrPlugin)
class Vdl2NoIqUiRadio final : public hal::demo::DemoBackend {
public:
    explicit Vdl2NoIqUiRadio(QObject *parent) : DemoBackend(parent) {}
    hal::BackendCapabilities capabilities() const override {
        auto caps = DemoBackend::capabilities();
        caps.demod = DspLocation::Device;
        return caps;
    }
};
static QQuickItem *findVisual(QQuickItem *item, const QString &name) {
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *found = findVisual(child, name)) return found;
    return nullptr;
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto root = qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    const auto settings = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const auto data = qEnvironmentVariable("DSDR_TEST_APPDATA_DIR");
    if (!isolatedTestDirectory(settings, root) || !isolatedTestDirectory(data, root)) return 77;
    QCoreApplication::setOrganizationName("DSDR-shutdown-test");
    QCoreApplication::setApplicationName("vdl2-main-qml");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    QQuickStyle::setStyle("Basic");
    const bool iqFileTest = qEnvironmentVariableIntValue("DSDR_TEST_VDL2_IQFILE") == 1;
    const bool recordedIqTest = qEnvironmentVariableIntValue("DSDR_TEST_VDL2_RECORDED") == 1;
    const bool recordedNegative = qEnvironmentVariableIntValue("DSDR_TEST_VDL2_RECORDED_NEGATIVE") == 1;
    assert(!recordedIqTest || iqFileTest);
    assert(!recordedNegative || recordedIqTest);
    const int requestedRepetitions=qEnvironmentVariableIntValue("DSDR_TEST_VDL2_REPETITIONS");
    const int repetitions=requestedRepetitions>0 && requestedRepetitions<=64 ? requestedRepetitions :
        (qEnvironmentVariableIntValue("DSDR_TEST_VDL2_STRESS") ? 8 : 1);
    QString wavPath, jsonPath;
    if (iqFileTest) {
        QFile fixture(qEnvironmentVariable("DSDR_TEST_VDL2_IQ")); assert(fixture.open(QIODevice::ReadOnly));
        const QByteArray samples = fixture.readAll();
        assert(recordedIqTest ? samples.size() == 192000 * 6 * 8 : samples.size() == 82300 * 8);
        wavPath = root + "/vdl2-fixture.wav"; jsonPath = root + "/vdl2-fixture.json";
        QFile wav(wavPath); assert(wav.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray silence(192000 * 3 * 8, '\0');
        const quint32 size = quint32(silence.size() + samples.size() * repetitions);
        QDataStream out(&wav); out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF",4); out << quint32(36 + size); out.writeRawData("WAVEfmt ",8);
        out << quint32(16) << quint16(3) << quint16(2) << quint32(192000) << quint32(192000*8) << quint16(8) << quint16(32);
        out.writeRawData("data",4); out << size; assert(wav.write(silence)==silence.size());
        for (int i=0; i<repetitions; ++i) assert(wav.write(samples)==samples.size());
        wav.close();
        QFile json(jsonPath); assert(json.open(QIODevice::WriteOnly | QIODevice::Truncate));
        assert(json.write("{\"centerFrequencyHz\":136975000}") > 0); json.close();
        qputenv("DSDR_IQFILE_PATH", wavPath.toLocal8Bit());
    }
    auto trace = std::make_shared<Trace>(); FakeRadio *fake = nullptr;
    hal::BackendRegistry::instance().registerBackend({"vdl2-qml-fake", "VDL2 fake", "test"},
        [&fake, trace](QObject *parent) { fake = new FakeRadio(trace, parent); return fake; });
    SessionManager session; dsdr::app::SessionSingleton::instance = &session;
    session.selectBackend(iqFileTest ? "iqfile" : "vdl2-qml-fake"); session.startDiscovery();
    assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
    session.connectToDevice(0); assert(session.isConnected() && (iqFileTest || fake));
    Vdl2AppManager manager(&session);
    QQmlApplicationEngine engine;
    const bool enabled = qEnvironmentVariableIntValue("DSDR_TEST_VDL2_AVAILABLE") == 1;
    engine.rootContext()->setContextProperty("vdl2Manager", enabled ? static_cast<QObject *>(&manager) : nullptr);
    engine.loadFromModule("DecodiumSdr", "Main");
    assert(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); assert(window);
    int heartbeatCount = 0; QTimer heartbeat; heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeatCount; }); heartbeat.start();
    std::unique_ptr<dsdr::dsp::SpscRing<float>> testRing;
    QByteArray testIq;
    std::atomic<bool> iqDelivered{false};
    auto *menu = window->findChild<QObject *>("vdl2MenuItem"); assert(menu);
    auto *menuBar = window->findChild<QObject *>("mainMenu"); assert(menuBar);
    assert(menuBar->property("vdl2Available").toBool() == enabled);
    assert(menu->property("enabled").toBool() == enabled);
    if (!enabled) assert(menu->property("text").toString().contains(QStringLiteral("non incluso")));
    if (enabled) {
        assert(QMetaObject::invokeMethod(menu, "triggered"));
        auto *dialog = window->findChild<QObject *>("vdl2Setup"); assert(dialog);
        assert(waitFor([&] { return dialog->property("visible").toBool(); }));
        auto *freq = dialog->findChild<QObject *>("vdl2Frequency");
        auto *start = dialog->findChild<QObject *>("vdl2Start"); assert(freq && start);
        auto *stop = dialog->findChild<QObject *>("vdl2Stop"); assert(stop);
        assert(start->property("enabled").toBool() && !stop->property("enabled").toBool());
        freq->setProperty("text", QString());
        assert(!start->property("enabled").toBool());
        freq->setProperty("text", QStringLiteral("0"));
        assert(!start->property("enabled").toBool());
        auto *status = dialog->findChild<QObject *>("vdl2SetupStatus"); assert(status);
        if (!iqFileTest) {
            session.disconnectDevice();
            assert(waitFor([&] { return !session.isConnected(); }));
            freq->setProperty("text", QStringLiteral("136975000"));
            assert(QMetaObject::invokeMethod(start, "clicked"));
            assert(!manager.busy() && !manager.controller());
            assert(status->property("text").toString().contains(QStringLiteral("sorgente IQ")));
            session.connectToDevice(0);
            assert(waitFor([&] { return session.isConnected(); }));
        }
        if (qEnvironmentVariableIntValue("DSDR_TEST_VDL2_NO_IQ")) {
            session.disconnectDevice(); assert(waitFor([&] { return !session.isConnected(); }));
            hal::BackendRegistry::instance().registerBackend({"vdl2-ui-no-iq", "VDL2 no IQ", "test"},
                [](QObject *parent) { return new Vdl2NoIqUiRadio(parent); });
            session.selectBackend("vdl2-ui-no-iq"); session.startDiscovery();
            assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
            session.connectToDevice(0); assert(waitFor([&] { return session.isConnected(); }));
            assert(!session.capabilities()->clientDemod());
            freq->setProperty("text", QString::number(session.centerFrequency()));
            assert(QMetaObject::invokeMethod(start, "clicked"));
            assert(!manager.busy() && !manager.controller());
            assert(status->property("text").toString().contains(QStringLiteral("sorgente IQ")));
            session.disconnectDevice(); assert(waitFor([&] { return !session.isConnected(); }));
            session.selectBackend("vdl2-qml-fake"); session.startDiscovery();
            assert(waitFor([&] { return session.devices()->rowCount() > 0; }));
            session.connectToDevice(0); assert(waitFor([&] { return session.isConnected(); }));
        }
        freq->setProperty("text", QString::number(session.centerFrequency() + qint64(session.sampleRate())));
        assert(start->property("enabled").toBool());
        assert(QMetaObject::invokeMethod(start, "clicked"));
        assert(!manager.busy() && !manager.controller());
        assert(status->property("text").toString().contains(QStringLiteral("fuori dalla banda IQ")));
        freq->setProperty("text", QString::number(session.centerFrequency()));
        assert(start->property("enabled").toBool());
        assert(QMetaObject::invokeMethod(start, "clicked"));
        assert(manager.busy());
        assert(!start->property("enabled").toBool() && stop->property("enabled").toBool());
        assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); },10000));
        assert(!start->property("enabled").toBool() && stop->property("enabled").toBool());
        if (qEnvironmentVariableIntValue("DSDR_TEST_VDL2_UI_RESTART")) {
            assert(QMetaObject::invokeMethod(stop, "clicked"));
            assert(waitFor([&] { return !manager.controller() && !session.externalShutdownPending(); },10000));
            assert(session.isConnected() && start->property("enabled").toBool());
            assert(QMetaObject::invokeMethod(start, "clicked"));
            assert(waitFor([&] { return manager.controller() && manager.state() == QStringLiteral("Attivo"); },10000));
        }
        auto *show = dialog->findChild<QObject *>("vdl2Show"); assert(show);
        assert(waitFor([&] { return show->property("enabled").toBool(); }));
        assert(QMetaObject::invokeMethod(show, "clicked"));
        assert(waitFor([&] { auto *display=window->findChild<QObject *>("vdl2Dialog"); return display && display->property("visible").toBool(); }));
        assert(waitFor([&] { auto *list=window->findChild<QObject *>("vdl2Messages"); return list && list->property("count").toInt()==0; }));
        const QString fixture = qEnvironmentVariable("DSDR_TEST_VDL2_IQ");
        if (!fixture.isEmpty() && !iqFileTest) {
            QFile file(fixture); assert(file.open(QIODevice::ReadOnly)); testIq = file.readAll();
            assert(testIq.size() == 82300 * 2 * int(sizeof(float)));
            testRing = std::make_unique<dsdr::dsp::SpscRing<float>>(32768);
            auto *dsp = session.dspEngineForOptionalDecoder();
            const auto center = session.centerFrequency();
            assert(QMetaObject::invokeMethod(dsp, [&, dsp, center] {
                dsp->setSource(testRing.get(), 192000, center, false);
                const auto *samples = reinterpret_cast<const float *>(testIq.constData());
                for (std::size_t at=0; at<82300; at+=1024) {
                    const auto n=std::min(std::size_t(1024),std::size_t(82300)-at);
                    assert(testRing->write(samples+2*at,2*n)==2*n);
                    dsdr::hal::IqFrame frame; frame.frameCount=n; dsp->onIqFrameReady(frame);
                    QThread::msleep(1);
                }
                iqDelivered.store(true);
            }, Qt::QueuedConnection));
            assert(waitFor([&] { return iqDelivered.load(); },10000));
            assert(waitFor([&] { auto *list=window->findChild<QObject *>("vdl2Messages"); return list && list->property("count").toInt()==4; },10000));
        }
        if (iqFileTest && !recordedNegative)
            assert(waitFor([&] { auto *list=window->findChild<QObject *>("vdl2Messages"); return list && list->property("count").toInt()==(recordedIqTest?5:4) * repetitions; },45000));
        if (recordedNegative) {
            QElapsedTimer elapsed; elapsed.start();
            assert(waitFor([&] { return elapsed.elapsed() >= 10000; },12000));
            auto *list=window->findChild<QObject *>("vdl2Messages");
            assert(list && list->property("count").toInt()==0);
        }
        if (recordedIqTest) {
            assert(heartbeatCount > 50);
            assert(manager.controller()->property("droppedResults").toULongLong() == 0);
            assert(manager.controller()->property("droppedBlocks").toULongLong() == 0);
            assert(manager.controller()->property("workerErrors").toULongLong() == 0);
        }
        if (iqFileTest && repetitions == 1 && !recordedNegative) {
            assert(waitFor([&] { return findVisual(window->contentItem(), "vdl2FrameHeading") != nullptr; }));
            auto *heading = findVisual(window->contentItem(), "vdl2FrameHeading");
            auto *summary = findVisual(window->contentItem(), "vdl2FrameSummary");
            assert(heading && summary);
            assert(heading->property("textFormat").toInt() == 0);
            assert(summary->property("textFormat").toInt() == 0);
        }
        if (repetitions > 1) {
            assert(heartbeatCount > 100);
            assert(manager.controller()->property("droppedResults").toULongLong() == 0);
            assert(manager.controller()->property("droppedBlocks").toULongLong() == 0);
            assert(manager.controller()->property("workerErrors").toULongLong() == 0);
        }
        if (iqFileTest && qEnvironmentVariableIntValue("DSDR_TEST_VDL2_RETUNE")) {
            auto *stop=dialog->findChild<QObject *>("vdl2Stop"); assert(stop);
            assert(QMetaObject::invokeMethod(menu, "triggered"));
            assert(QMetaObject::invokeMethod(stop, "clicked"));
            assert(waitFor([&] { return !manager.controller() && !session.externalShutdownPending(); },10000));
            freq->setProperty("text", QString::number(session.centerFrequency()+25000));
            assert(QMetaObject::invokeMethod(start, "clicked"));
            assert(waitFor([&] { return manager.controller() && manager.state()==QStringLiteral("Attivo"); },10000));
            assert(QMetaObject::invokeMethod(show, "clicked"));
            auto *list=window->findChild<QObject *>("vdl2Messages"); assert(list && list->property("count").toInt()==0);
            session.nativeCommand(QStringLiteral("iqfile.seek"), {{QStringLiteral("ms"),0}});
            QElapsedTimer elapsed; elapsed.start();
            assert(waitFor([&] { return elapsed.elapsed()>=4500; },7000));
            assert(list->property("count").toInt()==0);
            assert(QMetaObject::invokeMethod(menu, "triggered"));
            assert(QMetaObject::invokeMethod(stop, "clicked"));
            assert(waitFor([&] { return !manager.controller() && !session.externalShutdownPending(); },10000));
            freq->setProperty("text", QString::number(session.centerFrequency()));
            assert(QMetaObject::invokeMethod(start, "clicked"));
            assert(waitFor([&] { return manager.controller() && manager.state()==QStringLiteral("Attivo"); },10000));
            assert(QMetaObject::invokeMethod(show, "clicked"));
            list=window->findChild<QObject *>("vdl2Messages"); assert(list && list->property("count").toInt()==0);
            session.nativeCommand(QStringLiteral("iqfile.seek"), {{QStringLiteral("ms"),0}});
            assert(waitFor([&] { return list->property("count").toInt()==4; },12000));
        }
        if (iqFileTest && qEnvironmentVariableIntValue("DSDR_TEST_VDL2_RECONNECT")) {
            session.disconnectDevice();
            assert(waitFor([&] { auto *list=window->findChild<QObject *>("vdl2Messages"); return !session.isConnected() && list && list->property("count").toInt()==0; },10000));
            assert(manager.controller()->property("sourceStatus").toString() == QStringLiteral("In attesa della sorgente IQ"));
            session.startDiscovery();
            assert(waitFor([&] { return !session.isDiscovering() && session.devices()->rowCount()>0; }));
            session.connectToDevice(0); assert(waitFor([&] { return session.isConnected(); }));
            assert(waitFor([&] { auto *list=window->findChild<QObject *>("vdl2Messages"); return list && list->property("count").toInt()==4; },12000));
        }
    } else if (iqFileTest) {
        QElapsedTimer playback; playback.start();
        assert(waitFor([&] { return playback.elapsed() >= 4000; },6000));
        assert(!manager.controller() && !session.dspEngineForOptionalDecoder()->hasRawIqTap());
    }
    window->close();
    assert(waitFor([&] { return session.shutdownFencePassed(); },10000));
    assert(waitFor([&] { return !window->isVisible(); }));
    if (iqFileTest) { assert(QFile::remove(wavPath)); assert(QFile::remove(jsonPath)); }
    return 0;
}
