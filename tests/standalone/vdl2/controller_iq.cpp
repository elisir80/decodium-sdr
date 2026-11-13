// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Vdl2Controller.h"
#include "core/Vdl2Session.h"
#include "core/Vdl2IqIngress.h"
#include "core/DspEngine.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QtQml/qqmlextensionplugin.h>
#include <complex>
#include <cstdio>
#include <vector>
Q_IMPORT_QML_PLUGIN(DecodiumSdrPlugin)
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    if(argc!=2) return 1;
    FILE* f=std::fopen(argv[1],"rb"); if(!f) return 2;
    std::vector<std::complex<float>> iq;
    float pair[2];
    while(std::fread(pair,sizeof(float),2,f)==2) iq.emplace_back(pair[0],pair[1]);
    std::fclose(f);
    if(iq.size()!=450000) return 3;
    std::vector<float> interleaved; interleaved.reserve(iq.size()*2);
    for(const auto& z:iq) { interleaved.push_back(z.real()); interleaved.push_back(z.imag()); }
    dsdr::core::Vdl2Session session(1050000,200000,0.001,16,128);
    dsdr::core::Vdl2IqIngress ingress(session,1050000,136775000);
    dsdr::core::Vdl2Controller controller(&session,nullptr,&ingress);
    dsdr::core::DspEngine dspEngine;
    dsdr::dsp::SpscRing<float> ring(32768);
    dspEngine.setSource(&ring,1050000,136775000,false);
    auto feed=[&]() {
        for(std::size_t at=0;at<iq.size();at+=1024) {
            const auto n=std::min(std::size_t(1024),iq.size()-at);
            if(ring.write(interleaved.data()+2*at,2*n)!=2*n) return false;
            dsdr::hal::IqFrame frame; frame.frameCount=n; dspEngine.onIqFrameReady(frame);
            QThread::msleep(1);
        }
        return true;
    };
    session.start();
    if(!feed()) return 13;
    session.stop(true); controller.poll();
    if(controller.messages()->rowCount()!=0) return 14;
    session.start(); dspEngine.attachRawIqTap(&ingress);
    if(!feed()) return 15;
    if(ingress.status()!=dsdr::core::Vdl2IqIngress::Status::Active) return 18;
    dspEngine.attachRawIqTap(nullptr); session.stop(true);
    controller.poll();
    if(controller.sourceStatus()!=QStringLiteral("Sorgente IQ attiva")) return 22;
    auto* model=controller.messages();
    if(model->rowCount()!=4 || controller.droppedResults() || controller.droppedBlocks() || controller.workerErrors()) return 4;
    bool aircraft=false,ground=false;
    for(int i=0;i<model->rowCount();++i) {
        const auto source=model->data(model->index(i,0),dsdr::core::Vdl2MessageModel::SourceRole).toString();
        aircraft |= source=="4CA2B1"; ground |= source=="10A000";
    }
    if(!aircraft || !ground) return 5;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("testVdl2Controller",&controller);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport DecodiumSdr\nItem { width: 900; height: 700; Vdl2Dialog { objectName: 'vdl2Dialog'; controller: testVdl2Controller } }",QUrl());
    std::unique_ptr<QObject> root(component.create());
    if(!root) { std::fprintf(stderr,"QML: %s\n",qPrintable(component.errorString())); return 7; }
    QQuickWindow window;
    auto* item=qobject_cast<QQuickItem*>(root.get());
    if(!item) return 8;
    item->setParentItem(window.contentItem());
    window.resize(900,700); window.show();
    auto* dialog=root->findChild<QObject*>("vdl2Dialog");
    if(!dialog || !QMetaObject::invokeMethod(dialog,"open")) return 9;
    for(int i=0;i<20;++i) QCoreApplication::processEvents();
    auto* list=dialog->findChild<QObject*>("vdl2Messages");
    if(!list || list->property("count").toInt()!=4) return 10;
    auto* current=list->property("currentItem").value<QObject*>();
    auto* heading=current?current->findChild<QObject*>("vdl2FrameHeading"):nullptr;
    if(!heading || heading->property("text").toString().isEmpty()) return 11;
    dspEngine.attachRawIqTap(&ingress);
    const auto beforeDisconnect=session.generation();
    dspEngine.clearSource();
    if(session.generation()==beforeDisconnect) return 16;
    if(ingress.status()!=dsdr::core::Vdl2IqIngress::Status::Waiting) return 20;
    controller.poll();
    if(controller.sourceStatus()!=QStringLiteral("In attesa della sorgente IQ")) return 23;
    if(model->rowCount()!=0) return 17;
    dspEngine.attachRawIqTap(nullptr);
    session.start(); controller.poll(); session.stop(true);
    if(model->rowCount()!=0) return 6;
    session.start();
    const auto staleSource=session.sourceGeneration();
    session.invalidateResults();
    session.processForGeneration(iq.data(),iq.size(),staleSource);
    session.stop(true); controller.poll();
    if(model->rowCount()!=0) return 21;
    session.start();
    ingress.process(interleaved.data(),145000,1050000,136775000);
    ingress.process(interleaved.data()+290000,iq.size()-145000,1050000,136775001);
    if(ingress.status()!=dsdr::core::Vdl2IqIngress::Status::WrongCenter) return 19;
    session.stop(true); controller.poll();
    if(model->rowCount()!=0) return 12;
    controller.detach();
    std::puts("DspEngine IQ A/B: disabled empty, enabled four frames in QML; disconnect, restart and retune fenced stale data");
    return 0;
}
