// SPDX-License-Identifier: GPL-3.0-or-later
// Un capo della catena: da dove entra il segnale, dove esce.
//
// Un cerchio e non un blocco, perché non è uno stadio: non si accende, non si
// regola, non si esclude. È il microfono, l'antenna, la radio, le cuffie — e
// disegnarlo con la stessa forma degli stadi farebbe cercare un interruttore
// che non c'è.
import QtQuick
import DecodiumSdr

Rectangle {
    id: root

    property string label: ""

    /// Se da questo capo passa qualcosa adesso. Un capo spento non è un
    /// guasto: è una catena a riposo, e va detto senza allarmare.
    property bool live: true

    implicitWidth: 46
    implicitHeight: 46
    radius: width / 2
    color: Theme.surface
    border.width: 1
    border.color: live ? Theme.accent : Theme.border

    Behavior on border.color {
        ColorAnimation { duration: Theme.animationFast }
    }

    Text {
        anchors.centerIn: parent
        text: root.label
        font.pixelSize: Theme.fontSmall
        font.bold: true
        font.letterSpacing: 0.5
        color: root.live ? Theme.accent : Theme.textDisabled
    }
}
