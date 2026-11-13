// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbController.h"
#include "core/AdsbReceiver.h"
#include "core/DspEngine.h"
#include <QTest>
#include <atomic>
#include <thread>
#include <vector>
using namespace dsdr;
using namespace dsdr::core;
using Frame = dsp::AdsbDecoder::Frame;
namespace {
const Frame known{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x82,0xd6,0x90,0xc8,0xac,0x28,0x63,0xa7};
void fixCrc(Frame &f) {
    unsigned crc=0;
    for(int i=0;i<11;++i) for(int bit=7;bit>=0;--bit) {
        const bool top=((crc>>23)^(f[i]>>bit))&1;
        crc=(crc<<1)&0xffffff; if(top) crc^=0xfff409;
    }
    f[11]=crc>>16; f[12]=crc>>8; f[13]=crc;
}
std::vector<float> waveform(const Frame &f) {
    std::vector<float> s(480, 0.0f);
    for (int i : {0,2,7,9}) s[2*i] = 0.8f;
    for (std::size_t b=0; b<112; ++b)
        s[2*(16+2*b+((f[b/8] >> (7-b%8)) & 1 ? 0 : 1))] = 0.8f;
    return s;
}
void feed(DspEngine &engine, dsp::SpscRing<float> &ring, const std::vector<float> &samples) {
    QCOMPARE(ring.write(samples.data(), samples.size()), samples.size());
    hal::IqFrame frame; frame.frameCount = samples.size()/2;
    engine.onIqFrameReady(frame);
}
}
class TestAdsbIntegration : public QObject {
    Q_OBJECT
private slots:
    void pollingIsBoundedAndDrainsBacklog() {
        struct Counter {
            std::atomic<unsigned> blocks{0};
            static void hit(void *ctx) noexcept { ++static_cast<Counter *>(ctx)->blocks; }
        } counter;
        AdsbReceiver receiver(Counter::hit,&counter); AdsbController ui(&receiver);
        receiver.setSource(true,false,2000000,1090000000); ui.setEnabled(true);
        const auto raw=waveform(known); std::vector<dsp::Complex> samples;
        for(int n=0;n<100;++n)
            for(std::size_t i=0;i<raw.size();i+=2) samples.push_back({raw[i],raw[i+1]});
        for(std::size_t i=0;i<samples.size();i+=4000)
            receiver.process(samples.data()+i,std::min<std::size_t>(4000,samples.size()-i));
        const auto dataBlocks=receiver.iqStats().accepted;
        const dsp::Complex barrier{0,0}; receiver.process(&barrier,1);
        // Do not run Qt's event loop: fill the worker queue before the first
        // explicit UI update. The barrier hook runs after all data blocks.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(counter.blocks.load()<dataBlocks+1 && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        QVERIFY(counter.blocks.load()>=dataBlocks+1);
        QCOMPARE(receiver.iqStats().droppedBlocks,std::uint64_t(0));
        ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(64));
        ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(100));
    }
    void positionSurvivesSameAircraftOnly() {
        DspEngine engine; dsp::SpscRing<float> ring(4096);
        AdsbController ui(engine.adsbReceiver()); ui.setEnabled(true);
        engine.setSource(&ring,2000000,1090000000,false);
        const Frame odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
        feed(engine,ring,waveform(known)); feed(engine,ring,waveform(odd));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.hasPosition()),3000);
        const auto latitude=ui.latitude(), longitude=ui.longitude();
        Frame ident{0x8d,0x40,0x62,0x1d,0x20,0x2c,0xc3,0x71,0xc3,0x2c,0xe0,0,0,0};
        fixCrc(ident); QVERIFY(dsp::AdsbDecoder::validFrame(ident));
        feed(engine,ring,waveform(ident));
        QTRY_COMPARE_WITH_TIMEOUT((ui.poll(),ui.typeCode()),4,3000);
        QCOMPARE(ui.icao(),QStringLiteral("40621D")); QVERIFY(ui.hasPosition());
        QCOMPARE(ui.latitude(),latitude); QCOMPARE(ui.longitude(),longitude);
        ident[3]^=1; fixCrc(ident); QVERIFY(dsp::AdsbDecoder::validFrame(ident));
        feed(engine,ring,waveform(ident));
        QTRY_COMPARE_WITH_TIMEOUT((ui.poll(),ui.icao()),QStringLiteral("40621C"),3000);
        QVERIFY(!ui.hasPosition());
    }
    void expiredPositionIsNotRevivedByIdentification() {
        AdsbReceiver receiver; AdsbController ui(&receiver);
        receiver.setSource(true,false,2000000,1090000000); ui.setEnabled(true);
        const Frame odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
        auto send=[&](const Frame &frame, AdsbCpr::Clock::time_point time) {
            const auto raw=waveform(frame); std::vector<dsp::Complex> iq(raw.size()/2);
            for(std::size_t i=0;i<iq.size();++i) iq[i]={raw[2*i],raw[2*i+1]};
            receiver.process(iq.data(),iq.size(),time);
        };
        const auto old=AdsbCpr::Clock::now()-std::chrono::seconds(11);
        send(known,old); send(odd,old+std::chrono::milliseconds(1));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),3000);
        QVERIFY(!ui.hasPosition());
        Frame ident{0x8d,0x40,0x62,0x1d,0x20,0x2c,0xc3,0x71,0xc3,0x2c,0xe0,0,0,0}; fixCrc(ident);
        send(ident,AdsbCpr::Clock::now());
        QTRY_COMPARE_WITH_TIMEOUT((ui.poll(),ui.frameCount()),qulonglong(3),3000);
        QCOMPARE(ui.icao(),QStringLiteral("40621D")); QVERIFY(!ui.hasPosition());
    }
    void liveAB() {
        DspEngine engine;
        dsp::SpscRing<float> ring(4096);
        AdsbController ui(engine.adsbReceiver());
        engine.setSource(&ring, 2000000, 1090000000, false);
        QVERIFY(!ui.enabled());
        feed(engine, ring, waveform(known)); ui.poll();
        QCOMPARE(ui.frameCount(), qulonglong(0));
        ui.setEnabled(true);
        QCOMPARE(engine.adsbReceiver()->status(), AdsbReceiver::Status::Active);
        feed(engine, ring, waveform(known));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(), ui.frameCount() == qulonglong(1)), 3000);
        QCOMPARE(ui.lastFrame(), QStringLiteral("8D40621D58C382D690C8AC2863A7"));
        QCOMPARE(ui.icao(), QStringLiteral("40621D"));
        QCOMPARE(ui.typeCode(), 11);
        QVERIFY(ui.callsign().isEmpty());
        QVERIFY(ui.hasBarometricAltitude());
        QCOMPARE(ui.barometricAltitudeFeet(), 38000);
        const Frame ident{0x8d,0x48,0x40,0xd6,0x20,0x2c,0xc3,0x71,0xc3,0x2c,0xe0,0x57,0x60,0x98};
        feed(engine, ring, waveform(ident));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(), ui.frameCount() == qulonglong(2)), 3000);
        QCOMPARE(ui.icao(), QStringLiteral("4840D6"));
        QCOMPARE(ui.callsign(), QStringLiteral("KLM1023"));
        QVERIFY(!ui.hasBarometricAltitude());
        auto bad = known; bad[6] ^= 4;
        feed(engine, ring, waveform(bad)); ui.poll();
        QCOMPARE(ui.frameCount(), qulonglong(2));
        engine.clearSource(); ui.poll();
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.icao().isEmpty()),3000);
        QVERIFY(!ui.hasBarometricAltitude());
        dsp::SpscRing<float> replacement(4096);
        engine.setSource(&replacement,2000000,1090000000,false);
        feed(engine,replacement,waveform(known));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.icao()==QStringLiteral("40621D")),3000);
        QCOMPARE(ui.barometricAltitudeFeet(),38000);
        ui.setEnabled(false);
        QVERIFY(ui.icao().isEmpty());
        QVERIFY(ui.callsign().isEmpty());
        feed(engine, replacement, waveform(known)); ui.poll();
        QCOMPARE(ui.frameCount(), qulonglong(0));
    }
    void decoderABAndLimits() {
        AdsbReceiver receiver;
        receiver.setSource(true, false, 2000000, 1090000000);
        receiver.setEnabled(true);
        std::vector<Frame> direct;
        dsp::AdsbDecoder reference([](void *ctx, const Frame &frame) {
            static_cast<std::vector<Frame> *>(ctx)->push_back(frame);
        }, &direct);
        auto raw = waveform(known);
        std::vector<dsp::Complex> iq(raw.size() / 2);
        for (std::size_t i = 0; i < iq.size(); ++i) iq[i] = {raw[i * 2], raw[i * 2 + 1]};
        for (int pass = 0; pass < 12; ++pass) {
            reference.process(iq.data(), 73);
            reference.process(iq.data() + 73, iq.size() - 73);
            receiver.process(iq.data(), 73);
            receiver.process(iq.data() + 73, iq.size() - 73);
        }
        std::vector<Frame> async;
        auto drain = [&] { Frame f; while (receiver.read(f)) async.push_back(f); };
        QTRY_VERIFY_WITH_TIMEOUT((drain(), async.size() == direct.size()), 3000);
        QVERIFY(direct.size() == 12);
        QVERIFY(async == direct);
        QCOMPARE(receiver.iqStats().droppedBlocks, std::uint64_t(0));
        auto before = receiver.generation();
        std::vector<dsp::Complex> oversized(dsp::kMaxBlockFrames + 1);
        receiver.process(oversized.data(), oversized.size());
        QCOMPARE(receiver.generation(), before + 1);
        QCOMPARE(receiver.iqStats().droppedBlocks, std::uint64_t(1));
        QCOMPARE(receiver.iqStats().droppedSamples, std::uint64_t(oversized.size()));
        receiver.process(iq.data(), iq.size());
        QTRY_VERIFY_WITH_TIMEOUT((drain(), async.size() == direct.size() + 1), 3000);
        QVERIFY(async.back() == known);
    }
    void slowWorkerRetuneAndIngressTime() {
        struct Gate {
            std::atomic<bool> entered{false}, release{false};
            static void wait(void *ctx) noexcept {
                auto &gate = *static_cast<Gate *>(ctx);
                gate.entered.store(true, std::memory_order_release);
                while (!gate.release.load(std::memory_order_acquire)) std::this_thread::yield();
            }
        } gate;
        AdsbReceiver receiver(Gate::wait, &gate);
        struct Unblock { Gate &gate; ~Unblock() { gate.release.store(true, std::memory_order_release); } } unblock{gate};
        receiver.setSource(true, false, 2000000, 1090000000);
        receiver.setEnabled(true);
        auto raw = waveform(known);
        std::vector<dsp::Complex> iq(raw.size()/2);
        for (std::size_t i=0; i<iq.size(); ++i) iq[i] = {raw[2*i], raw[2*i+1]};
        const auto oldTime = std::chrono::steady_clock::now() - std::chrono::seconds(20);
        const auto newTime = std::chrono::steady_clock::now() - std::chrono::seconds(2);
        receiver.process(iq.data(), iq.size(), oldTime);
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(std::memory_order_acquire), 3000);
        receiver.process(iq.data(), iq.size(), oldTime);
        // Control operations must return even with the worker held indefinitely.
        receiver.setCenter(1089000000);
        receiver.setCenter(1090000000);
        const auto newGeneration = receiver.generation();
        receiver.process(iq.data(), iq.size(), newTime);
        gate.release.store(true, std::memory_order_release);
        AdsbReceiver::Message message;
        bool received = false;
        QTRY_VERIFY_WITH_TIMEOUT((received || (received = receiver.read(message))), 3000);
        QVERIFY(message.frame == known);
        QCOMPARE(message.generation, newGeneration);
        QVERIFY(message.timestamp == newTime);
        QTest::qWait(30);
        QVERIFY(!receiver.read(message));
    }
    void cprUsesIngressTimes() {
        AdsbReceiver receiver;
        AdsbController ui(&receiver);
        receiver.setSource(true, false, 2000000, 1090000000);
        ui.setEnabled(true);
        const Frame odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
        auto makeIq = [](const Frame &frame) {
            auto raw = waveform(frame);
            std::vector<dsp::Complex> iq(raw.size()/2);
            for (std::size_t i=0; i<iq.size(); ++i) iq[i] = {raw[2*i], raw[2*i+1]};
            return iq;
        };
        auto evenIq = makeIq(known), oddIq = makeIq(odd);
        const auto now = std::chrono::steady_clock::now();
        receiver.process(evenIq.data(), evenIq.size(), now - std::chrono::seconds(20));
        receiver.process(oddIq.data(), oddIq.size(), now);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(), ui.frameCount() == 2), 3000);
        QVERIFY(!ui.hasPosition());
    }
    void queuedCprCannotCrossRetune() {
        struct Gate {
            std::atomic<bool> entered{false}, release{false};
            static void wait(void *ctx) noexcept {
                auto &g=*static_cast<Gate *>(ctx); g.entered.store(true);
                while (!g.release.load()) std::this_thread::yield();
            }
        } gate;
        AdsbReceiver receiver(Gate::wait,&gate);
        struct Unblock { Gate &gate; ~Unblock() { gate.release.store(true); } } unblock{gate};
        AdsbController ui(&receiver);
        receiver.setSource(true,false,2000000,1090000000); ui.setEnabled(true);
        const Frame odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
        auto iq=[](const Frame &f) {
            auto raw=waveform(f); std::vector<dsp::Complex> out(raw.size()/2);
            for(std::size_t i=0;i<out.size();++i) out[i]={raw[2*i],raw[2*i+1]};
            return out;
        };
        const auto evenIq=iq(known), oddIq=iq(odd);
        receiver.process(evenIq.data(),evenIq.size());
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),3000);
        receiver.process(oddIq.data(),oddIq.size());
        receiver.setCenter(1089000000); receiver.setCenter(1090000000);
        receiver.process(oddIq.data(),oddIq.size()); gate.release.store(true);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.icao()==QStringLiteral("40621D")),3000);
        QVERIFY(!ui.hasPosition());
        receiver.process(evenIq.data(),evenIq.size());
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.hasPosition()),3000);
    }
    void invalidSourceAndChanges() {
        DspEngine engine; dsp::SpscRing<float> ring(4096);
        AdsbController ui(engine.adsbReceiver()); ui.setEnabled(true);
        engine.setSource(&ring, 2048000, 1090000000, false);
        QVERIFY(ui.status().contains("2.000.000"));
        QVERIFY(ui.status().contains("2048000"));
        feed(engine, ring, waveform(known)); ui.poll();
        QCOMPARE(ui.frameCount(), qulonglong(0));
        engine.setSource(&ring, 2000000, 1089000000, false);
        QCOMPARE(engine.adsbReceiver()->status(), AdsbReceiver::Status::WrongFrequency);
        feed(engine, ring, waveform(known)); ui.poll();
        QCOMPARE(ui.frameCount(), qulonglong(0));
        engine.setCenterFrequency(1090000000);
        feed(engine, ring, waveform(known));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(), ui.frameCount() == qulonglong(1)), 3000);
        engine.setAudioSource(&ring, 2000000, 1090000000, false);
        QCOMPARE(engine.adsbReceiver()->status(), AdsbReceiver::Status::AudioSource);
        engine.clearSource();
        QCOMPARE(engine.adsbReceiver()->status(), AdsbReceiver::Status::NoSource);
    }
    void cprClearsOnRetune() {
        DspEngine engine; dsp::SpscRing<float> ring(4096);
        AdsbController ui(engine.adsbReceiver()); ui.setEnabled(true);
        const Frame odd{0x8d,0x40,0x62,0x1d,0x58,0xc3,0x86,0x43,0x5c,0xc4,0x12,0x69,0x2a,0xd6};
        engine.setSource(&ring,2000000,1090000000,false);
        feed(engine,ring,waveform(known)); feed(engine,ring,waveform(odd));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.hasPosition()),3000);
        engine.setCenterFrequency(1089000000); ui.poll();
        QVERIFY(!ui.hasPosition()); QVERIFY(ui.icao().isEmpty());
        QCOMPARE(engine.adsbReceiver()->status(),AdsbReceiver::Status::WrongFrequency);
        engine.setCenterFrequency(1090000000); ui.poll();
        QVERIFY(!ui.hasPosition()); QVERIFY(ui.icao().isEmpty());
        feed(engine,ring,waveform(known)); feed(engine,ring,waveform(odd));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.hasPosition()),3000);
        QCOMPARE(ui.icao(),QStringLiteral("40621D"));
        engine.setCenterFrequency(1089000000); ui.poll();
        engine.setCenterFrequency(1090000000); ui.poll();
        feed(engine,ring,waveform(known));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.icao()==QStringLiteral("40621D")),3000);
        QVERIFY(!ui.hasPosition());
        engine.setCenterFrequency(1089000000); ui.poll();
        engine.setCenterFrequency(1090000000); ui.poll();
        feed(engine,ring,waveform(odd));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.icao()==QStringLiteral("40621D")),3000);
        QVERIFY(!ui.hasPosition());
        feed(engine,ring,waveform(known));
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.hasPosition()),3000);
    }
    void discontinuity() {
        DspEngine engine; dsp::SpscRing<float> ring(4096);
        AdsbController ui(engine.adsbReceiver()); ui.setEnabled(true);
        engine.setSource(&ring, 2000000, 1090000000, false);
        auto w = waveform(known);
        feed(engine, ring, std::vector<float>(w.begin(), w.begin()+240));
        engine.adsbReceiver()->discontinuity();
        feed(engine, ring, std::vector<float>(w.begin()+240, w.end()));
        ui.poll(); QCOMPARE(ui.frameCount(), qulonglong(0));
        feed(engine, ring, w); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(), ui.frameCount() == qulonglong(1)), 3000);
    }
    void detachedControllerIsSafe() {
        auto receiver=std::make_unique<AdsbReceiver>();
        AdsbController ui(receiver.get()); ui.setEnabled(true); QVERIFY(ui.enabled());
        ui.detach(); QVERIFY(!ui.enabled()); QVERIFY(ui.status().contains("arresto"));
        receiver.reset();
        ui.poll(); ui.setEnabled(true); QVERIFY(!ui.enabled());
        QVERIFY(ui.lastFrame().isEmpty()); QVERIFY(!ui.hasPosition());
        ui.detach();
    }
};
QTEST_GUILESS_MAIN(TestAdsbIntegration)
#include "tst_adsb_integration.moc"
