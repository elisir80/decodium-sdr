// SPDX-License-Identifier: GPL-3.0-or-later
// Traduzione profilo SoapySDR → capability della HAL.
//
// È la parte del backend soapy che si può verificare senza hardware, ed è
// anche quella che sbaglia più danni: le capability guidano la UI, quindi un
// errore qui fa comparire un PTT su una chiavetta che non trasmette o promette
// bande che il device non copre.

#include "hal/BackendRegistry.h"
#include "hal/IRadioBackend.h"
#include "hal/backends/soapy/SoapyProfile.h"

#include <QTest>

#include <memory>

using namespace dsdr;
using namespace dsdr::hal;
using namespace dsdr::hal::soapy;

namespace {

/// Profilo di una chiavetta RTL-SDR: sola ricezione, un canale.
SoapyDeviceProfile rtlSdrProfile()
{
    SoapyDeviceProfile profile;
    profile.driver = QStringLiteral("rtlsdr");
    profile.hardware = QStringLiteral("RTL2838");
    profile.rxChannels = 1;
    profile.txChannels = 0;
    profile.sampleRates = {250000.0, 1024000.0, 2048000.0, 2400000.0, 3200000.0};
    profile.minFrequencyHz = 24'000'000;
    profile.maxFrequencyHz = 1'766'000'000;
    profile.hasAgc = true;
    profile.minGainDb = 0.0;
    profile.maxGainDb = 49.6;
    profile.antennas = {QStringLiteral("RX")};
    return profile;
}

/// Profilo di un HackRF: trasmette, ma non in full duplex.
SoapyDeviceProfile hackRfProfile()
{
    SoapyDeviceProfile profile;
    profile.driver = QStringLiteral("hackrf");
    profile.hardware = QStringLiteral("HackRF One");
    profile.rxChannels = 1;
    profile.txChannels = 1;
    profile.fullDuplex = false;
    profile.sampleRates = {8000000.0, 10000000.0, 20000000.0};
    profile.minFrequencyHz = 1'000'000;
    profile.maxFrequencyHz = 6'000'000'000;
    return profile;
}

/// Profilo di un device a due canali coerenti.
SoapyDeviceProfile coherentProfile()
{
    SoapyDeviceProfile profile;
    profile.driver = QStringLiteral("lime");
    profile.rxChannels = 2;
    profile.txChannels = 2;
    profile.fullDuplex = true;
    profile.sampleRates = {2000000.0, 5000000.0};
    profile.minFrequencyHz = 100'000;
    profile.maxFrequencyHz = 3'800'000'000;
    return profile;
}

} // namespace

class TestSoapyProfile : public QObject
{
    Q_OBJECT

private slots:
    void backendIsRegistered();
    void receiveOnlyDeviceDeclaresNoTransmit();
    void physicalTransmitDeviceRemainsRxOnlyUntilTxPathExists();
    void physicalFullDuplexDeviceRemainsRxOnlyUntilTxPathExists();
    void coherentOnlyWithMultipleHardwareChannels();
    void frequencyCoverageFollowsTheDevice();
    void sampleRatesAreSortedAndDefaultIsSane();
    void defaultSampleRateAvoidsUsbOverruns();
    void automaticGainIsConservativeAndCorrectable();
    void invalidProfileIsRecognised();
};

void TestSoapyProfile::backendIsRegistered()
{
    registerBuiltinBackends();
    QVERIFY2(BackendRegistry::instance().contains(QStringLiteral("soapy")),
             "il backend soapy non risulta registrato");

    std::unique_ptr<IRadioBackend> backend(
        BackendRegistry::instance().create(QStringLiteral("soapy")));
    QVERIFY(backend);
    QCOMPARE(backend->backendId(), QStringLiteral("soapy"));

    // Senza device aperto le capability devono essere prudenti: nessun TX
    // promesso, nessuna banda inventata.
    const BackendCapabilities caps = backend->capabilities();
    QVERIFY(!caps.canTransmit());
    QVERIFY(caps.isRawIq());
    QVERIFY(caps.maxRxChannels >= 1);
}

void TestSoapyProfile::receiveOnlyDeviceDeclaresNoTransmit()
{
    const BackendCapabilities caps = capabilitiesFrom(rtlSdrProfile());

    // Una chiavetta RTL non trasmette: se lo dichiarasse, la UI mostrerebbe
    // un PTT che non può funzionare.
    QCOMPARE(caps.tx, TxSupport::None);
    QVERIFY(!caps.canTransmit());
    QVERIFY(caps.hasPreamp);       // ha un guadagno regolabile
    QVERIFY(caps.isRawIq());
}

