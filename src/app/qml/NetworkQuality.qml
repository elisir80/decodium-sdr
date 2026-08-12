// SPDX-License-Identifier: GPL-3.0-or-later
// La salute del collegamento, in fondo alla finestra.
//
// Quando la sorgente sta dall'altra parte di una rete c'è una domanda che
// prima non aveva risposta: la banda è vuota, o i campioni non stanno
// arrivando? Sono due cose che sullo schermo si somigliano — traccia piatta,
// waterfall scuro — e portano a cercare il guasto in due posti opposti.
//
// La misura è il rapporto fra i campioni contati nell'ultimo secondo e quelli
// che la frequenza di campionamento dichiarata prometteva. Scende prima che si
// senta qualcosa, che è l'unico momento in cui saperlo serve a qualcosa.
//
// Compare solo dove c'è una rete: su una radio attaccata al bus quel rapporto
// sta incollato a uno, e un indicatore che dice sempre la stessa cosa smette
// di essere letto — e poi non lo si legge nemmeno il giorno che cambia.
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Row {
    id: root

    readonly property real health: Session.streamHealth

    /// Il colore dice il giudizio, e le soglie non sono estetiche.
    ///
    /// Sopra il 98 % il flusso è integro: quello che manca è il conteggio di un
    /// secondo che finisce a metà blocco. Sotto il 90 % si stanno perdendo
    /// campioni a sufficienza perché l'audio cominci a spezzarsi, e a quel
    /// punto è un guasto da guardare, non una statistica.
    readonly property color tint: {
        if (health < 0)
            return Theme.textDisabled
        if (health >= 0.98)
            return Theme.success
        if (health >= 0.90)
            return Theme.spectrumPeak
        return Theme.danger
    }

    visible: Session.connected && Session.streamOverNetwork
    spacing: Theme.spacingTight

    // Quattro tacche di altezza crescente: si legge di sbieco, senza fermarsi
    // a interpretare un numero. Il numero c'è comunque accanto, perché «tre
    // tacche su quattro» non si scrive in una segnalazione.
    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Repeater {
            model: 4

            delegate: Rectangle {
                required property int index

                width: 3
                height: 4 + index * 3
                anchors.bottom: parent.bottom
                radius: 1
                // Una tacca è accesa se la salute ha superato la sua quota:
                // la prima a un quarto, l'ultima a uno.
                color: root.health >= (index + 1) / 4 ? root.tint : Theme.border
            }
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: root.health < 0 ? qsTr("rete —")
                              : qsTr("rete %1%").arg(Math.round(root.health * 100))
        font.pixelSize: Theme.fontSmall
        font.family: Theme.monoFamily
        color: root.tint
    }

    HoverHandler { id: hover }

    ToolTip.visible: hover.hovered
    ToolTip.text: root.health < 0
        ? qsTr("Nessuna misura ancora: serve un secondo di flusso.")
        : qsTr("Campioni arrivati nell'ultimo secondo rispetto a quelli attesi. Sotto il 90% l'audio comincia a spezzarsi.")
}
