// SPDX-License-Identifier: GPL-3.0-or-later
// Real DspEngine -> existing IQ module ABI -> AIS worker. The module is a test
// fixture, not the final integration or a proof of production gap/shutdown.
#include "core/DspEngine.h"
#include "ais_engine_probe.h"
#include "ais_synthetic.h"
#include <QLibrary>
#include <QTest>
using namespace dsdr;
using namespace dsdr::core;
namespace {
void feed(DspEngine &engine,dsp::SpscRing<float> &ring,const std::vector<std::complex<float>> &iq) {
    std::vector<float> block;
    for(std::size_t at=0;at<iq.size();) {
        const auto n=std::min<std::size_t>(4096,iq.size()-at); block.resize(n*2);
        for(std::size_t i=0;i<n;++i) { block[2*i]=iq[at+i].real(); block[2*i+1]=iq[at+i].imag(); }
        QCOMPARE(ring.write(block.data(),block.size()),block.size());
        hal::IqFrame f; f.frameCount=n; engine.onIqFrameReady(f); at+=n; QTest::qWait(1);
    }
}
}
class TestAisRealEngine : public QObject {
    Q_OBJECT
private slots:
    void filteredIqModuleAB() {
        const QString path=QStringLiteral(DSDR_AIS_PROBE_PATH);
        QLibrary lib(path); QVERIFY2(lib.load(),qPrintable(lib.errorString()));
        auto enable=reinterpret_cast<void (*)(bool)>(lib.resolve("dsdr_ais_probe_enable"));
        auto read=reinterpret_cast<bool (*)(AisProbeMessage *)>(lib.resolve("dsdr_ais_probe_read"));
        auto workerCalls=reinterpret_cast<std::uint64_t (*)()>(lib.resolve("dsdr_ais_probe_worker_calls"));
        auto accepted=reinterpret_cast<std::uint64_t (*)()>(lib.resolve("dsdr_ais_probe_accepted"));
        auto fence=reinterpret_cast<bool (*)()>(lib.resolve("dsdr_ais_probe_fence"));
        auto calls=reinterpret_cast<std::uint64_t (*)()>(lib.resolve("dsdr_ais_probe_calls"));
        auto frames=reinterpret_cast<std::uint64_t (*)()>(lib.resolve("dsdr_ais_probe_frames"));
        auto rate=reinterpret_cast<double (*)()>(lib.resolve("dsdr_ais_probe_rate"));
        QVERIFY(enable); QVERIFY(read); QVERIFY(calls); QVERIFY(frames); QVERIFY(rate);
        QVERIFY(workerCalls); QVERIFY(accepted); QVERIFY(fence);
        DspEngine engine; dsp::SpscRing<float> ring(32768);
        QVERIFY2(engine.loadIqModule(path),qPrintable(engine.lastIqModuleError()));
        engine.setSource(&ring,192000,162000000,false);
        dsp::ChannelSettings cfg; cfg.mode=DemodMode::Iq; cfg.offsetHz=25000;
        engine.addChannel(1,cfg);
        auto target=ais_test::transmit(ais_test::position(),192000,25000,0.01,3,false);
        auto bad=ais_test::transmit(ais_test::position(),192000,25000,0.01,3,true);
        auto other=ais_test::transmit(ais_test::position(987654321),192000,-25000,0.01,3,false);
        QCOMPARE(target.size(),other.size()); QCOMPARE(bad.size(),other.size());
        // Drain the engine's FIR delay; a real stream continues after a burst.
        target.insert(target.end(),19200,{}); bad.insert(bad.end(),19200,{}); other.insert(other.end(),19200,{});
        // Independent packets arrive simultaneously on the two AIS channels.
        for(std::size_t i=0;i<target.size();++i) { target[i]+=other[i]; bad[i]+=other[i]; }
        AisProbeMessage message{};
        // Disabled and invalid CRC are negative cases, then decode target.
        feed(engine,ring,target); QTest::qWait(20); QVERIFY(!read(&message));
        enable(true); feed(engine,ring,bad);
        QVERIFY(fence()); const auto watermark=accepted();
        QTRY_VERIFY_WITH_TIMEOUT(workerCalls()>=watermark,3000);
        QVERIFY(!read(&message));
        feed(engine,ring,target);
        qInfo() << "AIS probe calls" << calls() << "frames" << frames() << "rate" << rate();
        bool received=false;
        QTRY_VERIFY_WITH_TIMEOUT((received || (received=read(&message))),3000);
        QCOMPARE(message.type,1u); QCOMPARE(message.mmsi,std::uint32_t(123456789));
        QVERIFY(message.position); QVERIFY(qAbs(message.latitude-35.9)<1e-6);
        QVERIFY(qAbs(message.longitude-14.5)<1e-6);
        QTest::qWait(30); QVERIFY(!read(&message));
        // Engine's channel DDC/filter selects the other simultaneous AIS burst.
        cfg.offsetHz=-25000; engine.updateChannel(1,cfg);
        feed(engine,ring,target); received=false;
        QTRY_VERIFY_WITH_TIMEOUT((received || (received=read(&message))),5000);
        QCOMPARE(message.mmsi,std::uint32_t(987654321));
        enable(false); feed(engine,ring,target); QTest::qWait(30); QVERIFY(!read(&message));
        QVERIFY(engine.unloadIqModule(path));
    }
};
QTEST_GUILESS_MAIN(TestAisRealEngine)
#include "tst_ais_real_engine.moc"
