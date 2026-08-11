// SPDX-License-Identifier: GPL-3.0-or-later
// Un comando compatto della targa: mostra il valore, e premuto apre l'elenco.
//
// Sulla targa lo spazio è una riga sopra il waterfall: undici modi in fila la
// renderebbero più larga della finestra, e il pannello della colonna — dove i
// pulsanti stanno tutti sotto gli occhi — resta il posto giusto per chi si
// siede a regolare. Qui serve l'altra cosa: cambiare al volo quello che si sta
// ascoltando senza spostare lo sguardo dal segnale.
//
// Chiuso occupa quanto un'etichetta, che è la ragione per cui prima erano
// etichette: una targa che copre lo spettro va tenuta stretta.
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Rectangle {
    id: root

    /// Il valore mostrato quando il menu è chiuso.
    property alias text: label.text

    /// Le voci fra cui scegliere, e quale è in uso. Un indice negativo vuol
    /// dire che il valore corrente non è fra le voci — succede con un filtro
    /// regolato a mano — e allora non se ne segna nessuna, che è più onesto
    /// di spuntarne una a caso.
    property var model: []
    property int currentIndex: -1

    signal selected(int index)

    implicitWidth: label.implicitWidth + 2 * Theme.spacing + 10
    implicitHeight: label.implicitHeight + 6
    radius: Theme.radiusSmall
    color: menu.opened ? Theme.surfaceRaised : "transparent"
    border.width: 1
    border.color: menu.opened ? Theme.accent
                : hover.hovered ? Theme.borderStrong : Theme.border
    opacity: enabled ? 1 : 0.5

    Behavior on border.color {
        ColorAnimation { duration: Theme.animationFast }
    }

    Row {
        anchors.centerIn: parent
        spacing: 4

        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: Theme.fontSmall
            color: hover.hovered || menu.opened ? Theme.textPrimary : Theme.textSecondary
        }

        // La punta dice che c'è dell'altro sotto: senza, resta un'etichetta
        // che per caso reagisce al clic.
        Canvas {
            anchors.verticalCenter: parent.verticalCenter
            width: 7
            height: 4

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, 0)
                ctx.lineTo(width / 2, height)
                ctx.closePath()
                ctx.fillStyle = Theme.textDisabled
                ctx.fill()
            }
        }
    }

    HoverHandler {
        id: hover
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.enabled
        onTapped: menu.opened ? menu.close() : menu.popup(0, root.height + 2)
    }

    Menu {
        id: menu

        // Lo sfondo è opaco e il menu sta sopra tutto: sotto scorre il
        // waterfall, e un elenco velato su un waterfall in movimento non si
        // legge — è la stessa lezione dei comandi che galleggiavano.
        background: Rectangle {
            implicitWidth: 130
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radiusSmall
        }

        Repeater {
            model: root.model

            delegate: MenuItem {
                required property int index
                required property string modelData

                text: modelData
                height: 26

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: Theme.fontSmall
                    font.bold: index === root.currentIndex
                    color: index === root.currentIndex ? Theme.accent
                         : parent.hovered ? Theme.textPrimary : Theme.textSecondary
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacing
                }

                background: Rectangle {
                    color: parent.hovered ? Theme.surface : "transparent"
                }

                onTriggered: root.selected(index)
            }
        }
    }
}
