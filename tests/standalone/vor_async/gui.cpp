// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/VorAsyncReceiver.h"
#include "core/VorController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <cstdlib>
#include <iostream>
void check(bool b,const char* what) { if(!b) { std::cerr<<"FAIL "<<what<<'\n'; std::exit(1); } }
int main(int argc,char** argv) {
 QGuiApplication app(argc,argv); dsdr::core::VorAsyncReceiver rx; dsdr::core::VorController controller(&rx);
 QQmlApplicationEngine engine; engine.addImportPath(QStringLiteral(VOR_TEST_QML));
 engine.rootContext()->setContextProperty("vorController",&controller);
 engine.loadData(R"(import QtQuick
import QtQuick.Controls.Basic
import "../../../src/app/qml" as App
ApplicationWindow { width: 800; height: 600; visible: true
 App.VorDialog { objectName: "dialog"; controller: vorController; Component.onCompleted: open() }
})",QUrl::fromLocalFile(QStringLiteral(VOR_TEST_BASE) + "/harness.qml"));
 check(!engine.rootObjects().isEmpty(),"QML load"); auto* root=engine.rootObjects().first();
 QTimer::singleShot(300,[&] {
  auto* dialog=root->findChild<QObject*>("dialog"); check(dialog && dialog->property("opened").toBool(),"dialog open");
  auto* warning=root->findChild<QObject*>("vorWarning"); auto* frequency=root->findChild<QObject*>("vorFrequency");
  auto* label=root->findChild<QObject*>("vorRadial"); auto* enabled=root->findChild<QObject*>("vorEnabled");
  check(warning && warning->property("text").toString().contains("non usare"),"warning");
  check(frequency && frequency->property("text").toString().isEmpty(),"frequency initially empty");
  check(enabled && !enabled->property("checked").toBool(),"disabled default");
  check(controller.setFrequencyHz(110007000),"frequency accepted"); controller.setEnabled(true); QCoreApplication::processEvents();
  check(enabled->property("checked").toBool(),"controller binding");
  check(label && label->property("text").toString().contains("non disponibile"),"no unearned radial");
  check(qobject_cast<QQuickWindow*>(root)->isVisible(),"window visible"); app.quit();
 });
 int ret=app.exec(); std::cout<<"VOR QML offscreen dialog and bindings passed\n"; return ret;
}
