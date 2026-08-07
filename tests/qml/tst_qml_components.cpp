// SPDX-License-Identifier: GPL-3.0-or-later
// Runner dei test Qt Quick sui componenti del modulo DecodiumSdr.

#include "app/SessionSingleton.h"
#include "core/SessionManager.h"

#include <QtQuickTest>

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *)
    {
        // I componenti sotto test non usano Session, ma il singleton deve
        // comunque poter essere istanziato se qualcuno lo tocca: meglio una
        // sessione vera che un puntatore nullo.
        static dsdr::core::SessionManager session;
        dsdr::app::SessionSingleton::instance = &session;
    }
};

QUICK_TEST_MAIN_WITH_SETUP(dsdr_qml_components, Setup)

#include "tst_qml_components.moc"
