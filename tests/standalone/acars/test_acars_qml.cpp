// SPDX-License-Identifier: GPL-3.0-or-later
// Independent synthetic waveform generator shared by both paths in this test.
#ifdef NDEBUG
#error "ACARS QML test requires active assertions"
#endif
#include "acars_ui_synthetic.h"
#include "core/AcarsController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QThread>
#include <QTest>
#include <QDir>
#include <QFile>
#include <memory>

static void writeFixture(const QString &path,const QByteArray &text) {
    QFile f(path); assert(f.open(QIODevice::WriteOnly|QIODevice::Truncate)); assert(f.write(text)==text.size());
}
template<class F> static void waitFor(F predicate,int timeout=7000) {
    QElapsedTimer t; t.start();
    while(!predicate()) { if(t.elapsed()>=timeout) { std::fprintf(stderr,"wait timeout\n"); std::abort(); } QCoreApplication::processEvents(); QThread::msleep(2); }
    QCoreApplication::processEvents();
}
static QQuickItem *findVisual(QQuickItem *item,const char *name) {
    if(item->objectName()==QLatin1String(name)) return item;
    for(auto *child:item->childItems()) if(auto *found=findVisual(child,name)) return found;
    return nullptr;
}
static void click(QQuickWindow *window,QObject *object) {
    auto *item=qobject_cast<QQuickItem*>(object); assert(item && item->isVisible());
    const QPoint p=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,p); QCoreApplication::processEvents();
}
static void submit(dsdr::core::AcarsReceiver &receiver,const std::vector<Complex> &v) {
    const auto generation=receiver.generation();
    for(std::size_t i=0;i<v.size();i+=2048)
        assert(receiver.submitForGeneration(v.data()+i,std::min<std::size_t>(2048,v.size()-i),generation)==dsdr::core::AcarsReceiver::SubmitResult::Accepted);
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv); assert(argc==3);
    std::puts("Based on OnAir by Kristijonas Adomaitis - https://on-air.dev");
    QString fixture=QString::fromLocal8Bit(argv[1]), appQml=QString::fromLocal8Bit(argv[2]);
    assert(QDir().mkpath(fixture+"/DecodiumSdr"));
    writeFixture(fixture+"/DecodiumSdr/qmldir","module DecodiumSdr\nsingleton Theme 1.0 Theme.qml\n");
    writeFixture(fixture+"/DecodiumSdr/Theme.qml",R"(pragma Singleton
import QtQuick
QtObject { readonly property color surfaceRaised: "#222"; readonly property color border: "#888"; readonly property color textPrimary: "white"; readonly property color textSecondary: "silver"; readonly property int radiusSmall: 4 }
)");
    dsdr::core::AcarsReceiver reference(512,4096,32), uiReceiver(512,4096,32);
    dsdr::core::AcarsController controller(&uiReceiver);
    assert(controller.setFrequencyHz(131550000));
    QQmlApplicationEngine engine; engine.addImportPath(fixture); engine.rootContext()->setContextProperty("acarsController",&controller);
    const QString qml=QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow { width: 900; height: 700; visible: true
    App.AcarsDialog { objectName: "dialog"; controller: acarsController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(appQml).toString());
    engine.loadData(qml.toUtf8(),QUrl::fromLocalFile(fixture+"/harness.qml"));
    assert(!engine.rootObjects().isEmpty()); auto *root=engine.rootObjects().first();
    auto *window=qobject_cast<QQuickWindow*>(root); assert(window && window->isVisible());
    auto *dialog=root->findChild<QObject*>("dialog"); assert(dialog); waitFor([&]{return dialog->property("opened").toBool();});
    auto *enable=root->findChild<QObject*>("acarsEnabled"); auto *status=root->findChild<QObject*>("acarsStatus");
    auto *clear=root->findChild<QObject*>("acarsClear"); auto *list=root->findChild<QObject*>("acarsMessages");
    assert(enable && status && clear && list && !controller.enabled());
    auto *frequency=root->findChild<QObject*>("acarsFrequency"); auto *apply=root->findChild<QObject*>("acarsApply");
    assert(frequency && apply && frequency->property("text").toString()==QStringLiteral("131550000"));
    frequency->setProperty("text",QStringLiteral("131725000")); QCoreApplication::processEvents();
    QMetaObject::invokeMethod(apply,"clicked"); assert(controller.frequencyHz()==131725000);
    QMetaObject::invokeMethod(dialog,"close"); assert(controller.setFrequencyHz(131850000));
    QMetaObject::invokeMethod(dialog,"open"); waitFor([&]{return dialog->property("opened").toBool();});
    assert(frequency->property("text").toString()==QStringLiteral("131850000"));
    reference.setSource(true,rate,0); reference.setEnabled(true); reference.start();
    uiReceiver.setSource(true,rate,0); uiReceiver.start();
    click(window,enable); waitFor([&]{return controller.enabled();});
    assert(status->property("text").toString().contains("attiva"));
    const auto valid=generate(); submit(reference,valid); submit(uiReceiver,valid);
    dsdr::core::AcarsReceiver::Message expected; bool have=false;
    waitFor([&]{ if(!have) have=reference.read(expected); controller.poll(); return have && controller.messageCount()==1; });
    auto *model=controller.messages(); assert(model->rowCount()==1); const QModelIndex index=model->index(0,0);
    auto role=[&](int r) { return model->data(index,r).toString(); };
    assert(expected.size==payload().size());
    auto decodedField=[&](std::size_t begin,std::size_t end) {
        QString out; for(auto i=begin;i<end;++i) out+=QChar(expected.bytes[i]&0x7f); return out;
    };
    assert(role(dsdr::core::AcarsHistoryModel::AddressRole)==decodedField(1,8));
    assert(role(dsdr::core::AcarsHistoryModel::LabelRole)==decodedField(9,11));
    assert(role(dsdr::core::AcarsHistoryModel::TextRole)==decodedField(13,expected.size-1));
    assert(role(dsdr::core::AcarsHistoryModel::AddressRole)==".N824UA");
    assert(role(dsdr::core::AcarsHistoryModel::LabelRole)=="H1");
    assert(role(dsdr::core::AcarsHistoryModel::AckRole)=="<15>");
    assert(role(dsdr::core::AcarsHistoryModel::TextRole)=="TEST123");
    assert(role(dsdr::core::AcarsHistoryModel::EndingRole)=="ETX");
    for(std::size_t i=0;i<expected.size;++i) assert(expected.bytes[i]==payload()[i]);
    waitFor([&]{return findVisual(window->contentItem(),"acarsMessageText")!=nullptr;});
    assert(findVisual(window->contentItem(),"acarsMessageText")->property("text").toString()=="TEST123");
    assert(findVisual(window->contentItem(),"acarsMessageText")->property("textFormat").toInt()==0); // PlainText
    assert(findVisual(window->contentItem(),"acarsMessageHeader")->property("text").toString().contains(".N824UA"));
    // An invalid block followed by a valid block proves the worker passed the
    // negative input before the comparison; neither path may count it.
    auto bad=generate(true); submit(reference,bad); submit(reference,valid); submit(uiReceiver,bad); submit(uiReceiver,valid);
    bool second=false; waitFor([&]{if(!second) second=reference.read(expected); controller.poll(); return second && controller.messageCount()==2;});
    assert(model->rowCount()==2); dsdr::core::AcarsReceiver::Message extra; assert(!reference.read(extra));
    uiReceiver.setSource(false,rate,0); controller.poll(); QCoreApplication::processEvents();
    assert(model->rowCount()==0 && status->property("text").toString().contains("non disponibile"));
    uiReceiver.setSource(true,rate,0); controller.poll();
    submit(uiReceiver,bad); submit(uiReceiver,valid);
    waitFor([&]{controller.poll(); return controller.messageCount()==3;}); assert(model->rowCount()==1);
    assert(model->data(model->index(0,0),dsdr::core::AcarsHistoryModel::TextRole).toString()=="TEST123");
    // A source change synchronously triggered by model insertion must clear
    // the old entry and must not increment the lifetime count.
    bool changedDuringInsert=false;
    QMetaObject::Connection duringInsert;
    duringInsert=QObject::connect(model,&QAbstractItemModel::rowsInserted,[&] {
        if(changedDuringInsert) return;
        changedDuringInsert=true; uiReceiver.discontinuity();
    });
    submit(uiReceiver,valid);
    waitFor([&]{controller.poll(); return changedDuringInsert;});
    QObject::disconnect(duringInsert);
    assert(model->rowCount()==0 && controller.messageCount()==3);
    submit(uiReceiver,valid);
    waitFor([&]{controller.poll(); return controller.messageCount()==4;});
    assert(model->rowCount()==1);
    uiReceiver.setSource(true,rate,20000); controller.poll();
    assert(model->rowCount()==0);
    uiReceiver.setSource(true,rate,0); controller.poll();
    submit(uiReceiver,bad); submit(uiReceiver,valid);
    waitFor([&]{controller.poll(); return controller.messageCount()==5;});
    assert(model->rowCount()==1);
    assert(model->data(model->index(0,0),dsdr::core::AcarsHistoryModel::TextRole).toString()=="TEST123");
    click(window,clear); assert(model->rowCount()==0);
    click(window,enable); waitFor([&]{return !controller.enabled();});
    assert(status->property("text").toString().contains("disattivato"));
    assert(controller.messageCount()==5); // clear affects history, not lifetime count
    dsdr::core::AcarsHistoryModel bounded;
    for(int i=0;i<201;++i) { dsdr::core::AcarsHistoryModel::Entry e; e.text=QString::number(i); bounded.append(e); }
    assert(bounded.rowCount()==200);
    assert(bounded.data(bounded.index(0,0),dsdr::core::AcarsHistoryModel::TextRole).toString()=="1");
    assert(bounded.data(bounded.index(199,0),dsdr::core::AcarsHistoryModel::TextRole).toString()=="200");
    {
        auto disposable=std::make_unique<dsdr::core::AcarsReceiver>();
        dsdr::core::AcarsController detached(disposable.get());
        detached.detach(); disposable.reset();
        detached.setEnabled(true); detached.poll();
        assert(!detached.enabled() && detached.status().contains("arresto"));
        assert(detached.droppedMessages()==0 && detached.droppedBlocks()==0 && detached.workerErrors()==0);
    }
    reference.stop(); uiReceiver.stop();
    std::puts("ACARS offscreen UI and receiver A/B: OK");
}
