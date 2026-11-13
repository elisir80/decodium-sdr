// SPDX-License-Identifier: GPL-3.0-or-later
#include "../standalone/acars/acars_ui_synthetic.h"
#include "core/DspEngine.h"
#include <QTest>
using namespace dsdr;
class TestAcarsEngine : public QObject {
    Q_OBJECT
private slots:
    void liveIqAB() {
        core::AcarsReceiver receiver(1024,1024,32); receiver.start(); receiver.setSource(true,rate,0);
        core::DspEngine engine; dsp::SpscRing<float> ring(32768);
        engine.attachAcarsReceiver(&receiver); engine.setSource(&ring,rate,131550000,false);
        const auto good=generate(), bad=generate(true);
        const auto feedRange=[&](const auto &iq,std::size_t begin,std::size_t end) {
            float data[2048];
            for(std::size_t at=begin;at<end;) {
                const auto n=std::min<std::size_t>(1024,end-at);
                for(std::size_t i=0;i<n;++i) { data[2*i]=iq[at+i].real(); data[2*i+1]=iq[at+i].imag(); }
                QCOMPARE(ring.write(data,n*2),n*2); hal::IqFrame f; f.frameCount=n; engine.onIqFrameReady(f);
                at+=n; QTest::qWait(1);
            }
        };
        const auto feed=[&](const auto &iq) { feedRange(iq,0,iq.size()); };
        core::AcarsReceiver::Message msg; feed(good); QVERIFY(!receiver.read(msg));
        receiver.setEnabled(true); feed(bad); QTest::qWait(100); QVERIFY(!receiver.read(msg));
        feed(good); bool got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.read(msg)),5000);
        const auto expected=payload(); QCOMPARE(msg.size,expected.size()); QVERIFY(std::equal(expected.begin(),expected.end(),msg.bytes.begin()));
        const auto middle=good.size()/2;
        feedRange(good,0,middle); hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        feedRange(good,middle+128,good.size()); QTest::qWait(200); QVERIFY(!receiver.read(msg));
        feed(good); got=false; QTRY_VERIFY_WITH_TIMEOUT((got=got || receiver.read(msg)),5000);
        QCOMPARE(msg.size,expected.size()); QVERIFY(std::equal(expected.begin(),expected.end(),msg.bytes.begin()));
        receiver.setEnabled(false); feed(good); QVERIFY(!receiver.read(msg));
        engine.clearSource(); engine.attachAcarsReceiver(nullptr); receiver.stop();
    }
};
QTEST_GUILESS_MAIN(TestAcarsEngine)
#include "tst_acars_engine.moc"
