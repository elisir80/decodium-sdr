// SPDX-License-Identifier: GPL-3.0-or-later
// Il pannello dello strumento, nella colonna dei pannelli.
//
// Guarda il canale corrente e basta. Uno strumento a lancetta con dentro
// quattro segnali diversi non si legge, e chi opera guarda quello su cui è
// sintonizzato; gli altri canali hanno già la loro barra nella scheda.
//
// Un quadrante per canale, e visibile quello corrente: è lo stesso modo in cui
// il panadattatore mostra i notch del solo canale scelto. Sembra spreco e non
// lo è — i quadranti nascosti non dipingono e il loro timer sta fermo — mentre
// un solo strumento aggiornato a mano da fuori vorrebbe dire copiare i valori
// del modello, e una copia si disallinea sempre.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("S-METER")
    draggable: true

    Repeater {
        model: Session.channels

        // Il quadrante sta dentro un contenitore invece di essere lui stesso il
        // delegate: i ruoli del modello si chiamano come le sue proprietà —
        // `snrDb`, `modeName` — e dichiararli sullo stesso oggetto sarebbe
        // dichiarare due volte la stessa proprietà.
        delegate: Item {
            id: entry

            required property int index
            required property real signalDb
            required property real noiseFloorDb
            required property real snrDb
            required property string modeName
            required property int filterLowHz
            required property int filterHighHz

            Layout.fillWidth: true
            Layout.preferredHeight: meter.implicitHeight

            visible: Session.channels.currentIndex === index

            LcdSMeter {
                id: meter

                width: entry.width
                levelDb: entry.signalDb
                noiseFloorDb: entry.noiseFloorDb
                snrDb: entry.snrDb
                modeName: entry.modeName
                bandwidthHz: Math.max(0, entry.filterHighHz - entry.filterLowHz)
                transmitting: Session.transmitting
            }
        }
    }
}
