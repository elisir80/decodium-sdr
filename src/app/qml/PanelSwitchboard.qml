// SPDX-License-Identifier: GPL-3.0-or-later
// Le icone che accendono e spengono i pannelli della colonna.
//
// I pannelli si potevano già chiudere, uno per uno, dalla loro intestazione.
// Non basta: chiuso vuol dire ridotto a una riga di titolo, e nove righe di
// titolo sono comunque nove righe. Chi opera in fonia non ha bisogno della
// macchina del tempo; chi sta regolando l'immagine non ha bisogno del pannello
// di trasmissione. Quello che non serve deve poter sparire, non rimpicciolirsi.
//
// Una fila di icone e non un menu: la si guarda e si sa che cosa c'è, senza
// aprire niente. Le icone accese sono i pannelli in colonna, e la fila è
// l'indice di quello che si sta guardando.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Flow {
    id: root

    /// I pannelli, nella forma `{ key, glyph, label }`. Li dà chi tiene la
    /// colonna: è lui a sapere quali esistono.
    required property var panels

    /// Le chiavi dei pannelli spenti.
    required property var hidden

    signal toggled(string key)

    spacing: Theme.spacingTight

    Repeater {
        model: root.panels

        delegate: Rectangle {
            id: chip

            required property var modelData

            readonly property bool on: root.hidden.indexOf(modelData.key) < 0

            width: 30
            height: 26
            radius: Theme.radiusSmall
            color: on ? Theme.surfaceRaised : "transparent"
            border.width: 1
            border.color: on ? Theme.accent
                        : hover.hovered ? Theme.borderStrong : Theme.border
            opacity: on || hover.hovered ? 1.0 : 0.55

            Behavior on opacity {
                NumberAnimation { duration: Theme.animationFast }
            }

            Text {
                anchors.centerIn: parent
                text: chip.modelData.glyph
                font.pixelSize: Theme.fontNormal
                color: chip.on ? Theme.accent
                     : hover.hovered ? Theme.textPrimary : Theme.textDisabled
            }

            HoverHandler {
                id: hover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: root.toggled(chip.modelData.key)
            }

            // Il nome per esteso quando ci si ferma sopra: otto glifi in fila
            // sono otto indovinelli finché non si è imparato quale è quale, e
            // impararli è una tassa che si paga una volta ma la si paga.
            ToolTip.visible: hover.hovered
            ToolTip.text: chip.on ? qsTr("Nascondi %1").arg(chip.modelData.label)
                                  : qsTr("Mostra %1").arg(chip.modelData.label)
        }
    }
}
