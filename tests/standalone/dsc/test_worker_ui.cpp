#define DSC_AUDIO_NO_MAIN
#include "test_audio_ab.cpp"
#undef DSC_AUDIO_NO_MAIN
#include "AsyncDsc.h"
#include "DscController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QThread>
#include <QEventLoop>
#include <QVariantMap>

static void feed(dsc_rx::AsyncDsc& rx, const std::vector<float>& audio, size_t begin=0, size_t end=SIZE_MAX) {
    end=std::min(end,audio.size());
    for(size_t at=begin;at<end;) { auto len=std::min<size_t>(1024,end-at); require(rx.submit(audio.data()+at,len)==dsc_rx::AsyncDsc::Submit::Accepted,"worker submit"); at+=len; }
}
static QQuickItem* findVisual(QQuickItem* root,const QString& name) {
    if(root->objectName()==name) return root;
    for(auto child:root->childItems()) if(auto* p=findVisual(child,name)) return p;
    return nullptr;
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    std::cout << "Based on OnAir by Kristijonas Adomaitis - https://on-air.dev\n";
    const std::array<int,15> body = {112,23,21,23,45,60,103,15,5,10,1,18,12,34,109};
    auto audio=modulate(fixture(body),100,0,.02,.73);
    auto baseline=evaluate(audio,137);
    require(baseline.theirs.size()==1 && baseline.theirs[0].fromMmsi=="232123456", "OnAir A baseline");
    dsc_rx::AsyncDsc rx(1024,1024,2);
    dsc_rx::Controller controller(&rx);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dscController",&controller);
    auto source=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as Dsc
ApplicationWindow { width: 800; height: 600; visible: true
 Dsc.DscDialog { controller: dscController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSC_SOURCE_DIR)).toString());
    engine.loadData(source.toUtf8());
    require(!engine.rootObjects().isEmpty(),"QML loaded");
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window && window->isVisible(),"offscreen window visible");
    auto* checkbox=window->findChild<QObject*>("dscEnabled");
    require(checkbox,"checkbox exists");
    checkbox->setProperty("checked",true); QMetaObject::invokeMethod(checkbox,"toggled");
    require(controller.enabled() && !rx.active() && controller.status().contains("attesa"),"enabled awaits source");
    require(!rx.configure({48000,1700,"source-a"}),"reject unsupported rate");
    controller.configurationChanged(); require(!rx.active(),"unsupported configuration stopped");
    require(rx.configure({24000,1700,"source-a"}),"configure"); controller.configurationChanged(); require(rx.active(),"controller starts receiver");
    feed(rx,audio); rx.stop(true); controller.poll();
    auto rows=controller.messages(); require(rows.size()==1,"worker B one result");
    auto row=rows[0].toMap();
    require(row["mmsi"].toString()=="232123456" && row["nature"].toInt()==103 && row["positionRaw"].toString()=="1505100118" && row["utcRaw"].toString()=="1234", "decoded fields");
    QCoreApplication::processEvents();
    auto* view=window->findChild<QObject*>("dscMessages"); require(view && view->property("count").toInt()==1,"dialog list populated");
    auto* line=findVisual(window->contentItem(),"dscMessage");
    require(line && line->property("text").toString().contains("232123456"),"visible message");
    require(rx.configure({24000,1700,"source-b"}),"source change"); controller.configurationChanged();
    require(controller.messages().empty() && rx.active(),"change clears results and restarts");
    const size_t middle=audio.size()/2;
    feed(rx,audio,0,middle); rx.gap(); feed(rx,audio,middle); feed(rx,audio);
    rx.stop(true); controller.poll(); require(controller.messages().size()==1,"gap suppresses interrupted call and next succeeds");
    require(rx.configure({24000,1700,"drop-gap"}),"configure drop test"); controller.configurationChanged();
    feed(rx,audio,0,middle);
    require(rx.submit(audio.data(),1025)==dsc_rx::AsyncDsc::Submit::TooLarge,"dropped block");
    feed(rx,audio,middle); feed(rx,audio); rx.stop(true); controller.poll();
    require(controller.messages().size()==1,"dropped input resets decoder before continuation");
    require(rx.configure({24000,1700,"source-c"}),"reconfigure again"); controller.configurationChanged();
    require(rx.submit(audio.data(),1025)==dsc_rx::AsyncDsc::Submit::TooLarge,"oversized block counted");
    feed(rx,audio); feed(rx,audio); feed(rx,audio);
    rx.stop(true); controller.poll();
    require(controller.messages().size()==2 && rx.stats().droppedResults==1 && rx.stats().droppedBlocks>=1,"bounded result queue and counters");
    controller.setEnabled(false); require(!rx.active(),"disable");
    {
        dsc_rx::AsyncDsc bounded(1,1024,1);
        require(bounded.configure({24000,1700,"overflow"}) && bounded.start(),"overflow setup");
        std::vector<float> block(1024,0.f); int full=0;
        for(int i=0;i<10000;++i) if(bounded.submit(block.data(),block.size())==dsc_rx::AsyncDsc::Submit::Full) ++full;
        bounded.stop(true);
        require(full>0 && bounded.stats().droppedBlocks==static_cast<uint64_t>(full) && bounded.stats().droppedSamples==static_cast<uint64_t>(full)*1024, "input queue saturation accounting");
    }
    std::cout<<"PASS worker / controller / QML / OnAir A/B\n";
}
