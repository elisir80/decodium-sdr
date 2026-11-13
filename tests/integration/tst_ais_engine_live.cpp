// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "core/AisController.h"
#include "../standalone/ais_synthetic.h"
#include <QTest>
#include <atomic>
#include <vector>
using namespace dsdr;
using namespace dsdr::core;
namespace {
struct Monitor { std::atomic<std::uint64_t> calls{0}; static void hook(void *p) noexcept { ++static_cast<Monitor *>(p)->calls; } };
void feed(DspEngine &engine,dsp::SpscRing<float> &ring,const std::vector<std::complex<float>> &iq) {
    std::vector<float> buffer;
    for(std::size_t at=0;at<iq.size();) {
        const auto n=std::min<std::size_t>(4096,iq.size()-at); buffer.resize(n*2);
        for(std::size_t i=0;i<n;++i) { buffer[2*i]=iq[at+i].real();buffer[2*i+1]=iq[at+i].imag(); }
        QCOMPARE(ring.write(buffer.data(),buffer.size()),buffer.size());
        hal::IqFrame f; f.frameCount=n; engine.onIqFrameReady(f); at+=n; QTest::qWait(1);
    }
}
}
class TestAisEngineLive : public QObject {
    Q_OBJECT
private slots:
    void inputABLossAndEpoch() {
        Monitor monitor; AisReceiver rx(128,4096,32,Monitor::hook,&monitor); rx.start();
        DspEngine engine; engine.attachAisReceiver(&rx); dsp::SpscRing<float> ring(1<<20);
        AisController ui(&rx); engine.setSource(&ring,192000,162000000,false);
        rx.setSource(true,48000,0); ui.setEnabled(true);
        const auto a=ais_test::transmit(ais_test::position(),192000,-25000,0.01,3,false);
        const auto bad=ais_test::transmit(ais_test::position(),192000,-25000,0.01,3,true);
        const auto b=ais_test::transmit(ais_test::position(987654321),192000,25000,0.01,3,false);
        std::vector<std::complex<float>> warm(4096,{0,0}); feed(engine,ring,warm);
        feed(engine,ring,bad); QTest::qWait(30); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(0));
        feed(engine,ring,a); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==1),5000);
        QCOMPARE(ui.mmsi(),QStringLiteral("123456789")); QVERIFY(ui.hasPosition());
        // A static-data report has no position: never present the preceding
        // coordinates as if they belonged to the new message, even for the
        // same MMSI. A fresh position report must restore them.
        std::vector<std::uint8_t> staticData(53,0);
        ais_test::setBits(staticData,0,6,5); ais_test::setBits(staticData,8,30,123456789);
        const auto staticFrame=ais_test::transmit(staticData,192000,-25000,0.01,3,false);
        feed(engine,ring,staticFrame);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),5000);
        QCOMPARE(ui.mmsi(),QStringLiteral("123456789")); QCOMPARE(ui.messageType(),5);
        QVERIFY(!ui.hasPosition());
        feed(engine,ring,a); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==3),5000);
        QVERIFY(ui.hasPosition());
        ui.setChannel(1); engine.setAisFrequency(162025000); feed(engine,ring,warm);
        feed(engine,ring,b); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==4),5000);
        QCOMPARE(ui.mmsi(),QStringLiteral("987654321"));
        // Put a complete valid packet after the first four DSP blocks; all of
        // it predates the loss notification and must remain fenced.
        std::vector<float> backlog(4*16384*2,0);
        for(auto z:b) { backlog.push_back(z.real()); backlog.push_back(z.imag()); }
        QCOMPARE(ring.write(backlog.data(),backlog.size()),backlog.size());
        hal::IqFrame gap; gap.frameCount=backlog.size()/2; gap.droppedFrames=256;
        engine.onIqFrameReady(gap);
        QTRY_VERIFY_WITH_TIMEOUT(ring.available()==0,5000);
        const dsp::Complex barrier{0,0};
        QCOMPARE(rx.submit(&barrier,1),AsyncIqDecoder::SubmitResult::Accepted);
        const auto watermark=rx.stats().accepted;
        QTRY_VERIFY_WITH_TIMEOUT(monitor.calls.load()>=watermark,5000);
        ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(4));
        feed(engine,ring,b); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==5),5000);
        // A new ring epoch, including reset at quiescence, must not inherit
        // the preceding absolute loss watermark.
        engine.clearSource(); ring.reset(1<<20); engine.setSource(&ring,192000,162000000,false);
        rx.setSource(true,48000,0); feed(engine,ring,warm); feed(engine,ring,b);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==6),5000);
        QCOMPARE(ui.mmsi(),QStringLiteral("987654321"));
        engine.clearSource(); rx.setSource(false,48000,0); ui.poll(); QVERIFY(ui.mmsi().isEmpty());
        engine.detachAisTap(); engine.clearAisReceiver(); rx.stop();
    }
};
QTEST_GUILESS_MAIN(TestAisEngineLive)
#include "tst_ais_engine_live.moc"
