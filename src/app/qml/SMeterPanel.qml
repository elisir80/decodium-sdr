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

    /// La potenza è una misura TX reale, non una trasformazione del livello
    /// ricevuto. Un RTL-SDR è solo RX e non può fornire watt o ROS: lì il
    /// quadrante mostrerebbe dei trattini, e il tasto non deve esserci.
    ///
    /// **A dirlo sono le capability, non una misura arrivata.** Il primo
    /// criterio era «è arrivato un valore sopra zero», e aveva due difetti che
    /// si incontrano tutti e due con una radio vera collegata: il tasto restava
    /// spento finché non si era **già trasmesso** — e un rosmetro lo si guarda
    /// mentre si accorda, non dopo — mentre da scollegati era acceso, cioè
    /// proprio quando non c'è certamente niente da misurare.
    ///
    /// Chi trasmette può misurare: è quello che dichiara il backend, e si sa
    /// prima di premere qualunque cosa.
    /// Assegnabile, e non `readonly`: il legame con le capability regge
    /// finché nessuno scrive, ed è quello che serve al programma. Una prova
    /// però deve poter esercitare tutti e due i casi — con e senza wattmetro —
    /// e senza questo seam l'unico modo sarebbe collegare una radio vera al
    /// banco di prova.
    property bool powerMeterAvailable:
        Session.connected && Session.capabilities.canTransmit
    readonly property bool showingPower: instrument === 2 && powerMeterAvailable

    title: showingPower ? qsTr("DECØMETER") : qsTr("DECØMETER-S")
    persistKey: "strumento"
    draggable: true

    /// La taratura dello strumento: a quanti dBFS corrisponde S9.
    ///
    /// Sta qui e non nel quadrante perché deve sopravvivere al riavvio: una
    /// taratura che si perde chiudendo il programma non è una taratura, è una
    /// regolazione da rifare ogni sera. Il valore vale per il ricevitore, non
    /// per il canale: cambiando canale la scala non si sposta.
    readonly property real defaultS9ReferenceDb: -55
    property real s9ReferenceDb: defaultS9ReferenceDb

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

    function normalizeInstrument() {
        if (instrument === 2 && !powerMeterAvailable)
            instrument = 0
    }

    // Una vecchia prima taratura poteva salvare S9 sopra 0 dBFS se l'app si
    // apriva su una broadcast FM: non è una calibrazione valida e rende S1
    // qualunque segnale. Ripararla qui aggiorna anche le preferenze esistenti.
    function normalizeCalibration() {
        if (!isFinite(s9ReferenceDb) || s9ReferenceDb > SMeterScale.maxS9ReferenceDb
                || s9ReferenceDb < -140) {
            s9ReferenceDb = defaultS9ReferenceDb
            calibrated = false
        }
    }

    Component.onCompleted: {
        normalizeInstrument()
        normalizeCalibration()
    }
    onInstrumentChanged: normalizeInstrument()
    onPowerMeterAvailableChanged: normalizeInstrument()
    onS9ReferenceDbChanged: normalizeCalibration()

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

    // Qt.labs.settings può applicare un valore persistito dopo il completion
    // del contenitore. Rimandare di un giro garantisce che una preferenza
    // precedente e impossibile, ad esempio S9 = +13 dBFS, venga riparata al
    // primo avvio e non solo quando il pannello è già stato usato.
    Timer {
        interval: 0
        repeat: false
        running: true
        onTriggered: root.normalizeCalibration()
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

                readonly property bool current:
                    root.instrument === modelData.key && enabled

                // Il nome serve al test che preme davvero il selettore: senza
                // un modo di ritrovare questi rettangoli, l'unica prova
                // possibile sarebbe assegnare la proprietà da fuori — che è
                // esattamente ciò che non fa chi usa il programma.
                objectName: "instrument-" + modelData.key

                // Non spento: assente. Un blocco che non fa niente è peggio di
                // un blocco che manca (CONSTITUTION §7), e un tasto grigio
                // resta lì a far chiedere che cosa manchi per accenderlo.
                visible: modelData.key !== 2 || root.powerMeterAvailable

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
                    color: parent.current ? Theme.textPrimary
                           : parent.enabled ? Theme.textSecondary : Theme.textDisabled
                }

                TapHandler {
                    onTapped: root.instrument = parent.modelData.key
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Session.connected && !root.powerMeterAvailable
        text: Session.capabilities.canTransmit
              ? qsTr("Wattmetro TX non fornito da questo backend.")
              : qsTr("Ricevitore solo RX: il wattmetro TX non è disponibile.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
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
