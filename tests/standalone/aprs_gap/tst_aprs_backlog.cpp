// SPDX-License-Identifier: GPL-3.0-or-later
// Real DspEngine: same queued IQ with/without an upstream loss notification.
#define main aprs_test_fixture_main
#include "../tst_aprs_afsk.cpp"
#undef main
#include "core/AprsController.h"
#include "core/DspEngine.h"
#include <QElapsedTimer>
#include <QTest>
#include <atomic>
#include <cmath>
#include <vector>
using namespace dsdr;
using namespace dsdr::core;
namespace {
struct Counter {
    std::atomic<std::uint64_t> calls{0};
    static void hit(void *p) noexcept { ++static_cast<Counter *>(p)->calls; }
};
std::vector<float> packetIq() {
    const auto audio=waveform(frame(),48000,0.3,5);
    std::vector<float> iq;
    double phase=0;
    auto sample=[&](double x) {
        phase+=2*pi*3000*x/192000;
        iq.push_back(float(0.7*std::cos(phase))); iq.push_back(float(0.7*std::sin(phase)));
    };
    for(int i=0;i<9600;++i) sample(0);
    for(float x:audio) for(int i=0;i<4;++i) sample(x);
    for(int i=0;i<9600;++i) sample(0);
    return iq;
}
struct Run {
    std::vector<float> audio;
    qulonglong oldFrames=0, finalFrames=0;
};
Run run(bool loss) {
    Counter counter;
    auto receiver=std::make_unique<AprsReceiver>(256,4096,128,Counter::hit,&counter);
    DspEngine engine(nullptr,std::move(receiver));
    dsp::SpscRing<float> ring(1<<20);
    AprsController ui(engine.aprsReceiver());
    ui.setChannels({QVariantMap{{"id",1},{"label","NFM"}}});
    engine.setSource(&ring,192000,144800000,false);
    dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm; cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
    cfg.volume=0.7f; cfg.squelchEnabled=false;
    engine.addChannel(1,cfg);
    ui.selectChannel(1); engine.aprsReceiver()->setChannelState(1,true,true,48000); ui.setEnabled(true);
    const auto packet=packetIq();
    std::vector<float> backlog;
    // DspEngine processes at most four 16384-IQ-frame blocks per dispatch;
    // put the valid packet strictly after that first dispatch.
    for(std::size_t i=0;i<4*16384;++i) { backlog.push_back(0.7f); backlog.push_back(0); }
    backlog.insert(backlog.end(),packet.begin(),packet.end());
    Q_ASSERT(backlog.size()<ring.capacity());
    if(ring.write(backlog.data(),backlog.size())!=backlog.size()) qFatal("input ring capacity");
    hal::IqFrame notification; notification.frameCount=backlog.size()/2;
    notification.droppedFrames=loss ? 256 : 0;
    engine.onIqFrameReady(notification);
    Run result;
    auto drainAudio=[&] {
        auto *out=engine.audioRing(); const auto n=out->available();
        const auto start=result.audio.size(); result.audio.resize(start+n);
        if(n && out->read(result.audio.data()+start,n)!=n) qFatal("audio ring changed unexpectedly");
    };
    QElapsedTimer timer; timer.start();
    while(ring.available()>0 && timer.elapsed()<10000) { drainAudio(); QCoreApplication::processEvents(); QTest::qWait(1); }
    if(ring.available()!=0) qFatal("IQ backlog did not drain");
    drainAudio();
    // Worker fence: the last queued callback is known to have entered after
    // every previous accepted callback. Only the test producer uses submit.
    const dsp::Complex barrier{0,0};
    if(engine.aprsReceiver()->submit(1,&barrier,1)!=AsyncIqDecoder::SubmitResult::Accepted) qFatal("APRS barrier rejected");
    const auto watermark=engine.aprsReceiver()->stats().accepted;
    timer.restart(); while(counter.calls.load()<watermark && timer.elapsed()<5000) QTest::qWait(1);
    if(counter.calls.load()<watermark) qFatal("APRS worker timeout");
    ui.poll(); result.oldFrames=ui.frameCount();
    if(loss) {
        // A complete frame arriving strictly after the fence must be accepted.
        for(std::size_t at=0;at<packet.size();) {
            auto n=std::min<std::size_t>(8192,packet.size()-at);
            if(ring.write(packet.data()+at,n)!=n) qFatal("recovery input ring full");
            hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f); at+=n; QTest::qWait(1);
        }
        const auto expected=result.oldFrames+1;
        timer.restart();
        while(ui.frameCount()<expected && timer.elapsed()<5000) { ui.poll(); QTest::qWait(1); }
    }
    result.finalFrames=ui.frameCount();
    return result;
}
}
class TestAprsBacklog : public QObject {
    Q_OBJECT
private slots:
    void lossFencePreservesAudioAndRecovers() {
        const auto control=run(false), loss=run(true);
        QCOMPARE(control.oldFrames,qulonglong(1));
        QCOMPARE(loss.finalFrames,loss.oldFrames+1);
        QCOMPARE(control.audio.size(),loss.audio.size());
        QVERIFY(!control.audio.empty());
        float peak=0,maxDiff=0;
        for(std::size_t i=0;i<control.audio.size();++i) {
            peak=std::max(peak,std::abs(control.audio[i]));
            maxDiff=std::max(maxDiff,std::abs(control.audio[i]-loss.audio[i]));
        }
        QVERIFY2(peak>0.001f,"audio must carry demodulated signal");
        QVERIFY2(maxDiff<1e-5f,"loss fence changed the audio pipeline");
        QCOMPARE(loss.oldFrames,qulonglong(0));
    }
};
QTEST_GUILESS_MAIN(TestAprsBacklog)
#include "tst_aprs_backlog.moc"
