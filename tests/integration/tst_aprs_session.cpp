// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SessionManager.h"
#include <QTest>
using namespace dsdr;
using namespace dsdr::core;
class AprsSessionTest : public QObject {
    Q_OBJECT
private slots:
    void liveDemoSelectionAndDisconnect() {
        SessionManager session;
        session.selectBackend(QStringLiteral("demo"));
        session.startDiscovery();
        QTRY_VERIFY_WITH_TIMEOUT(session.devices()->rowCount() > 0, 3000);
        session.connectToDevice(0);
        QVERIFY(session.isConnected());
        QVERIFY(session.channels()->rowCount() > 0);
        session.setChannelMode(0, int(DemodMode::Nfm));
        const int id = int(session.channels()->at(0)->id);
        session.aprs()->setEnabled(true);
        session.aprs()->selectChannel(id);
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->status(), QStringLiteral("Ricezione APRS attiva"), 3000);
        session.disconnectDevice();
        QVERIFY(!session.isConnected());
        QVERIFY(session.aprs()->status() != QStringLiteral("Ricezione APRS attiva"));
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->selectedChannel(),-1,3000);
        QTRY_VERIFY_WITH_TIMEOUT(session.aprs()->source().isEmpty(),3000);
        session.connectToDevice(0);
        QVERIFY(session.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(session.channels()->rowCount()>0,3000);
        session.setChannelMode(0,int(DemodMode::Nfm));
        const int newId=int(session.channels()->at(0)->id);
        session.aprs()->selectChannel(newId);
        QTRY_COMPARE_WITH_TIMEOUT(session.aprs()->status(),QStringLiteral("Ricezione APRS attiva"),3000);
        session.requestShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(session.shutdownFencePassed(),10000);
    }
    void selectionTracksNfmChannels() {
        SessionManager session;
        auto *aprs = session.aprs();
        QVERIFY(aprs);
        QVERIFY(!aprs->enabled());
        QCOMPARE(aprs->selectedChannel(), -1);
        aprs->setEnabled(true);
        ChannelEntry first; first.id=12; first.label=QStringLiteral("RX A"); first.settings.mode=DemodMode::Usb;
        ChannelEntry second; second.id=13; second.label=QStringLiteral("RX B"); second.settings.mode=DemodMode::Nfm;
        session.channels()->append(first); session.channels()->append(second);
        QCOMPARE(aprs->channels().size(), 1);
        QCOMPARE(aprs->channels().first().toMap().value(QStringLiteral("id")).toInt(), 13);
        aprs->selectChannel(12); QCOMPARE(aprs->selectedChannel(), -1);
        aprs->selectChannel(13); QCOMPARE(aprs->selectedChannel(), 13);
        QVERIFY(aprs->status().contains(QStringLiteral("non disponibili")));
        auto *entry=session.channels()->mutableAt(0); entry->settings.mode=DemodMode::Nfm;
        session.channels()->entryChanged(0, {ChannelModel::ModeRole});
        QCOMPARE(aprs->channels().size(), 2);
        entry=session.channels()->mutableAt(1); entry->settings.mode=DemodMode::Usb;
        session.channels()->entryChanged(1, {ChannelModel::ModeRole});
        QCOMPARE(aprs->selectedChannel(), -1);
        QCOMPARE(aprs->channels().size(), 1);
        aprs->selectChannel(12); QCOMPARE(aprs->selectedChannel(), 12);
        session.channels()->removeAt(0); QCOMPARE(aprs->selectedChannel(), -1);
    }
};
QTEST_GUILESS_MAIN(AprsSessionTest)
#include "tst_aprs_session.moc"
