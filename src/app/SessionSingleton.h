// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — esposizione di SessionManager a QML come singleton `Session`.
//
// La registrazione è dichiarativa (QML_FOREIGN + QML_SINGLETON) e non
// imperativa: registrare a mano un tipo nell'URI di un modulo generato da
// qt_add_qml_module crea un secondo namespace con lo stesso nome, e l'engine
// finisce per non vedere più i tipi C++ del modulo.
//
// SessionManager vive in dsdr_core, che non deve dipendere da QtQml: il
// collegamento sta tutto qui, nel livello applicativo.
#pragma once

#include "core/SessionManager.h"

#include <QJSEngine>
#include <QQmlEngine>

namespace dsdr::app {

struct SessionSingleton
{
    Q_GADGET
    QML_FOREIGN(dsdr::core::SessionManager)
    QML_NAMED_ELEMENT(Session)
    QML_SINGLETON

public:
    /// Istanza posseduta da main(): il singleton la espone, non la crea.
    inline static core::SessionManager *instance = nullptr;

    static core::SessionManager *create(QQmlEngine *, QJSEngine *)
    {
        // La proprietà resta del C++: l'engine non deve mai distruggerla.
        QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

} // namespace dsdr::app
