// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlcat/RtlSdrCatBackend.h"
#include "hal/backends/rtlcat/RtlSdrTxSafety.h"

#include <QtTest>

using namespace dsdr;
using namespace dsdr::hal;
using namespace dsdr::hal::rtlcat;

class RtlCatSafetyTest final : public QObject
{
    Q_OBJECT

private slots:
    void inputIsFailClosedUntilRxIsKnown();
    void backendDeclaresSingleRadioAuthoritativeVfo();
    void profileRequiresHamlibModelForLocalSerial();
};

void RtlCatSafetyTest::inputIsFailClosedUntilRxIsKnown()
{
    QVERIFY(shouldBlockRtlInput(false, false));
    QVERIFY(shouldBlockRtlInput(false, true));
    QVERIFY(shouldBlockRtlInput(true, true));
    QVERIFY(!shouldBlockRtlInput(true, false));
}

void RtlCatSafetyTest::backendDeclaresSingleRadioAuthoritativeVfo()
{
    RtlSdrCatBackend backend;
    const BackendCapabilities caps = backend.capabilities();
    QCOMPARE(caps.maxRxChannels, 1);
    QVERIFY(!caps.coherentRx);
    QVERIFY(caps.vfoFollowsRadio);
    QVERIFY(caps.nativePanels.contains(QStringLiteral("RtlSdrDevicePanel")));
    QVERIFY(caps.nativePanels.contains(QStringLiteral("RtlCatPanel")));
}

void RtlCatSafetyTest::profileRequiresHamlibModelForLocalSerial()
{
    RtlSdrCatBackend backend;
    QVariantMap profile{{QStringLiteral("driver"), QStringLiteral("hamlib-local")},
                        {QStringLiteral("port"), QStringLiteral("tty.test")}};
    QCOMPARE(backend.nativeCommand(QStringLiteral("device.declare"), profile).toBool(), false);
    profile.insert(QStringLiteral("hamlibModel"), 2011);
    QCOMPARE(backend.nativeCommand(QStringLiteral("device.declare"), profile).toBool(), true);
}

QTEST_GUILESS_MAIN(RtlCatSafetyTest)
#include "tst_rtlcat_safety.moc"
