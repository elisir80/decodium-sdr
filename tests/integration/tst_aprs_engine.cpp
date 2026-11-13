// SPDX-License-Identifier: GPL-3.0-or-later
#define main aprs_standalone_fixture_main
#include "../standalone/tst_aprs_afsk.cpp"
#undef main
#include "core/DspEngine.h"
#include "core/AprsController.h"
#include <QTest>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
using namespace dsdr;
using namespace dsdr::core;
namespace {
std::vector<float> fmIq(const Frame &packet, unsigned rate=192000) {
    const auto audio = waveform(packet, 48000, 0.3, 5);
    const auto symbols = audio.size()*rate/48000;
    std::vector<float> iq; iq.reserve((symbols+rate/10)*2);
    double phase=0;
    auto emitSample = [&](double value) {
        phase += 2*pi*3000.0*value/rate;
        iq.push_back(float(0.7*std::cos(phase))); iq.push_back(float(0.7*std::sin(phase)));
    };
    for (unsigned i=0;i<rate/20;++i) emitSample(0);
    for (std::size_t i=0;i<symbols;++i) emitSample(audio[i*48000/rate]);
    for (unsigned i=0;i<rate/20;++i) emitSample(0);
    return iq;
}
void feed(DspEngine &engine, dsp::SpscRing<float> &ring, const std::vector<float> &iq) {
    for (std::size_t at=0; at<iq.size();) {
        const auto n=std::min<std::size_t>(8192,iq.size()-at);
        QCOMPARE(ring.write(iq.data()+at,n),n);
        hal::IqFrame f; f.frameCount=n/2; engine.onIqFrameReady(f);
        at += n;
        QTest::qWait(1);
    }
}
}
class TestAprsEngine : public QObject {
    Q_OBJECT
private slots:
    void uncompressedPositionAB() {
        auto parse=[](const char *s) { const QString text=QString::fromLatin1(s); return parseAprsPosition(QStringView(text)); };
        // Truncated timestamped packets must be rejected before indexing any
        // timestamp field, including under a Qt debug build.
        for (const char *truncated : {"/", "/1", "/12345", "/123456", "@", "@123456"})
            QVERIFY(!parse(truncated));
        // Every prefix before the complete position must be safe and absent,
        // for both timestamp styles and both position encodings.
        for (const auto &sample : {QStringLiteral("@121234z3554.00S/01430.00W>"),
                                   QStringLiteral("/235959h/5L!!<*e7>7P[")}) {
            for (qsizetype n=0; n<sample.size(); ++n)
                QVERIFY(!parseAprsPosition(QStringView(sample).first(n)));
            QVERIFY(parseAprsPosition(QStringView(sample)));
        }
        const auto north=parse("!3554.00N/01430.00E-Test"); QVERIFY(north);
        QVERIFY(std::abs(north->latitude-35.9)<1e-9); QVERIFY(std::abs(north->longitude-14.5)<1e-9);
        const auto south=parse("@121234z3554.00S/01430.00W-Test"); QVERIFY(south);
        QVERIFY(std::abs(south->latitude+35.9)<1e-9); QVERIFY(std::abs(south->longitude+14.5)<1e-9);
        QVERIFY(parse("/235959h3554.00N/01430.00E-Test"));
        QVERIFY(parse("@312359/3554.00S/01430.00W-Test"));
        const auto object=parse(";TEST     *121234z3554.00N/01430.00E-Test");
        QVERIFY(object); QCOMPARE(object->objectName,QStringLiteral("TEST"));
        QVERIFY(std::abs(object->latitude-35.9)<1e-9);
        const auto item=parse(")WX1!3554.00N/01430.00E-Test");
        QVERIFY(item); QCOMPARE(item->objectName,QStringLiteral("WX1"));
        QVERIFY(std::abs(item->longitude-14.5)<1e-9);
        const auto spacedItem=parse(")WX 1!3554.00N/01430.00E-Test");
        QVERIFY(spacedItem); QCOMPARE(spacedItem->objectName,QStringLiteral("WX 1"));
        // A six-character item name puts the position at offset 8, just as a
        // timestamped station report does; it must not be parsed as a date.
        const auto sixCharItem=parse(")WX1234!3554.00N/01430.00E-Test");
        QVERIFY(sixCharItem); QCOMPARE(sixCharItem->objectName,QStringLiteral("WX1234"));
        const auto sixCharCompressed=parse(")WX1234!/5L!!<*e7>7P[");
        QVERIFY(sixCharCompressed); QCOMPARE(sixCharCompressed->objectName,QStringLiteral("WX1234"));
        const auto compressed=parse("!/5L!!<*e7>7P["); QVERIFY(compressed);
        QVERIFY(std::abs(compressed->latitude-49.5)<0.00001);
        QVERIFY(std::abs(compressed->longitude+72.75)<0.00001);
        const auto numericOverlay=parse("=d5L!!<*e7>7P["); QVERIFY(numericOverlay);
        QVERIFY(std::abs(numericOverlay->latitude-49.5)<0.00001);
        QVERIFY(std::abs(numericOverlay->longitude+72.75)<0.00001);
        QVERIFY(parse("=B5L!!<*e7>7P["));
        QVERIFY(parse("@092345z4903.50N307201.75W>"));
        const auto boundary=parse("!/!!!!!!!!>7P["); QVERIFY(boundary);
        QCOMPARE(boundary->latitude,90.0); QCOMPARE(boundary->longitude,-180.0);
        const auto compressedObject=parse(";TEST     *121234z/5L!!<*e7>7P[");
        QVERIFY(compressedObject); QCOMPARE(compressedObject->objectName,QStringLiteral("TEST"));
        for(const char *bad : {"hello", "!3554.  N/01430.00E-Test", "!3560.00N/01430.00E-Test",
                               "!9000.01N/01430.00E-Test", "!3554.00N/18000.01E-Test",
                               "@xx1234z3554.00S/01430.00W-Test", "!3554.00N/01430.00E",
                               "@002359z3554.00N/01430.00E-Test", "/246000h3554.00N/01430.00E-Test",
                               ";TEST     _121234z3554.00N/01430.00E-Test", ";         *121234z3554.00N/01430.00E-Test",
                               ";TEST     *002359z3554.00N/01430.00E-Test",
                               ")WX1_3554.00N/01430.00E-Test", ")WX!3554.00N/01430.00E-Test",
                               ")ABCDEFGHIJ!3554.00N/01430.00E-Test", "!/5L!!<*e7>7P",
                               "!/5L!!<*e~>7P[", "!/{ {{{{{>7P[",
                               "!k5L!!<*e7>7P[", "!3554.00N?01430.00E-Test",
                               "!/{{{{!!!!>7P[", "!/!!!!{{{{>7P["})
            QVERIFY(!parse(bad));
    }
    void positionClearsForNonPositionPacket() {
        DspEngine engine; dsp::SpscRing<float> ring(32768);
        AprsController ui(engine.aprsReceiver());
        ui.setChannels({QVariantMap{{"id",1},{"label","NFM"}}});
        engine.setSource(&ring,192000,144800000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm; cfg.filterLowHz=-6000;
        cfg.filterHighHz=6000; cfg.squelchEnabled=false; cfg.muted=true;
        engine.addChannel(1,cfg); ui.selectChannel(1);
        engine.aprsReceiver()->setChannelState(1,true,true,48000); ui.setEnabled(true);
        const auto withPosition=frame(); auto withoutPosition=withPosition;
        withoutPosition.resize(23); // three AX.25 addresses, control and PID
        for(char c:std::string(">test without position")) withoutPosition.push_back(c);
        const auto crc=Decoder::fcs(withoutPosition.data(),withoutPosition.size());
        withoutPosition.push_back(crc&255); withoutPosition.push_back(crc>>8);
        QVERIFY(Decoder::validFrame(withoutPosition));
        const auto positionIq=fmIq(withPosition), textIq=fmIq(withoutPosition);
        feed(engine,ring,positionIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==1),5000);
        QVERIFY(ui.hasPosition());
        feed(engine,ring,textIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),5000);
        QVERIFY(!ui.hasPosition()); QCOMPARE(ui.information(),QStringLiteral(">test without position"));
        feed(engine,ring,positionIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==3),5000);
        QVERIFY(ui.hasPosition()); QVERIFY(std::abs(ui.latitude()-35.9)<1e-9);
        auto objectFrame=withPosition; objectFrame.resize(23);
        const std::string objectInfo=";TEST     *121234z3554.00N/01430.00E-Test";
        for(char c:objectInfo) objectFrame.push_back(c);
        const auto objectCrc=Decoder::fcs(objectFrame.data(),objectFrame.size());
        objectFrame.push_back(objectCrc&255); objectFrame.push_back(objectCrc>>8);
        feed(engine,ring,fmIq(objectFrame)); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==4),5000);
        QVERIFY(ui.hasPosition()); QCOMPARE(ui.positionObject(),QStringLiteral("TEST"));
        objectFrame[33]='_'; // object is no longer active
        const auto deadCrc=Decoder::fcs(objectFrame.data(),objectFrame.size()-2);
        objectFrame[objectFrame.size()-2]=deadCrc&255; objectFrame.back()=deadCrc>>8;
        feed(engine,ring,fmIq(objectFrame)); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==5),5000);
        QVERIFY(!ui.hasPosition()); QVERIFY(ui.positionObject().isEmpty());
        auto itemFrame=withPosition; itemFrame.resize(23);
        for(char c:std::string(")WX 1!3554.00N/01430.00E-Test")) itemFrame.push_back(c);
        const auto itemCrc=Decoder::fcs(itemFrame.data(),itemFrame.size());
        itemFrame.push_back(itemCrc&255); itemFrame.push_back(itemCrc>>8);
        feed(engine,ring,fmIq(itemFrame)); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==6),5000);
        QVERIFY(ui.hasPosition()); QCOMPARE(ui.positionObject(),QStringLiteral("WX 1"));
        itemFrame[28]='_';
        const auto removedCrc=Decoder::fcs(itemFrame.data(),itemFrame.size()-2);
        itemFrame[itemFrame.size()-2]=removedCrc&255; itemFrame.back()=removedCrc>>8;
        feed(engine,ring,fmIq(itemFrame)); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==7),5000);
        QVERIFY(!ui.hasPosition()); QVERIFY(ui.positionObject().isEmpty());
        auto compressedFrame=withPosition; compressedFrame.resize(23);
        for(char c:std::string("!/5L!!<*e7>7P[")) compressedFrame.push_back(c);
        const auto compressedCrc=Decoder::fcs(compressedFrame.data(),compressedFrame.size());
        compressedFrame.push_back(compressedCrc&255); compressedFrame.push_back(compressedCrc>>8);
        feed(engine,ring,fmIq(compressedFrame)); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==8),5000);
        QVERIFY(ui.hasPosition()); QVERIFY(std::abs(ui.latitude()-49.5)<0.00001);
        QVERIFY(std::abs(ui.longitude()+72.75)<0.00001);
    }
    void controllerDetachAB() {
        auto rx = std::make_unique<AprsReceiver>();
        AprsController ui(rx.get());
        ui.setChannels({QVariantMap{{"id", 1}, {"label", "NFM"}}});
        ui.selectChannel(1);
        ui.setEnabled(true);
        QVERIFY(ui.enabled());
        QCOMPARE(ui.selectedChannel(), 1);
        ui.detach();
        QVERIFY(!rx->enabled());
        rx.reset(); // the timer and public methods must no longer touch it
        ui.poll();
        ui.setEnabled(true);
        ui.selectChannel(1);
        ui.setChannels({});
        QVERIFY(!ui.enabled());
        QCOMPARE(ui.selectedChannel(), -1);
        QCOMPARE(ui.droppedFrames(), qulonglong(0));
        QCOMPARE(ui.droppedBlocks(), qulonglong(0));
        QVERIFY(!ui.status().isEmpty());
        QTest::qWait(150); // allow the controller's polling timer to fire
    }
    void bufferedLossFenceAB() {
        DspEngine engine;
        dsp::SpscRing<float> ring(1u<<22);
        AprsController ui(engine.aprsReceiver());
        ui.setChannels({QVariantMap{{"id",1},{"label","NFM"}}});
        engine.setSource(&ring,192000,144800000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm;
        cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
        cfg.squelchEnabled=false; cfg.volume=0.7f;
        engine.addChannel(1,cfg);
        ui.selectChannel(1); engine.aprsReceiver()->setChannelState(1,true,true,48000);
        ui.setEnabled(true);
        const auto iq=fmIq(frame());
        // Several packets exceed a single DSP dispatch. Their exact position
        // relative to a reported upstream loss is unknown.
        for (int i=0;i<3;++i) QCOMPARE(ring.write(iq.data(),iq.size()),iq.size());
        hal::IqFrame gap; gap.droppedFrames=128; engine.onIqFrameReady(gap);
        QTRY_COMPARE_WITH_TIMEOUT(ring.available(),std::size_t(0),10000);
        QTest::qWait(200); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(0));
        QVERIFY(engine.audioRing()->available()>0); // main audio still processed
        // A fresh packet written after the fenced backlog remains decodable.
        feed(engine,ring,iq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==1),5000);
        // A new ring starts its own position epoch; the old fence must not
        // suppress valid packets from the reconnected source.
        dsp::SpscRing<float> replacement(1u<<22);
        engine.clearSource();
        engine.setSource(&replacement,192000,144800000,false);
        engine.aprsReceiver()->setChannelState(1,true,true,48000);
        feed(engine,replacement,iq);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),5000);
    }
    void resampledNfmMuteAB() {
        DspEngine engine;
        dsp::SpscRing<float> ring(32768);
        AprsController ui(engine.aprsReceiver());
        ui.setChannels({QVariantMap{{"id",1},{"label","NFM"}}});
        // 200 kHz produces a 50 kHz channel and exercises the 48 kHz resampler.
        engine.setSource(&ring,200000,144800000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm;
        cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
        cfg.squelchEnabled=false; cfg.volume=0.7f;
        engine.addChannel(1,cfg);
        ui.selectChannel(1); engine.aprsReceiver()->setChannelState(1,true,true,48000);
        ui.setEnabled(true);
        const auto iq=fmIq(frame(),200000);
        feed(engine,ring,iq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==1),5000);
        engine.audioRing()->clear(); cfg.muted=true; engine.updateChannel(1,cfg);
        feed(engine,ring,iq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),5000);
        std::vector<float> silent(engine.audioRing()->available());
        QVERIFY(!silent.empty());
        QCOMPARE(engine.audioRing()->read(silent.data(),silent.size()),silent.size());
        QVERIFY(std::all_of(silent.begin(),silent.end(),[](float value) { return value==0.0f; }));
    }
    void shutdownDoesNotWaitForAprsWorker() {
        struct Gate {
            std::atomic<bool> entered{false}, release{false}, exited{false};
            static void hook(void *p) noexcept {
                auto &g=*static_cast<Gate *>(p); g.entered.store(true);
                while (!g.release.load()) std::this_thread::yield();
                g.exited.store(true);
            }
            ~Gate() { release.store(true); while (entered.load() && !exited.load()) std::this_thread::yield(); }
        } gate;
        auto rx=std::make_unique<AprsReceiver>(64,4096,128,Gate::hook,&gate);
        auto engine=std::make_unique<DspEngine>(nullptr,std::move(rx));
        dsp::SpscRing<float> ring(32768);
        engine->setSource(&ring,192000,144800000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm; cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
        engine->addChannel(1,cfg);
        engine->aprsReceiver()->selectChannel(1);
        engine->aprsReceiver()->setChannelState(1,true,true,48000);
        engine->aprsReceiver()->setEnabled(true);
        const auto iq=fmIq(frame());
        feed(*engine,ring,std::vector<float>(iq.begin(),iq.begin()+8192));
        QTRY_VERIFY_WITH_TIMEOUT(gate.entered.load(),3000);
        QElapsedTimer timer; timer.start(); engine.reset();
        const auto elapsed=timer.elapsed();
        gate.release.store(true);
        QTRY_VERIFY_WITH_TIMEOUT(gate.exited.load(),3000);
        QVERIFY2(elapsed < 500, "DspEngine destructor waited for the held APRS worker");
    }
    void nfmEndToEndAB() {
        DspEngine engine;
        dsp::SpscRing<float> ring(32768);
        AprsController ui(engine.aprsReceiver());
        ui.setChannels({QVariantMap{{"id", 1},{"label","NFM 1"}}, QVariantMap{{"id",2},{"label","NFM 2"}}});
        engine.setSource(&ring,192000,144800000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Nfm;
        cfg.filterLowHz=-6000; cfg.filterHighHz=6000;
        cfg.squelchEnabled=false; cfg.volume=0.7f;
        engine.addChannel(1,cfg); engine.addChannel(2,cfg);
        ui.selectChannel(1); engine.aprsReceiver()->setChannelState(1,true,true,48000);
        auto valid=frame(), bad=valid; bad.back()^=1;
        const auto goodIq=fmIq(valid), badIq=fmIq(bad);
        // A: disabled. B: enabled with valid FCS. C: corrupted FCS.
        feed(engine,ring,goodIq); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(0));
        ui.setEnabled(true); feed(engine,ring,goodIq);
        QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==1),5000);
        QCOMPARE(ui.source(),QStringLiteral("IU8LMC-7"));
        QCOMPARE(ui.destination(),QStringLiteral("APRS"));
        QVERIFY(ui.information().startsWith(QStringLiteral("!3554")));
        QVERIFY(ui.hasPosition()); QVERIFY(std::abs(ui.latitude()-35.9)<1e-9);
        QVERIFY(std::abs(ui.longitude()-14.5)<1e-9);
        feed(engine,ring,badIq); QTest::qWait(100); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(1));
        // The other selected channel is deliberately out of band.
        auto other=cfg; other.offsetHz=50000; engine.updateChannel(2,other);
        ui.selectChannel(2); engine.aprsReceiver()->setChannelState(2,true,true,48000);
        feed(engine,ring,goodIq); QTest::qWait(100); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(1));
        ui.selectChannel(1); engine.aprsReceiver()->setChannelState(1,true,true,48000);
        feed(engine,ring,goodIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==2),5000);
        // An upstream drop between packet halves must not form a frame.
        const auto midpoint = (goodIq.size()/4)*2;
        feed(engine,ring,std::vector<float>(goodIq.begin(),goodIq.begin()+midpoint));
        hal::IqFrame gap; gap.droppedFrames=256; engine.onIqFrameReady(gap);
        feed(engine,ring,std::vector<float>(goodIq.begin()+midpoint,goodIq.end()));
        QTest::qWait(100); ui.poll(); QCOMPARE(ui.frameCount(),qulonglong(2));
        feed(engine,ring,goodIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==3),5000);
        // A: ordinary channel gain above. B: muted or zero-volume channel
        // still feeds APRS, while the user's audible channel is silenced.
        other.muted=true; engine.updateChannel(2,other);
        engine.audioRing()->clear();
        auto quiet=cfg; quiet.muted=true; engine.updateChannel(1,quiet);
        feed(engine,ring,goodIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==4),5000);
        std::vector<float> silent(engine.audioRing()->available());
        QVERIFY(!silent.empty());
        QCOMPARE(engine.audioRing()->read(silent.data(),silent.size()),silent.size());
        QVERIFY(std::all_of(silent.begin(),silent.end(),[](float sample) { return sample == 0.0f; }));
        quiet.muted=false; quiet.volume=0; engine.updateChannel(1,quiet);
        feed(engine,ring,goodIq); QTRY_VERIFY_WITH_TIMEOUT((ui.poll(),ui.frameCount()==5),5000);
        silent.resize(engine.audioRing()->available());
        QVERIFY(!silent.empty());
        QCOMPARE(engine.audioRing()->read(silent.data(),silent.size()),silent.size());
        QVERIFY(std::all_of(silent.begin(),silent.end(),[](float sample) { return sample == 0.0f; }));
        // Source disconnect prevents further frames and clears stale UI data.
        engine.clearSource(); engine.aprsReceiver()->setChannelState(1,false,true,48000); ui.poll();
        QVERIFY(ui.source().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAprsEngine)
#include "tst_aprs_engine.moc"
