// SPDX-License-Identifier: GPL-3.0-or-later
// Runner dei test Qt Quick sui componenti del modulo DecodiumSdr.

#include "app/SessionSingleton.h"
#include "core/SessionManager.h"

#include <QCoreApplication>
#include <QtQuickTest>

class Setup : public QObject
{
    Q_OBJECT

public slots:
    void applicationAvailable()
    {
        // I pannelli si ricordano com'erano, e per farlo aprono un QSettings.
        // Senza un'identità l'oggetto non si costruisce e ogni istanza urla un
        // avviso: qui erano centinaia, e in mezzo non si vedeva più nulla.
        //
        // L'organizzazione è apposta diversa da quella del prodotto: un test
        // che scrivesse le preferenze vere riordinerebbe i pannelli a chi sta
        // usando l'applicazione.
        QCoreApplication::setOrganizationName(QStringLiteral("DECODIUM-test"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("test.decodium.invalid"));
        QCoreApplication::setApplicationName(QStringLiteral("qml-components"));
    }

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
