// SPDX-License-Identifier: GPL-3.0-or-later
#include "async_fixture.h"
#include "core/PocsagReceiver.h"
#include "core/PocsagController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
static QQuickItem *visualFind(QQuickItem *item, const QString &name) {
    if (item->objectName()==name) return item;
    for (auto *child : item->childItems()) if (auto *found=visualFind(child,name)) return found;
    return nullptr;
}
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    dsdr::core::PocsagReceiver rx(512,1024,4);
    dsdr::core::PocsagController controller(&rx);
    assert(controller.configureRequested(512,144800000));
    bool requested = false, configured = false;
    QObject::connect(&controller, &dsdr::core::PocsagController::configurationRequested,
                     [&](int baud, qint64 hz) { configured = baud == 2400 && hz == 145000000; });
    QObject::connect(&controller, &dsdr::core::PocsagController::enabledRequested,
                     [&](bool enabled) { requested=enabled; });
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(DSDR_POCSAG_SOURCE_DIR "/tests/standalone/pocsag/qml"));
    engine.rootContext()->setContextProperty("pocsagController", &controller);
    const auto qml = QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import "%1" as App
ApplicationWindow {
    width: 800; height: 600; visible: true
    App.PocsagDialog { id: dialog; objectName: "dialog"; controller: pocsagController; Component.onCompleted: open() }
})").arg(QUrl::fromLocalFile(QStringLiteral(DSDR_POCSAG_SOURCE_DIR "/src/app/qml")).toString());
    engine.loadData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(DSDR_POCSAG_BINARY_DIR "/pocsag-harness.qml")));
    assert(!engine.rootObjects().isEmpty());
    auto *root=engine.rootObjects().first();
    QTimer::singleShot(250, [&] {
        auto *dialog=root->findChild<QObject *>("dialog"); assert(dialog && dialog->property("opened").toBool());
        assert(qobject_cast<QQuickWindow *>(root)->isVisible());
        auto *checkbox=root->findChild<QObject *>("pocsagEnabled"); assert(checkbox);
        checkbox->setProperty("checked",true); QMetaObject::invokeMethod(checkbox,"toggled");
        assert(requested && controller.enabled() && !controller.active());
        assert(!controller.configureRequested(9600,145000000));
        assert(!controller.configureRequested(1200,0));
        auto *baudPicker=root->findChild<QObject *>("pocsagBaud");
        auto *frequencyInput=root->findChild<QObject *>("pocsagFrequency");
        auto *apply=root->findChild<QObject *>("pocsagApply");
        assert(baudPicker && frequencyInput && apply);
        auto *help=root->findChild<QObject *>("pocsagTuningHelp");
        assert(help && help->property("text").toString().contains("non cambia la sintonia"));
        assert(baudPicker->property("currentValue").toInt()==512);
        assert(frequencyInput->property("text").toString()==QStringLiteral("144800000"));
        baudPicker->setProperty("currentIndex",2); frequencyInput->setProperty("text",QStringLiteral("145000000"));
        QCoreApplication::processEvents(); assert(apply->property("enabled").toBool());
        QMetaObject::invokeMethod(apply,"clicked");
        assert(configured && controller.baud()==2400 && controller.frequencyHz()==145000000);
        QMetaObject::invokeMethod(dialog,"close");
        assert(controller.configureRequested(512,144800000));
        QMetaObject::invokeMethod(dialog,"open"); QCoreApplication::processEvents();
        assert(baudPicker->property("currentValue").toInt()==512);
        assert(frequencyInput->property("text").toString()==QStringLiteral("144800000"));
        assert(controller.status().contains("attesa"));
        assert(rx.configure(96000,1200,2000)); assert(rx.start()); controller.setActive(true);
        auto iq=generate(96000,1200,2600,0,7);
        for(std::size_t i=0;i<iq.size();) {auto n=std::min<std::size_t>(1024,iq.size()-i); assert(rx.submit(iq.data()+i,n)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted); i+=n;}
        rx.stop(true); controller.poll(); assert(controller.messageCount()==1);
        auto verifyMessage = [&] {
            const auto *model = controller.messages(); assert(model->rowCount()==1);
            const auto index = model->index(0,0);
            assert(model->data(index, dsdr::core::PocsagHistoryModel::AddressRole).toUInt()==1234563);
            assert(model->data(index, dsdr::core::PocsagHistoryModel::FunctionRole).toUInt()==3);
            assert(model->data(index, dsdr::core::PocsagHistoryModel::TextRole).toString()==QStringLiteral("HELLO PAGER"));
            assert(model->data(index, dsdr::core::PocsagHistoryModel::CorrectedBitsRole).toUInt()==0);
        };
        verifyMessage();
        QCoreApplication::processEvents();
        auto *counts=root->findChild<QObject *>("pocsagCounts"); assert(counts && counts->property("text").toString().contains("1"));
        auto *view=root->findChild<QObject *>("pocsagMessages"); assert(view && view->property("count").toInt()==1);
        auto *line=visualFind(qobject_cast<QQuickWindow *>(root)->contentItem(), "pocsagMessageText"); assert(line && line->property("text").toString()==QStringLiteral("HELLO PAGER"));
        auto *header=visualFind(qobject_cast<QQuickWindow *>(root)->contentItem(), "pocsagMessageHeader"); assert(header && header->property("text").toString().contains("1234563") && header->property("text").toString().contains("Funzione: 3"));
        // A new rate, baud, and channel clears old results, then populates the UI again.
        controller.setActive(false);
        assert(rx.configure(48000,2400,-2000)); controller.poll();
        assert(controller.messages()->rowCount()==0 && controller.messageCount()==0);
        assert(rx.start()); controller.setActive(true);
        iq=generate(48000,2400,-1400,0.05,12);
        for(std::size_t i=0;i<iq.size();) {auto n=std::min<std::size_t>(1024,iq.size()-i); assert(rx.submit(iq.data()+i,n)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted); i+=n;}
        rx.stop(true); controller.poll(); assert(controller.messageCount()==1); verifyMessage();
        QCoreApplication::processEvents(); assert(view->property("count").toInt()==1);
        controller.setActive(false); controller.setEnabled(false); assert(!requested);
        {
            dsdr::core::PocsagHistoryModel model;
            std::vector<dsdr::core::PocsagHistoryModel::Entry> rows;
            for (unsigned i=0;i<600;++i) rows.push_back({i,i%4,QString::number(i),0});
            for (unsigned i=0;i<600;i+=20) model.appendBatch({rows.begin()+i, rows.begin()+i+20});
            assert(model.rowCount()==500);
            for (int i=0;i<500;++i) {
                const auto idx=model.index(i,0);
                assert(model.data(idx,dsdr::core::PocsagHistoryModel::AddressRole).toUInt()==unsigned(i+100));
                assert(model.data(idx,dsdr::core::PocsagHistoryModel::TextRole).toString()==QString::number(i+100));
            }
            model.clear(); assert(model.rowCount()==0);
            model.appendBatch(rows); assert(model.rowCount()==500);
            assert(model.data(model.index(0,0),dsdr::core::PocsagHistoryModel::AddressRole).toUInt()==100);
        }
        {
            dsdr::core::PocsagReceiver batchRx(2048,1024,64);
            dsdr::core::PocsagController batchController(&batchRx);
            assert(batchRx.configure(48000,2400,0)); assert(batchRx.start());
            const auto burst=generate(48000,2400,600,0,7);
            for (int repeat=0;repeat<40;++repeat) for(std::size_t i=0;i<burst.size();) {
                auto n=std::min<std::size_t>(1024,burst.size()-i);
                assert(batchRx.submit(burst.data()+i,n)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted); i+=n;
            }
            batchRx.stop(true); assert(batchRx.droppedMessages()==0);
            batchController.poll(); assert(batchController.messageCount()==32 && batchController.messages()->rowCount()==32);
            batchController.poll(); assert(batchController.messageCount()==40 && batchController.messages()->rowCount()==40);
        }
        {
            dsdr::core::PocsagReceiver changing(512,1024,8);
            dsdr::core::PocsagController modelController(&changing);
            assert(changing.configure(48000,2400,0)); assert(changing.start());
            const auto burst=generate(48000,2400,600,0,7);
            auto send=[&] { for(std::size_t i=0;i<burst.size();) {
                auto n=std::min<std::size_t>(1024,burst.size()-i);
                assert(changing.submit(burst.data()+i,n)==dsdr::core::AsyncIqDecoder::SubmitResult::Accepted); i+=n;
            }};
            send(); changing.stop(true);
            bool changed=false;
            auto connection=QObject::connect(modelController.messages(),&QAbstractItemModel::rowsInserted,[&] {
                if(!changed) { changed=true; changing.invalidateResults(); }
            });
            modelController.poll(); QObject::disconnect(connection);
            assert(changed && modelController.messageCount()==0 && modelController.messages()->rowCount()==0);
            assert(changing.configure(48000,2400,0)); assert(changing.start());
            send(); changing.stop(true); modelController.poll();
            assert(modelController.messageCount()==1 && modelController.messages()->rowCount()==1);
        }
        {
            auto detachedRx=std::make_unique<dsdr::core::PocsagReceiver>();
            dsdr::core::PocsagController detached(detachedRx.get());
            detached.setEnabled(true); detached.setActive(true);
            detached.detach(); detachedRx.reset();
            detached.poll(); detached.setEnabled(true); detached.setActive(true);
            assert(!detached.enabled() && !detached.active());
            assert(!detached.configureRequested(1200,100000000));
            assert(detached.droppedBlocks()==0 && detached.droppedMessages()==0);
        }
        app.quit();
    });
    return app.exec();
}
