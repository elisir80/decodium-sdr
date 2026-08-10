// SPDX-License-Identifier: GPL-3.0-or-later
// Lo strumento della colonna: uno alla volta, e si sceglie quale.
//
// Sono due mestieri diversi. Il quadrante a lancetta misura quello che entra e
// serve in ricezione; il DECØMETER misura quello che esce e serve mentre si
// trasmette. Tenerli entrambi aperti vorrebbe dire che uno dei due è sempre
// fermo a occupare la colonna, e la colonna è lo spazio che scarseggia.
//
// Il titolo dice quale strumento è attivo, perché a pannello chiuso il titolo
// è tutto quello che resta. La chiave con cui si ricorda l'apertura, invece,
// non cambia con lo strumento: altrimenti cambiare strumento riaprirebbe un
// pannello che era stato chiuso apposta.
import QtCore
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    /// Lo strumento mostrato: 0 il quadrante S, 1 il DECØMETER.
    property int instrument: 0

    readonly property bool showingSMeter: instrument === 0

    title: showingSMeter ? qsTr("S-METER") : qsTr("DECØMETER")
    persistKey: "strumento"
    draggable: true

    Settings {
        category: "panels/strumento"
        property alias instrument: root.instrument
    }

    // ── Il selettore ─────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: [
                { key: 0, label: qsTr("SEGNALE") },
                { key: 1, label: qsTr("POTENZA") }
            ]

            delegate: Rectangle {
                required property var modelData

                readonly property bool current: root.instrument === modelData.key

                // Il nome serve al test che preme davvero il selettore: senza
                // un modo di ritrovare questi due rettangoli, l'unica prova
                // possibile sarebbe assegnare la proprietà da fuori — che è
                // esattamente ciò che non fa chi usa il programma.
                objectName: "instrument-" + modelData.key

                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight - 6
                radius: Theme.radiusSmall
                color: current ? Theme.accentDim : Theme.surfaceSunken
                border.width: 1
                border.color: current ? Theme.accent : Theme.border

                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
                }

                Text {
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    font.bold: parent.current
                    color: parent.current ? Theme.textPrimary : Theme.textSecondary
                }

                TapHandler {
                    onTapped: root.instrument = parent.modelData.key
                }
            }
        }
    }

    // ── Il quadrante del segnale ─────────────────────────────────────────
    //
    // Un quadrante per canale, visibile quello corrente: è lo stesso modo in
    // cui il panadattatore mostra i notch del solo canale scelto. I quadranti
    // nascosti non dipingono e il loro timer sta fermo, mentre un solo
    // strumento aggiornato da fuori vorrebbe dire copiare i valori del
    // modello — e una copia si disallinea sempre.
    Loader {
        Layout.fillWidth: true
        active: root.showingSMeter

        sourceComponent: ColumnLayout {
            spacing: 0

            Repeater {
                model: Session.channels

                // Il quadrante sta dentro un contenitore invece di essere lui
                // stesso il delegate: i ruoli del modello si chiamano come le
                // sue proprietà — `snrDb`, `modeName` — e dichiararli sullo
                // stesso oggetto sarebbe dichiarare due volte la stessa
                // proprietà.
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
    }

    // ── Lo strumento di potenza ──────────────────────────────────────────
    Loader {
        Layout.fillWidth: true
        active: !root.showingSMeter

        sourceComponent: DecoMeter {
            metersAvailable: Session.txMetersAvailable
            forwardWatt: Session.txForwardWatt
            reflectedWatt: Session.txReflectedWatt
            swr: Session.txSwr
            transmitting: Session.transmitting
        }
    }
}
