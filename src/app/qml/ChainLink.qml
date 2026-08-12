// SPDX-License-Identifier: GPL-3.0-or-later
// Il collegamento fra due blocchi, con quanto ci passa.
//
// La misura sta fra i blocchi e non dentro: è lì che ha significato — quanto
// esce da uno è quanto entra nell'altro — ed è lì che si guarda quando ci si
// chiede se uno stadio sta esagerando.
//
// Dove una misura vera non c'è, resta la sola linea. Un indicatore fermo è
// peggio di nessun indicatore, perché lo si crede: si regola il blocco prima,
// non si vede cambiare niente, e si conclude che il blocco non funziona.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Il livello che passa, da 0 a 1. Negativo quando non si misura.
    property real level: -1

    /// La tinta della colonnina. Ambra o rossa dicono «qui si sta
    /// esagerando», e il colore lo decide chi conosce la grandezza.
    property color tint: Theme.accent

    implicitWidth: 34
    implicitHeight: 66

    Row {
        anchors.centerIn: parent
        spacing: 2

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: root.level >= 0 ? 10 : 16
            height: 2
            color: Theme.border
        }

        // La colonnina del livello: si riempie dal basso, come qualunque
        // indicatore che si sia mai guardato.
        Rectangle {
            visible: root.level >= 0
            anchors.verticalCenter: parent.verticalCenter
            width: 7
            height: 32
            radius: 3
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border
            clip: true

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 1
                height: Math.max(0, Math.min(1, root.level)) * (parent.height - 2)
                radius: 2
                color: root.tint

                Behavior on height {
                    NumberAnimation { duration: 80 }
                }
            }
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: root.level >= 0 ? 10 : 16
            height: 2
            color: Theme.border
        }
    }
}
