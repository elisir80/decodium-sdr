// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic filtered IQ -> async receiver -> controller -> actual AIS dialog.
// This deliberately does not instantiate DspEngine, a backend or SessionManager.
#include "core/AisController.h"
#include "core/AisReceiver.h"
#include "core/AisEngineTap.h"
#include "ais_synthetic.h"
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <limits>
#include <vector>
using namespace dsdr::core;
namespace {
struct Monitor {
    std::atomic<unsigned> calls{0};
    static void entered(void *p) noexcept { ++static_cast<Monitor *>(p)->calls; }
};
}
class TestAisEndToEnd : public QObject {
    Q_OBJECT
private slots:
    void iqToDialogAB() {
        QTemporaryDir temp(QDir::tempPath()+QStringLiteral("/dsdr-ais-qml-XXXXXX"));
        QVERIFY2(temp.isValid(), "Cannot create temporary AIS QML directory");
        QVERIFY(QDir().mkpath(temp.path()+QStringLiteral("/DecodiumSdr")));
        QFile module(temp.path()+QStringLiteral("/DecodiumSdr/qmldir"));
        QVERIFY(module.open(QIODevice::WriteOnly));
        QVERIFY(module.write("module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n")>0);
        module.close();
        QVERIFY(QFile::copy(QStringLiteral(DSDR_AIS_QML_DIR "/Theme.qml"),
                            temp.path()+QStringLiteral("/DecodiumSdr/Theme.qml")));
        Monitor monitor;
        AisReceiver rx(64,8192,16,Monitor::entered,&monitor);
        AisController controller(&rx);
        QQmlApplicationEngine qml;
        qml.addImportPath(temp.path());
        qml.rootContext()->setContextProperty("aisController",&controller);
        const auto source=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { width: 800; height: 600; visible: true
  App.AisDialog { objectName: "dialog"; controller: aisController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_AIS_QML_DIR)).toString());
        qml.loadData(source.toUtf8(),QUrl::fromLocalFile(temp.path()+QStringLiteral("/test.qml")));
        QVERIFY2(!qml.rootObjects().isEmpty(), "AIS dialog did not load");
        QObject *root=qml.rootObjects().first();
        auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        auto *dialog=root->findChild<QObject *>("dialog"); QVERIFY(dialog);
        auto *enabled=root->findChild<QQuickItem *>("aisEnabled"); QVERIFY(enabled);
        auto *type=root->findChild<QObject *>("aisType"); QVERIFY(type);
        auto *mmsi=root->findChild<QObject *>("aisMmsi"); QVERIFY(mmsi);
        auto *position=root->findChild<QObject *>("aisPosition"); QVERIFY(position);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(),5000);
        QVERIFY(!controller.enabled());
        rx.start(); rx.setSource(true,48000,0);
        AisEngineTap tap(rx);
        QVERIFY(tap.configure(192000,25000));
        auto good=ais_test::transmit(ais_test::position(),192000,25000,0.015,3,false);
        auto bad=ais_test::transmit(ais_test::position(),192000,25000,0.015,3,true);
        auto other=ais_test::transmit(ais_test::position(987654321),192000,-25000,0.015,3,false);
        QCOMPARE(good.size(),other.size()); QCOMPARE(bad.size(),other.size());
        for(std::size_t i=0;i<good.size();++i) { good[i]+=other[i]; bad[i]+=other[i]; }
        auto feed=[&](const auto &samples) {
            for(std::size_t at=0;at<samples.size();) {
                const auto n=std::min<std::size_t>(1024,samples.size()-at);
                const auto generation=rx.generation();
                tap.process(samples.data()+at,n,generation); at+=n;
            }
        };
        // A: disabled. B: enable using the real QML checkbox.
        feed(good); QCOMPARE(rx.stats().accepted,std::uint64_t(0));
        QCOMPARE(controller.frameCount(),qulonglong(0));
        auto center=enabled->mapToScene(QPointF(enabled->width()/2,enabled->height()/2));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center.toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(controller.enabled(),3000);
        QCOMPARE(rx.status(),AisReceiver::Status::Active);
        tap.reset();
        // C: corrupted target FCS, while the adjacent AIS channel carries a
        // valid packet. A one-sample barrier proves the preceding IQ drained.
        auto calls=monitor.calls.load(); const auto acceptedBefore=rx.stats().accepted; feed(bad);
        const auto submitted=rx.stats().accepted-acceptedBefore;
        QVERIFY(submitted>0);
        const dsdr::dsp::Complex barrier{1,0};
        QCOMPARE(rx.submit(&barrier,1),AsyncIqDecoder::SubmitResult::Accepted);
        QTRY_VERIFY_WITH_TIMEOUT(monitor.calls.load()>=calls+submitted+1,5000);
        controller.poll(); QCOMPARE(controller.frameCount(),qulonglong(0));
        // D: target valid, adjacent channel valid with a different MMSI.
        feed(good);
        QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),controller.frameCount()==1),5000);
        QCOMPARE(controller.messageType(),1);
        QCOMPARE(controller.mmsi(),QStringLiteral("123456789"));
        QVERIFY(controller.hasPosition());
        QVERIFY(qAbs(controller.latitude()-35.9)<1e-6);
        QVERIFY(qAbs(controller.longitude()-14.5)<1e-6);
        QTRY_VERIFY(type->property("text").toString().contains(QStringLiteral("1")));
        QTRY_VERIFY(mmsi->property("text").toString().contains(QStringLiteral("123456789")));
        QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("35.90000")));
        // Retune selects the other channel and invalidates pre-acquired IQ.
        const auto old=rx.generation(); rx.discontinuity(); controller.poll();
        QCOMPARE(rx.submitForGeneration(other.data(),other.size(),old),AsyncIqDecoder::SubmitResult::Stopped);
        QVERIFY(!controller.hasPosition()); QVERIFY(controller.mmsi().isEmpty());
        QTRY_VERIFY(position->property("text").toString().contains(QStringLiteral("non disponibile")));
        QVERIFY(tap.configure(192000,-25000));
        feed(good);
        QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),controller.frameCount()==2),5000);
        QCOMPARE(controller.mmsi(),QStringLiteral("987654321"));
        // A common device rate is not an integer multiple of 48 kHz.
        // Resample the independent 192 kHz IQ fixture to 2.048 MHz.
        std::vector<std::complex<float>> faster;
        faster.reserve(good.size()*2048/192+1);
        for(std::size_t i=0;;++i) {
            const double pos=double(i)*192000.0/2048000.0;
            const auto index=std::size_t(pos);
            if(index+1>=good.size()) break;
            const float frac=float(pos-index);
            faster.push_back(good[index]*(1-frac)+good[index+1]*frac);
        }
        QVERIFY(tap.configure(2048000,-25000));
        rx.setSource(true,tap.outputRate(),0); feed(faster);
        QTRY_VERIFY_WITH_TIMEOUT((controller.poll(),controller.frameCount()==3),5000);
        QCOMPARE(controller.mmsi(),QStringLiteral("987654321"));
        // An invalid input sample is propagated as an explicit loss.
        auto generation=rx.generation();
        const dsdr::dsp::Complex invalid{std::numeric_limits<float>::quiet_NaN(),0};
        tap.process(&invalid,1,generation); QVERIFY(rx.generation()!=generation);
        rx.setSource(false,48000,0); controller.poll();
        QVERIFY(controller.mmsi().isEmpty()); QVERIFY(!controller.hasPosition());
        // Receiver owner is still alive and producer is quiescent. This is a
        // test thread, not the application's GUI shutdown path.
        rx.stop();
    }
};
QTEST_MAIN(TestAisEndToEnd)
#include "tst_ais_end_to_end.moc"
