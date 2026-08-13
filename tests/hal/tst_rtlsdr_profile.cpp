// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"
#include "hal/backends/rtlsdr/RtlSdrProfile.h"

#include <QTest>

#include <algorithm>

using namespace dsdr;
using namespace dsdr::hal;
using namespace dsdr::hal::rtlsdr;

namespace {

RtlSdrDeviceProfile rtlBlogV4Profile()
{
    RtlSdrDeviceProfile profile;
    profile.index = 0;
    profile.product = QStringLiteral("Blog V4");
    profile.tuner = QStringLiteral("Rafael Micro R828D");
    profile.sampleRates = {2'400'000.0, 250'000.0, 2'048'000.0};
    profile.preferredSampleRate = 2'048'000.0;
    profile.gainTenthsDb = {496, 0, 280, 99};
    profile.minFrequencyHz = 24'000'000;
    profile.maxFrequencyHz = 1'766'000'000;
    return profile;
}

} // namespace

class TestRtlSdrProfile : public QObject
{
    Q_OBJECT

private slots:
    void backendIsRegistered();
    void receiveOnlyAndNativePanel();
    void ratesAreSortedAndDefaultIsSafe();
    void directSamplingExtendsCoverage();
    void autoGainStartsAtASafeStep();
};

void TestRtlSdrProfile::backendIsRegistered()
{
    registerBuiltinBackends();
    QVERIFY(BackendRegistry::instance().contains(QStringLiteral("rtlsdr")));
    std::unique_ptr<IRadioBackend> backend(
        BackendRegistry::instance().create(QStringLiteral("rtlsdr")));
    QVERIFY(backend);
    QCOMPARE(backend->backendId(), QStringLiteral("rtlsdr"));
}

void TestRtlSdrProfile::receiveOnlyAndNativePanel()
{
    const BackendCapabilities caps = capabilitiesFrom(rtlBlogV4Profile());
    QCOMPARE(caps.tx, TxSupport::None);
    QVERIFY(!caps.canTransmit());
    QVERIFY(caps.hasPreamp);
    QVERIFY(caps.isRawIq());
    QVERIFY(caps.nativePanels.contains(QStringLiteral("RtlSdrDevicePanel")));
    QVERIFY(caps.coherentRx);
}

void TestRtlSdrProfile::ratesAreSortedAndDefaultIsSafe()
{
    const BackendCapabilities caps = capabilitiesFrom(rtlBlogV4Profile());
    QVERIFY(std::is_sorted(caps.sampleRates.begin(), caps.sampleRates.end()));
    QCOMPARE(caps.defaultSampleRate, 2'048'000.0);
    QVERIFY(caps.defaultSampleRate <= 2'400'000.0);
}

void TestRtlSdrProfile::directSamplingExtendsCoverage()
{
    RtlSdrDeviceProfile profile = rtlBlogV4Profile();
    profile.directSampling = true;
    const BackendCapabilities caps = capabilitiesFrom(profile);
    QCOMPARE(caps.minFrequencyHz, qint64(0));
    QVERIFY(caps.coversFrequency(3'500'000));
    QVERIFY(caps.coversFrequency(100'000'000));
}

void TestRtlSdrProfile::autoGainStartsAtASafeStep()
{
    QCOMPARE(safeAutoGainTenthsDb({0, 99, 198, 280, 370, 496}), 198);
    QCOMPARE(safeAutoGainTenthsDb({0, 280, 496}), 0);

    const BackendCapabilities caps = capabilitiesFrom(rtlBlogV4Profile());
    QCOMPARE(caps.maxGainReductionDb, 49.6);
}

QTEST_MAIN(TestRtlSdrProfile)
#include "tst_rtlsdr_profile.moc"
