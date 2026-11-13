// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated integration rehearsal using the real WAV IQ reader, not DspEngine.
#include "async_fixture.h"
#include "core/PocsagReceiver.h"
#include "core/PocsagController.h"
#include "hal/IqFileReader.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;
using dsdr::core::PocsagReceiver;
using dsdr::core::AsyncIqDecoder;

static void writeWav(const QString &path, unsigned rate, const std::vector<dsdr::dsp::Complex> &iq) {
    QFile f(path); assert(f.open(QIODevice::WriteOnly)); QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian); s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    const quint32 dataSize=static_cast<quint32>(iq.size()*8);
    assert(s.writeRawData("RIFF",4)==4); s << quint32(36+dataSize);
    assert(s.writeRawData("WAVEfmt ",8)==8); s << quint32(16) << quint16(3) << quint16(2) << quint32(rate) << quint32(rate*8) << quint16(8) << quint16(32);
    assert(s.writeRawData("data",4)==4); s << dataSize;
    for(auto v:iq) s << v.real() << v.imag();
    assert(s.status()==QDataStream::Ok); f.close();
}
static void feed(dsdr::hal::iqfile::IqFileReader &file, PocsagReceiver &rx, qint64 end) {
    std::vector<float> interleaved(2*379); std::vector<dsdr::dsp::Complex> block(379);
    while (file.position()<end) {
        const auto want=static_cast<std::size_t>(std::min<qint64>(379,end-file.position()));
        const auto n=file.read(interleaved.data(),want); assert(n==want);
        for(std::size_t i=0;i<n;++i) block[i]={interleaved[2*i],interleaved[2*i+1]};
        assert(rx.submit(block.data(),n)==AsyncIqDecoder::SubmitResult::Accepted);
    }
}
static void verify(const dsdr::core::PocsagController &controller,
                   dsdr::core::PocsagHistoryModel *model, unsigned count) {
    assert(controller.messageCount()==count && model->rowCount()==int(count));
    for(unsigned i=0;i<count;++i) {
        const auto index=model->index(int(i),0);
        assert(model->data(index,dsdr::core::PocsagHistoryModel::AddressRole).toUInt()==1234563);
        assert(model->data(index,dsdr::core::PocsagHistoryModel::FunctionRole).toUInt()==3);
        assert(model->data(index,dsdr::core::PocsagHistoryModel::TextRole).toString()==QStringLiteral("HELLO PAGER"));
    }
}
struct Gate { std::atomic<bool> entered{false}, release{false}; };
static void hold(void *ctx) noexcept {
    auto *g=static_cast<Gate*>(ctx); g->entered=true;
    while(!g->release.load()) std::this_thread::yield();
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir dir(QStringLiteral(DSDR_POCSAG_BINARY_DIR "/pocsag-source-XXXXXX")); assert(dir.isValid());
    PocsagReceiver rx(1024,1024,16); dsdr::core::PocsagController controller(&rx);
    for(unsigned rate:{96000u,48000u}) {
        const unsigned baud=rate==96000 ? 1200 : 2400; const double tune=rate==96000 ? 2000 : -2000;
        auto iq=generate(rate,baud,tune+600,0.05,19);
        assert(expected(decode(iq,rate,baud,tune,257))); // independent synchronous A
        const auto path=dir.filePath(QStringLiteral("source.wav")); writeWav(path,rate,iq);
        dsdr::hal::iqfile::IqFileReader file; assert(file.open(path)); assert(file.info().sampleRate==rate);
        assert(rx.configure(rate,baud,tune)); controller.poll(); assert(controller.messages()->rowCount()==0);
        assert(rx.start()); feed(file,rx,file.info().frameCount); rx.stop(true); controller.poll();
        verify(controller,controller.messages(),1); assert(rx.stats().droppedBlocks==0);
        // B: remove samples in a partially assembled message; it must not be emitted.
        assert(file.seek(0)); assert(rx.start()); const qint64 split=850*rate/baud;
        feed(file,rx,split); assert(file.seek(split+rate/baud*4)); rx.discontinuity();
        feed(file,rx,file.info().frameCount); assert(file.seek(0)); feed(file,rx,file.info().frameCount);
        rx.stop(true); controller.poll(); verify(controller,controller.messages(),1);
    }
    {
        // A blocked worker must not block the Qt event loop while an external
        // owner performs the join. No real shutdown coordinator is claimed.
        Gate gate; PocsagReceiver pending(4,1024,4,hold,&gate);
        assert(pending.configure(48000,2400,0)); assert(pending.start());
        auto iq=generate(48000,2400,600,0,11);
        assert(pending.submit(iq.data(),1024)==AsyncIqDecoder::SubmitResult::Accepted);
        const auto deadline=std::chrono::steady_clock::now()+5s;
        while(!gate.entered.load()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); }
        std::atomic<bool> stopped{false}; std::thread stopper([&]{pending.stop(false); stopped=true;});
        int ticks=0; QTimer timer; QObject::connect(&timer,&QTimer::timeout,[&]{++ticks;}); timer.start(1);
        const auto until=std::chrono::steady_clock::now()+100ms;
        while(std::chrono::steady_clock::now()<until) { QCoreApplication::processEvents(); std::this_thread::sleep_for(1ms); }
        assert(ticks>3 && !stopped.load()); gate.release=true; stopper.join(); assert(stopped.load());
        PocsagReceiver::Message msg; assert(!pending.read(msg));
    }
}
