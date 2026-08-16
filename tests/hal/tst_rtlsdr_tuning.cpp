// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/backends/rtlsdr/RtlSdrCapabilities.h"
#include "hal/backends/rtlsdr/RtlSdrIfReference.h"
#include "hal/backends/rtlsdr/RtlSdrTuningPlan.h"

#include <QTest>

using namespace dsdr::hal::rtlsdr;

class TestRtlSdrTuning final : public QObject
{
    Q_OBJECT

private slots:
    void directRfMovesTheDcSpurAwayFromTheVfo();
    void fixedIfKeepsTheRadioDialAndAppliesUsbShift();
    void fixedIfFollowsTheRxVfoInsideThePanadapter();
    void lsbUsesItsOwnShift();
    void invertedIfConjugatesBeforeTranslation();
    void automaticIfSidebandFollowsTheReferenceRx();
    void directSamplingRespectsHardwareAndHfRange();
};

void TestRtlSdrTuning::directRfMovesTheDcSpurAwayFromTheVfo()
{
    TuningRequest request;
    request.dialFrequencyHz = 7'074'000;
    request.sampleRate = 240'000.0;

    const TuningPlan plan = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(plan.selectedInputFrequencyHz, qint64(7'074'000));
    QCOMPARE(plan.hardwareCenterFrequencyHz, qint64(7'134'000));
    QCOMPARE(plan.channelOffsetHz, qint64(-60'000));
    QCOMPARE(plan.basebandTranslationHz, 60'000.0);
    QCOMPARE(plan.logicalCenterFrequencyHz, qint64(7'074'000));
    QVERIFY(!plan.ifEnabled);
}

void TestRtlSdrTuning::fixedIfKeepsTheRadioDialAndAppliesUsbShift()
{
    TuningRequest request;
    request.dialFrequencyHz = 7'074'000;
    request.sampleRate = 240'000.0;
    request.ifEnabled = true;
    request.ifFrequencyHz = 8'830'000;
    request.usbShiftHz = 1'500;

    const TuningPlan plan = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(plan.selectedInputFrequencyHz, qint64(8'831'500));
    QCOMPARE(plan.hardwareCenterFrequencyHz, qint64(8'831'500));
    QCOMPARE(plan.channelOffsetHz, qint64(0));
    // Il tuner ha già spostato il centro della banda USB, come SDR++ in
    // modalità panadapter. Un mixer DSP aggiuntivo la sposterebbe due volte.
    QCOMPARE(plan.basebandTranslationHz, 0.0);
    QCOMPARE(plan.logicalCenterFrequencyHz, qint64(7'074'000));
    QVERIFY(plan.ifEnabled);

    request.dialFrequencyHz = 14'074'000;
    const TuningPlan secondBand = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(secondBand.selectedInputFrequencyHz, qint64(8'831'500));
    QCOMPARE(secondBand.hardwareCenterFrequencyHz, qint64(8'831'500));
    QCOMPARE(secondBand.logicalCenterFrequencyHz, qint64(14'074'000));
}

void TestRtlSdrTuning::fixedIfFollowsTheRxVfoInsideThePanadapter()
{
    TuningRequest request;
    request.dialFrequencyHz = 27'996'903;
    request.sampleRate = 2'048'000.0;
    request.ifEnabled = true;
    request.ifFrequencyHz = 8'830'000;
    request.usbShiftHz = 1'500;
    // RX 1 e' sul parlato della radio, mentre il centro grafico e' stato
    // trascinato 431.903 kHz piu' in alto.
    request.logicalSelectedOffsetHz = 27'565'000 - request.dialFrequencyHz;

    const TuningPlan plan = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(plan.selectedInputFrequencyHz, qint64(8'831'500));
    QCOMPARE(plan.hardwareCenterFrequencyHz, qint64(8'831'500));
    QCOMPARE(plan.channelOffsetHz, qint64(0));
    QCOMPARE(plan.logicalSelectedOffsetHz, qint64(-431'903));
    // I VFO virtuali sono scelti dal NCO del canale: l'IF fisso non deve
    // pre-traslare anche loro, o il movimento verrebbe contato due volte.
    QCOMPARE(plan.basebandTranslationHz, 0.0);
}

void TestRtlSdrTuning::lsbUsesItsOwnShift()
{
    TuningRequest request;
    request.dialFrequencyHz = 7'074'000;
    request.ifEnabled = true;
    request.ifFrequencyHz = 8'830'000;
    request.usbShiftHz = 1'500;
    request.lsbShiftHz = -1'500;
    request.sideband = IfSideband::Lsb;

    const TuningPlan plan = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(plan.selectedInputFrequencyHz, qint64(8'828'500));
    QCOMPARE(plan.hardwareCenterFrequencyHz, qint64(8'828'500));
    QCOMPARE(plan.channelOffsetHz, qint64(0));
    // Anche LSB è già collocata dal tuner sul proprio centro IF.
    QCOMPARE(plan.basebandTranslationHz, 0.0);
}

void TestRtlSdrTuning::invertedIfConjugatesBeforeTranslation()
{
    TuningRequest request;
    request.dialFrequencyHz = 7'074'000;
    request.sampleRate = 240'000.0;
    request.ifEnabled = true;
    request.ifFrequencyHz = 8'830'000;
    request.usbShiftHz = 1'500;
    request.spectrumInverted = true;

    const TuningPlan plan = makeTuningPlan(request, 100'000, 1'766'000'000);
    QCOMPARE(plan.channelOffsetHz, qint64(0));
    // L'inversione coniuga i campioni ma non richiede un secondo mixer IF.
    QCOMPARE(plan.basebandTranslationHz, 0.0);
    QCOMPARE(plan.logicalCenterFrequencyHz, qint64(7'074'000));
    QVERIFY(plan.spectrumInverted);
}

void TestRtlSdrTuning::automaticIfSidebandFollowsTheReferenceRx()
{
    dsdr::hal::RxChannelConfig reference;
    reference.frequencyHz = 14'995'000;
    reference.mode = dsdr::DemodMode::Usb;

    // Un RX virtuale LSB non deve spostare l'IF di RX 1: è questo caso che
    // faceva sintonizzare 8,8285 MHz anche con la radio in USB.
    dsdr::hal::RxChannelConfig virtualRx = reference;
    virtualRx.frequencyHz += 5'000;
    virtualRx.mode = dsdr::DemodMode::Lsb;
    QHash<dsdr::ChannelId, dsdr::hal::RxChannelConfig> channels;
    channels.insert(2, virtualRx);
    channels.insert(1, reference);

    QVERIFY(!ifReferenceUsesLsb(channels, dsdr::DemodMode::Lsb));

    // Quando cambia RX 1, invece, l'IF deve seguire realmente USB -> LSB.
    channels[1].mode = dsdr::DemodMode::Lsb;
    QVERIFY(ifReferenceUsesLsb(channels, dsdr::DemodMode::Usb));

    // Cambiare di nuovo il canale virtuale non può annullare il riferimento
    // LSB scelto da RX 1.
    channels[2].mode = dsdr::DemodMode::Usb;
    QVERIFY(ifReferenceUsesLsb(channels, dsdr::DemodMode::Usb));
}

void TestRtlSdrTuning::directSamplingRespectsHardwareAndHfRange()
{
    QCOMPARE(directSamplingBlockReason(QStringLiteral("RTLSDRBlog Blog V4"), 7'074'000),
             DirectSamplingBlockReason::BlogV4UsesUpconverter);
    QCOMPARE(directSamplingBlockReason(QStringLiteral("RTL-SDR Blog V3"), 499'999),
             DirectSamplingBlockReason::OutsideHfRange);
    QCOMPARE(directSamplingBlockReason(QStringLiteral("RTL-SDR Blog V3"), 7'074'000),
             DirectSamplingBlockReason::None);
    QCOMPARE(directSamplingBlockReason(QStringLiteral("RTL-SDR Blog V3"), 24'000'001),
             DirectSamplingBlockReason::OutsideHfRange);
}

QTEST_MAIN(TestRtlSdrTuning)
#include "tst_rtlsdr_tuning.moc"