void TestSoapyProfile::physicalTransmitDeviceRemainsRxOnlyUntilTxPathExists()
{
    const BackendCapabilities caps = capabilitiesFrom(hackRfProfile());
    // Il profilo hardware dichiara TX, ma il worker Soapy attuale non apre
    // uno stream TX: non bisogna esporre un PTT che non produce RF.
    QCOMPARE(caps.tx, TxSupport::None);
    QVERIFY(!caps.canTransmit());
}

void TestSoapyProfile::physicalFullDuplexDeviceRemainsRxOnlyUntilTxPathExists()
{
    const BackendCapabilities caps = capabilitiesFrom(coherentProfile());
    QCOMPARE(caps.tx, TxSupport::None);
    QVERIFY(!caps.canTransmit());
}

void TestSoapyProfile::coherentOnlyWithMultipleHardwareChannels()
{
    // Un solo canale hardware non può essere "coerente" con nessuno.
    QVERIFY(!capabilitiesFrom(rtlSdrProfile()).coherentRx);
    // Due canali dello stesso device sono campionati insieme: lo sono.
    QVERIFY(capabilitiesFrom(coherentProfile()).coherentRx);
}

void TestSoapyProfile::frequencyCoverageFollowsTheDevice()
{
    const BackendCapabilities rtl = capabilitiesFrom(rtlSdrProfile());
    QCOMPARE(rtl.minFrequencyHz, 24'000'000);
    QCOMPARE(rtl.maxFrequencyHz, 1'766'000'000);
    QVERIFY(rtl.coversFrequency(144'000'000));
    QVERIFY2(!rtl.coversFrequency(3'500'000),
             "gli 80 metri sono fuori dalla portata di un R820T senza upconverter");

    const BackendCapabilities hackrf = capabilitiesFrom(hackRfProfile());
    QVERIFY2(hackrf.coversFrequency(3'500'000), "un HackRF copre gli 80 metri");
    QVERIFY(hackrf.coversFrequency(5'000'000'000));
}

void TestSoapyProfile::sampleRatesAreSortedAndDefaultIsSane()
{
    SoapyDeviceProfile profile = rtlSdrProfile();
    profile.sampleRates = {3200000.0, 250000.0, 2048000.0}; // volutamente disordinati

    const BackendCapabilities caps = capabilitiesFrom(profile);
    QCOMPARE(caps.sampleRates.size(), 3);
    QVERIFY2(std::is_sorted(caps.sampleRates.begin(), caps.sampleRates.end()),
             "le frequenze di campionamento arrivano alla UI non ordinate");
    QVERIFY(caps.sampleRates.contains(caps.defaultSampleRate));
}

void TestSoapyProfile::defaultSampleRateAvoidsUsbOverruns()
{
    SoapyDeviceProfile profile = rtlSdrProfile();
    profile.preferredSampleRate = 0.0;   // il driver non esprime preferenze

    const BackendCapabilities caps = capabilitiesFrom(profile);

    // Oltre ~2,4 MS/s molti device perdono campioni su USB, e l'utente lo
    // interpreta come un difetto del programma.
    QVERIFY2(caps.defaultSampleRate <= 2'400'000.0,
             qPrintable(QStringLiteral("default troppo alto: %1").arg(caps.defaultSampleRate)));
    QCOMPARE(caps.defaultSampleRate, 2'400'000.0);
}

void TestSoapyProfile::automaticGainIsConservativeAndCorrectable()
{
    const SoapyDeviceProfile profile = rtlSdrProfile();
    QCOMPARE(safeAutoGainDb(profile), 20.0);

    const BackendCapabilities caps = capabilitiesFrom(profile);
    QCOMPARE(caps.maxGainReductionDb, 49.6);
}

void TestSoapyProfile::invalidProfileIsRecognised()
{
    SoapyDeviceProfile empty;
    QVERIFY(!empty.isValid());

    SoapyDeviceProfile txOnly;
    txOnly.txChannels = 1;
    QVERIFY2(!txOnly.isValid(), "un device senza canali RX non è utilizzabile da questo backend");
}

QTEST_MAIN(TestSoapyProfile)

#include "tst_soapy_profile.moc"
