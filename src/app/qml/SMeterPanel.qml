// SPDX-License-Identifier: GPL-3.0-or-later
// Lo strumento della colonna: uno alla volta, e si sceglie quale.
//
// Tre letture, non tre strumenti. Le prime due misurano lo stesso segnale in
// due modi — l'ago ha inerzia e mostra il QSB battere, le barre tengono il
// picco e dicono dove si è arrivati — e la terza misura quello che esce, che è
// un altro mestiere e serve mentre si trasmette.
//
// Un Loader solo, non uno per strumento: due Loader in colonna si prendono lo
// spazio del layout anche quando quello spento è alto zero, e in mezzo alla
// colonna restava una striscia vuota che non apparteneva a niente.
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

    /// Lo strumento mostrato: 0 il segnale con l'ago, 1 il segnale a barre,
    /// 2 la potenza.
    property int instrument: 0

    readonly property bool showingPower: instrument === 2

    title: showingPower ? qsTr("DECØMETER") : qsTr("DECØMETER-S")
    persistKey: "strumento"
    draggable: true

    /// La taratura dello strumento: a quanti dBFS corrisponde S9.
    ///
    /// Sta qui e non nel quadrante perché deve sopravvivere al riavvio: una
    /// taratura che si perde chiudendo il programma non è una taratura, è una
    /// regolazione da rifare ogni sera. Il valore vale per il ricevitore, non
    /// per il canale: cambiando canale la scala non si sposta.
    property real s9ReferenceDb: -55

    /// Se la taratura è stata fatta almeno una volta.
    ///
    /// La prima si fa da sé, appena arriva una misura del fondo: un valore di
    /// fabbrica va bene per un ricevitore e male per tutti gli altri, e chi
    /// apre il programma la prima volta non sa che esiste un tasto da premere.
    /// Dopo, resta ferma: è quello che distingue una taratura da un
    /// inseguimento.
    property bool calibrated: false

    /// Il fondo di rumore del canale corrente, per la prima taratura.
    property real currentFloorDb: -140

    // La prima taratura non si fa sulla prima misura: la stima del fondo parte
    // dal livello che trova e scende, quindi appena connessi è alta di
    // parecchi decibel e tararci sopra dà una scala buona per nessuno. Sei
    // secondi bastano perché l'inseguitore sia sceso dove deve.
    Timer {
        id: firstCalibration

        interval: 6000
        repeat: false
        running: !root.calibrated && root.currentFloorDb > -139

        onTriggered: {
            if (root.calibrated || root.currentFloorDb <= -139)
                return
            root.s9ReferenceDb = SMeterScale.s9From(root.currentFloorDb)
            root.calibrated = true
        }
    }

    Settings {
        category: "panels/strumento"
        property alias instrument: root.instrument
        property alias s9ReferenceDb: root.s9ReferenceDb
        property alias calibrated: root.calibrated
    }

    // ── Il selettore ─────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: [
                { key: 0, label: qsTr("AGO") },
                { key: 1, label: qsTr("BARRE") },
                { key: 2, label: qsTr("POTENZA") }
            ]

            delegate: Rectangle {
                required property var modelData

                readonly property bool current: root.instrument === modelData.key

                // Il nome serve al test che preme davvero il selettore: senza
                // un modo di ritrovare questi rettangoli, l'unica prova
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

    // ── Lo strumento ─────────────────────────────────────────────────────
    Loader {
        Layout.fillWidth: true
        sourceComponent: root.showingPower ? powerMeter : signalMeter
    }

    Component {
        id: powerMeter

        DecoMeter {
            metersAvailable: Session.txMetersAvailable
            forwardWatt: Session.txForwardWatt
            reflectedWatt: Session.txReflectedWatt
            swr: Session.txSwr
            transmitting: Session.transmitting
        }
    }

    // Un quadrante per canale, visibile quello corrente: è lo stesso modo in
    // cui il panadattatore mostra i notch del solo canale scelto. I quadranti
    // nascosti non dipingono e il loro timer sta fermo, mentre un solo
    // strumento aggiornato da fuori vorrebbe dire copiare i valori del
    // modello — e una copia si disallinea sempre.
    Component {
        id: signalMeter

        ColumnLayout {
            id: signalColumn

            spacing: 0

            /// Il canale che fa da secondo: il primo diverso da quello
            /// corrente. Non è una scelta, è quello che c'è — e con un canale
            /// solo non c'è, e i tasti B e A+B restano spenti.
            readonly property int secondIndex:
                Session.channels.count > 1
                    ? (Session.channels.currentIndex === 0 ? 1 : 0)
                    : -1

            property real secondLevelDb: -140

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

                    // Il canale che non è quello corrente porta il suo livello
                    // allo strumento, che lo disegna nel secondo arco: è la
                    // riga «RX B», e senza questa non avrebbe niente da dire.
                    onSignalDbChanged: if (index === signalColumn.secondIndex)
                                           signalColumn.secondLevelDb = signalDb

                    // Il fondo del canale corrente sale al pannello, che è
                    // dove vive la taratura: il quadrante la riceve, non la
                    // decide.
                    onNoiseFloorDbChanged:
                        if (Session.channels.currentIndex === index)
                            root.currentFloorDb = noiseFloorDb

                    DecoMeterS {
                        id: meter

                        width: entry.width
                        bars: root.instrument === 1
                        levelDb: entry.signalDb
                        noiseFloorDb: entry.noiseFloorDb
                        snrDb: entry.snrDb
                        modeName: entry.modeName
                        bandwidthHz: Math.max(0, entry.filterHighHz - entry.filterLowHz)
                        transmitting: Session.transmitting

                        // La taratura va e viene dal pannello: è del
                        // ricevitore, non di questo quadrante, e deve restare
                        // dov'è anche cambiando canale o lettura.
                        s9ReferenceDb: root.s9ReferenceDb
                        onS9ReferenceDbChanged: root.s9ReferenceDb = s9ReferenceDb

                        sourceLabel: Session.backendName
                        channelLabel: qsTr("RX %1").arg(entry.index + 1)
                        hasSecondChannel: signalColumn.secondIndex >= 0
                        secondLevelDb: signalColumn.secondLevelDb
                        secondChannelLabel: qsTr("RX %1").arg(signalColumn.secondIndex + 1)
                    }
                }
            }
        }
    }
}
