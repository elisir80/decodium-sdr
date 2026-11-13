// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DspEngine.h"
#include "core/Vdl2Lifecycle.h"
#include "core/Vdl2ResourcePreparation.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <stdexcept>
#include <thread>
using namespace dsdr::core;
static bool waitFor(const std::function<bool()>& condition,int ms) {
    QElapsedTimer t; t.start();
    while(!condition() && t.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return condition();
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try { Vdl2Lifecycle invalid(nullptr,105000,136975000,0,0.001); return 1; }
    catch(const std::invalid_argument&) {}
    DspEngine engine; QThread dsp; auto* ui=app.thread();
    engine.moveToThread(&dsp); dsp.start();
    int outcome=0, heartbeats=0;
    QTimer heartbeat; heartbeat.setInterval(1); QObject::connect(&heartbeat,&QTimer::timeout,[&]{++heartbeats;}); heartbeat.start();
    Vdl2ResourcePreparation invalidRate(1000,136975000,0,0.001);
    invalidRate.start();
    if(!waitFor([&]{return invalidRate.isFinished();},5000) || invalidRate.error().isEmpty() || invalidRate.takeResources()) outcome=12;
    Vdl2ResourcePreparation prepared(105000,136975000,0,0.001);
    prepared.start();
    if(!waitFor([&]{return prepared.isFinished();},5000) || !prepared.error().isEmpty()) outcome=13;
    auto resources=prepared.takeResources();
    if(!resources) return 14;
    {
        Vdl2Lifecycle lifecycle(&engine,std::move(resources));
        lifecycle.activate();
        if(!waitFor([&]{return lifecycle.state()==QStringLiteral("Attivo");},5000)) outcome=2;
        {
            Vdl2Lifecycle competing(&engine,105000,136975000,0,0.001);
            competing.activate();
            if(!waitFor([&]{return competing.state()==QStringLiteral("Punto IQ occupato");},5000)) outcome=6;
            competing.requestShutdown(); competing.acknowledgeBackendStopped();
            if(!waitFor([&]{return competing.ready();},5000)) outcome=7;
            if(!engine.hasRawIqTap()) outcome=8;
        }
        lifecycle.requestShutdown();
        const int before=heartbeats;
        waitFor([&]{return heartbeats>before+5;},1000);
        if(heartbeats<=before) outcome=3;
        lifecycle.acknowledgeBackendStopped();
        if(!waitFor([&]{return lifecycle.ready();},5000)) outcome=4;
        if(engine.hasRawIqTap()) outcome=9;
    }
    {
        Vdl2Lifecycle idle(&engine,105000,136975000,0,0.001);
        idle.requestShutdown(); idle.acknowledgeBackendStopped();
        if(!waitFor([&]{return idle.ready();},5000)) outcome=5;
    }
    {
        std::atomic<bool> entered{false},release{false};
        QMetaObject::invokeMethod(&engine,[&] { entered=true; while(!release.load()) std::this_thread::yield(); },Qt::QueuedConnection);
        const bool held=waitFor([&]{return entered.load();},5000);
        Vdl2Lifecycle quick(&engine,105000,136975000,0,0.001);
        quick.activate(); quick.requestShutdown(); quick.acknowledgeBackendStopped();
        const int before=heartbeats;
        waitFor([&]{return heartbeats>before+5;},1000);
        if(!held || quick.ready() || engine.hasRawIqTap() || heartbeats<=before) outcome=10;
        release=true;
        if(!waitFor([&]{return quick.ready();},5000) || engine.hasRawIqTap()) outcome=11;
    }
    QMetaObject::invokeMethod(&engine,[&]{engine.moveToThread(ui);},Qt::BlockingQueuedConnection);
    dsp.quit(); dsp.wait();
    return outcome;
}
