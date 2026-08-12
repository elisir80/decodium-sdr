// SPDX-License-Identifier: GPL-3.0-or-later
// Il diagramma di flusso: la catena disegnata come la si attraversa.
//
// È la tesi di DSDR-SPEC-005. Il concorrente di riferimento ha tutti questi
// stadi e li tiene in schede e sotto-menù: chi regola un compressore in una
// scheda e un equalizzatore in un'altra non ha modo di sapere quale dei due
// viene prima, e l'ordine è tutto. Qui i blocchi stanno in fila nel verso in
// cui il segnale li attraversa, con l'interruttore addosso e la misura in
// mezzo. Si capisce dove si sta intervenendo prima di aver toccato qualcosa.
//
// Compaiono solo gli stadi che il DSP ha davvero. Gate, leveller, EQ a curve,
// compressore multibanda e limiter sono disegnati nella specifica e non
// costruiti: un blocco che non fa niente è peggio di un blocco che manca
// (CONSTITUTION §7), e disegnarlo spento sarebbe una promessa da pagare al
// primo operatore che ci clicca sopra.
import QtQml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("FLUSSO")
    draggable: true
    collapsed: true
    // La catena vuole larghezza: in una striscia di trecento punti i blocchi
    // si accavallano e il diagramma smette di essere leggibile, che è l'unica
    // cosa per cui esiste.
    detachable: true

    /// Il blocco di cui si stanno vedendo i comandi. Uno per volta: la catena
    /// resta visibile mentre si regola, che è tutto il punto.
    property string picked: ""

    readonly property int row: Session.channels.currentIndex

    // ── Lo stato del canale scelto ───────────────────────────────────────
    //
    // Le impostazioni per canale non si leggono dal modello se non attraverso
    // un delegate: si srotola la riga corrente qui, una volta, invece di
    // tenerne una copia da riallineare.
    property bool nrEnabled: false
    property real nrStrength: 0
    property bool anfEnabled: false
    property int notchCount: 0
    property int filterLowHz: 0
    property int filterHighHz: 0
    property int agcMode: 0

    // Un Instantiator e non un Repeater: il delegate di un Repeater è un Item,
    // e un Item dentro un ColumnLayout è un figlio del layout — invisibile o
    // no, il layout gli faceva posto, e in cima al pannello restava una fascia
    // vuota alta quanto i canali aperti. Qui il delegate è un QtObject: non è
    // un oggetto visuale, non entra nel layout, e serve solo a srotolare la
    // riga del canale in proprietà leggibili.
    Instantiator {
        model: Session.channels

        delegate: QtObject {
            id: channelRow

            required property int index
            required property bool nrEnabled
            required property real nrStrength
            required property bool anfEnabled
            required property var notches
            required property int filterLowHz
            required property int filterHighHz
            required property int agcMode

            readonly property bool current: Session.channels.currentIndex === index

            // Un QtObject non ha una proprietà predefinita in cui
            // infilare figli: i binding stanno in una lista, che è il
            // modo di tenerli vivi senza un oggetto visuale attorno.
            property list<QtObject> wiring: [
                Binding { target: root; property: "nrEnabled"; value: channelRow.nrEnabled
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "nrStrength"; value: channelRow.nrStrength
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "anfEnabled"; value: channelRow.anfEnabled
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "notchCount"; value: channelRow.notches.length
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "filterLowHz"; value: channelRow.filterLowHz
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "filterHighHz"; value: channelRow.filterHighHz
                          when: channelRow.current; restoreMode: Binding.RestoreNone },
                Binding { target: root; property: "agcMode"; value: channelRow.agcMode
                          when: channelRow.current; restoreMode: Binding.RestoreNone }
            ]
        }
    }

    // ── L'alfabeto dei glifi ─────────────────────────────────────────────
    //
    // Ogni stadio porta addosso il disegno di quello che fa alla forma d'onda.
    // Sono i tracciati del disegno di riferimento, in coordinate 72×26.
    readonly property var glyphs: ({
        "lev":   "M2,18 C20,6 40,15 70,12",          // livella
        "lim":   "M2,20 C18,10 28,5 40,5 L70,5",     // tosa la cima
        "band":  "M2,12 C14,12 18,18 28,18 C40,18 46,8 56,8 C62,8 66,12 70,12",
        "nb":    "M2,14 L18,14 L20,4 L22,14 L44,14 L46,5 L48,14 L70,14",
        "notch": "M2,9 C20,9 24,24 30,24 C36,24 40,9 70,9",
        "nr":    "M2,17 C20,12 44,16 70,14",
        "mon":   "M2,13 L10,13 L14,5 L20,21 L26,9 L32,17 L38,11 L46,14 L70,13",
        "eq":    "M2,16 C12,16 14,7 22,7 C30,7 32,19 42,19 C52,19 58,9 70,9",
        "cfc":   "M6,24 L6,10 M20,24 L20,5 M34,24 L34,13 M48,24 L48,7 M62,24 L62,15",
    })

    /// Quanto sta abbassando la banda che lavora di più, in decibel. È la
    /// misura che dice se il multibanda sta esagerando, e sta sul collegamento
    /// perché è quello che esce dal blocco.
    readonly property real cfcWorking: {
        const values = Session.cfcReduction
        let worst = 0
        for (let i = 0; i < values.length; ++i)
            worst = Math.max(worst, values[i])
        return worst
    }

    /// Da dBFS a frazione di barretta. Sessanta decibel di scala: sotto non c'è
    /// niente da vedere, ed è la stessa scala della barra dei livelli audio.
    function fractionFromDb(db) {
        if (!isFinite(db))
            return -1
        return Math.max(0, Math.min(1, (db + 60) / 60))
    }

    // ── La catena di trasmissione ────────────────────────────────────────
    //
    // Sopra, perché è quella che si costruisce: la ricezione la si ascolta.
    // Compare solo dove c'è da trasmettere — la UI si genera dalle capability
    // (CONSTITUTION §7) — e si spegne quando la catena è a riposo, invece di
    // mostrare misure ferme che si crederebbero.
    RowLayout {
        Layout.fillWidth: true
        visible: Session.capabilities.canTransmit
        spacing: Theme.spacing

        Text {
            text: qsTr("TX · CATENA MICROFONO")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            font.letterSpacing: 1.4
            color: Theme.textDisabled
        }

        Item { Layout.fillWidth: true }

        Text {
            text: Session.transmitting ? qsTr("IN ARIA")
                : Session.micActive ? qsTr("PRONTA") : qsTr("A RIPOSO")
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.transmitting ? Theme.danger
                 : Session.micActive ? Theme.accent : Theme.textDisabled
        }
    }

    Flow {
        Layout.fillWidth: true
        // L'arco del bypass sale sopra il blocco: senza questo respiro
        // toccherebbe l'intestazione della corsia.
        Layout.topMargin: Theme.spacingTight
        visible: Session.capabilities.canTransmit
        spacing: 0
        // A riposo la corsia si smorza: c'è, e non sta lavorando.
        opacity: Session.transmitting || Session.micActive ? 1.0 : 0.45

        Behavior on opacity {
            NumberAnimation { duration: Theme.animationNormal }
        }

        ChainEndpoint {
            anchors.verticalCenter: undefined
            label: qsTr("MIC")
            live: Session.micActive
        }

        ChainLink {
            level: Session.micActive ? root.fractionFromDb(Session.micLevel) : -1
            tint: Session.micLevel > -3 ? Theme.danger
                : Session.micLevel > -12 ? Theme.spectrumPeak : Theme.success
        }

        ChainBlock {
            title: qsTr("Gate")
            glyph: root.glyphs.gate
            readout: Session.gateEnabled
                     ? qsTr("%1 dB").arg(Math.round(Session.gateThresholdDb)) : ""
            on: Session.gateEnabled
            selected: root.picked === "gate"
            onToggled: Session.gateEnabled = !Session.gateEnabled
            onPicked: root.picked = root.picked === "gate" ? "" : "gate"
        }

        ChainLink {
            level: Session.gateEnabled ? Session.gateOpening : -1
            tint: Session.gateOpening > 0.5 ? Theme.success : Theme.textDisabled
        }

        ChainBlock {
            title: qsTr("Leveller")
            glyph: root.glyphs.lev
            readout: Session.levellerEnabled
                     ? qsTr("%1 dB").arg(Session.levellerGainDb.toFixed(0)) : ""
            on: Session.levellerEnabled
            warning: Session.levellerEnabled && Session.levellerGainDb > 18
            selected: root.picked === "lev"
            onToggled: Session.levellerEnabled = !Session.levellerEnabled
            onPicked: root.picked = root.picked === "lev" ? "" : "lev"
        }

        ChainLink {}

        ChainBlock {
            title: qsTr("Compressore")
            glyph: root.glyphs.lev
            readout: qsTr("%1 dB").arg(Math.round(Session.txCompressionDb))
            on: Session.txCompressionDb > 0.5
            warning: Session.transmitting && Session.txCompressionMeter > 12
            selected: root.picked === "comp"
            // Escludere il compressore vuol dire portarlo a zero: non c'è un
            // interruttore separato, e inventarne uno vorrebbe dire tenere due
            // stati per la stessa cosa.
            onToggled: Session.txCompressionDb = Session.txCompressionDb > 0.5 ? 0 : 6
            onPicked: root.picked = root.picked === "comp" ? "" : "comp"
        }

        ChainLink {
            level: Session.transmitting
                   ? Math.min(1, Session.txCompressionMeter / 20) : -1
            tint: Session.txCompressionMeter > 12 ? Theme.spectrumPeak : Theme.success
        }

        ChainBlock {
            title: qsTr("CFC")
            glyph: root.glyphs.cfc
            readout: Session.cfcEnabled ? qsTr("punch %1").arg(Session.cfcPunch.toFixed(1)) : ""
            on: Session.cfcEnabled
            warning: root.cfcWorking > 8
            selected: root.picked === "cfc"
            onToggled: Session.cfcEnabled = !Session.cfcEnabled
            onPicked: root.picked = root.picked === "cfc" ? "" : "cfc"
        }

        ChainLink {
            level: Session.cfcEnabled ? Math.min(1, root.cfcWorking / 12) : -1
            tint: root.cfcWorking > 8 ? Theme.danger : Theme.spectrumPeak
        }

        ChainBlock {
            title: qsTr("Limiter")
            glyph: root.glyphs.lim
            readout: Session.limiterEnabled
                     ? qsTr("%1 dB").arg(Session.limiterCeilingDb.toFixed(0)) : ""
            on: Session.limiterEnabled
            warning: Session.limiterReductionDb > 6
            selected: root.picked === "lim"
            onToggled: Session.limiterEnabled = !Session.limiterEnabled
            onPicked: root.picked = root.picked === "lim" ? "" : "lim"
        }

        ChainLink {
            level: Session.limiterEnabled ? Math.min(1, Session.limiterReductionDb / 12) : -1
            tint: Session.limiterReductionDb > 6 ? Theme.danger : Theme.spectrumPeak
        }

        ChainBlock {
            title: qsTr("Filtro TX")
            glyph: root.glyphs.band
            readout: qsTr("%1 Hz").arg(Math.abs(root.filterHighHz - root.filterLowHz))
            switchable: false
            selected: root.picked === "txfilter"
            onPicked: root.picked = root.picked === "txfilter" ? "" : "txfilter"
        }

        ChainLink {}

        ChainBlock {
            title: qsTr("Drive")
            glyph: root.glyphs.lim
            readout: qsTr("%1%").arg(Math.round(Session.txDrive * 100))
            switchable: false
            warning: Session.transmitting && Session.txLevel > -1
            selected: root.picked === "drive"
            onPicked: root.picked = root.picked === "drive" ? "" : "drive"
        }

        ChainLink {
            level: Session.transmitting ? root.fractionFromDb(Session.txLevel) : -1
            tint: Session.txLevel > -1 ? Theme.danger : Theme.success
        }

        ChainEndpoint {
            label: qsTr("TX")
            live: Session.transmitting
        }
    }

    // ── Riascoltarsi (SPEC-005 §4.3) ─────────────────────────────────────
    //
    // Il pezzo che manca a ogni stazione che non ha un secondo ricevitore, e
    // sono quasi tutte. Mentre si parla si sente la propria voce per
    // conduzione ossea: qualunque giudizio dato in quel momento è dato sul
    // suono sbagliato, ed è il motivo per cui la maggior parte delle catene
    // audio in aria sono regolate male da persone convinte del contrario.
    //
    // Non c'è niente da armare prima: gli ultimi dieci secondi ci sono sempre,
    // perché ci si accorge di voler riascoltare solo dopo aver parlato.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingTight
        visible: Session.capabilities.canTransmit
        spacing: Theme.spacingTight

        DsdrButton {
            implicitWidth: 96
            implicitHeight: 26
            text: Session.voicePlaying ? qsTr("FERMA") : qsTr("RIASCOLTA")
            enabled: Session.voiceRecordingReady && !Session.transmitting
            onClicked: Session.voicePlaying ? Session.stopVoiceRecording()
                                            : Session.playVoiceRecording(1)
        }

        // Prima e dopo, commutabili **mentre suona**: si passa da una traccia
        // all'altra a metà parola e si sente la stessa sillaba nei due modi, di
        // seguito. Fermarsi e ripartire costringerebbe a ricordare com'era, e
        // il ricordo di un suono dura meno di un secondo.
        DsdrButton {
            implicitWidth: 62
            implicitHeight: 26
            text: qsTr("PRIMA")
            enabled: Session.voiceRecordingReady && !Session.transmitting
            checkable: true
            checked: Session.voicePlaybackSource === 0
            onClicked: {
                Session.voicePlaybackSource = 0
                if (!Session.voicePlaying)
                    Session.playVoiceRecording(0)
            }
        }

        DsdrButton {
            implicitWidth: 62
            implicitHeight: 26
            text: qsTr("DOPO")
            enabled: Session.voiceRecordingReady && !Session.transmitting
            checkable: true
            checked: Session.voicePlaybackSource === 1
            onClicked: {
                Session.voicePlaybackSource = 1
                if (!Session.voicePlaying)
                    Session.playVoiceRecording(1)
            }
        }

        // La barra dice due cose in una: quanto c'è registrato, e dove si è
        // arrivati a riascoltarlo.
        Rectangle {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: 6
            radius: 3
            color: Theme.surfaceSunken

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1,
                    Session.voiceRecordedSeconds / Session.voiceRecordingCapacitySeconds))
                height: parent.height
                radius: parent.radius
                color: Theme.border
            }

            Rectangle {
                visible: Session.voicePlaying
                width: parent.width * Math.max(0, Math.min(1,
                    Session.voicePlaybackPosition / Session.voiceRecordingCapacitySeconds))
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }

        Text {
            text: Session.voiceRecordingReady
                  ? qsTr("%1 s").arg(Session.voiceRecordedSeconds.toFixed(1))
                  : qsTr("—")
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.voiceRecordingReady ? Theme.textSecondary : Theme.textDisabled
        }
    }

    // Perché il tasto è spento: senza questa riga resta un comando grigio e
    // nessuna spiegazione, che è il modo in cui una funzione buona non viene
    // mai trovata.
    Text {
        Layout.fillWidth: true
        visible: Session.capabilities.canTransmit && !Session.voiceRecordingReady
        text: qsTr("Il riascolto si riempie da sé mentre si trasmette: parla, e poi risentiti.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingTight
        Layout.bottomMargin: Theme.spacingTight
        visible: Session.capabilities.canTransmit
        height: 1
        color: Theme.border
    }

    // ── La catena di ricezione ───────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("RX · CATENA RICEZIONE")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            font.letterSpacing: 1.4
            color: Theme.textDisabled
        }

        Item { Layout.fillWidth: true }

        Text {
            text: Session.transmitting ? qsTr("IN ATTESA")
                : Session.neuralEnabled ? qsTr("DECODIUM NR ATTIVO") : qsTr("IN ASCOLTO")
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.transmitting ? Theme.textDisabled
                 : Session.neuralEnabled ? Theme.spectrumPeak : Theme.accent
        }
    }

    Flow {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingTight
        spacing: 0
        // In trasmissione la radio si assorda: la corsia di ricezione c'è ma
        // non sta ricevendo, e mostrarla accesa sarebbe una bugia.
        opacity: Session.transmitting ? 0.45 : 1.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animationNormal }
        }

        ChainEndpoint {
            label: qsTr("RX")
            live: !Session.transmitting
        }

        ChainLink {
            level: root.fractionFromDb(Session.peakDbfs)
            tint: Session.overloaded ? Theme.danger : Theme.success
        }

        ChainBlock {
            title: qsTr("NB")
            glyph: root.glyphs.nb
            readout: Session.noiseBlanker
                     ? qsTr("%1×").arg(Session.noiseBlankerThreshold.toFixed(1)) : ""
            on: Session.noiseBlanker
            warning: Session.noiseBlanker && Session.noiseBlankerActivity > 0.02
            selected: root.picked === "nb"
            onToggled: Session.setNoiseBlanker(!Session.noiseBlanker,
                                               Session.noiseBlankerThreshold)
            onPicked: root.picked = root.picked === "nb" ? "" : "nb"
        }

        ChainLink {
            // Quanto sta lavorando il soppressore: la frazione di campioni che
            // ha toccato, e dice se la soglia è troppo bassa.
            level: Session.noiseBlanker ? Math.min(1, Session.noiseBlankerActivity * 20) : -1
            tint: Session.noiseBlankerActivity > 0.02 ? Theme.spectrumPeak : Theme.success
        }

        ChainBlock {
            title: qsTr("Filtro")
            glyph: root.glyphs.band
            readout: qsTr("%1 Hz").arg(Math.abs(root.filterHighHz - root.filterLowHz))
            switchable: false
            selected: root.picked === "rxfilter"
            onPicked: root.picked = root.picked === "rxfilter" ? "" : "rxfilter"
        }

        ChainLink {}

        ChainBlock {
            title: qsTr("NR")
            glyph: root.glyphs.nr
            readout: root.nrEnabled ? root.nrStrength.toFixed(0) : ""
            on: root.nrEnabled
            selected: root.picked === "nr"
            onToggled: Session.setChannelNoiseReduction(root.row, !root.nrEnabled,
                                                        root.nrStrength)
            onPicked: root.picked = root.picked === "nr" ? "" : "nr"
        }

        ChainLink {}

        ChainBlock {
            title: qsTr("Notch")
            glyph: root.glyphs.notch
            readout: root.notchCount > 0 ? qsTr("%1 + ANF").arg(root.notchCount)
                   : root.anfEnabled ? qsTr("ANF") : ""
            on: root.anfEnabled || root.notchCount > 0
            selected: root.picked === "notch"
            onToggled: Session.setChannelAutoNotch(root.row, !root.anfEnabled)
            onPicked: root.picked = root.picked === "notch" ? "" : "notch"
        }

        ChainLink {}

        ChainBlock {
            title: qsTr("Decodium NR")
            neural: true
            neuralTag: Session.neuralEnabled
                       ? qsTr("%1 ms").arg(Session.neuralLatencyMs.toFixed(0)) : ""
            on: Session.neuralEnabled
            unavailable: !Session.neuralAvailable
            selected: root.picked === "dnr"
            onToggled: Session.setNeuralNr(!Session.neuralEnabled)
            onPicked: root.picked = root.picked === "dnr" ? "" : "dnr"
        }

        ChainLink {
            level: Session.neuralEnabled ? Math.min(1, Session.neuralLoad) : -1
            tint: Session.neuralLoad > 0.7 ? Theme.danger
                : Session.neuralLoad > 0.4 ? Theme.spectrumPeak : Theme.success
        }

        ChainBlock {
            title: qsTr("AGC")
            glyph: root.glyphs.lev
            readout: {
                const names = Session.agcModeNames()
                return (root.agcMode >= 0 && root.agcMode < names.length)
                       ? names[root.agcMode] : ""
            }
            switchable: false
            selected: root.picked === "agc"
            onPicked: root.picked = root.picked === "agc" ? "" : "agc"
        }

        ChainLink {}

        // L'equalizzatore dell'ascolto: l'ultimo stadio prima delle cuffie, e
        // il primo che non c'era. La sua curva si regola nello studio audio,
        // sopra lo spettro vivo.
        ChainBlock {
            title: qsTr("EQ")
            glyph: root.glyphs.eq
            readout: Session.audioEqEnabled ? qsTr("5 celle") : ""
            on: Session.audioEqEnabled
            selected: root.picked === "eq"
            onToggled: Session.audioEqEnabled = !Session.audioEqEnabled
            onPicked: root.picked = root.picked === "eq" ? "" : "eq"
        }

        ChainLink {
            level: root.fractionFromDb(Session.audioPeakDb)
            tint: Session.audioPeakDb > -1 ? Theme.danger
                : Session.audioPeakDb > -6 ? Theme.spectrumPeak : Theme.success
        }

        ChainEndpoint {
            label: qsTr("CUFFIE")
            live: !Session.transmitting
        }
    }

    // ── L'interlock ──────────────────────────────────────────────────────
    //
    // Va detto qui e non in una nota di rilascio: questa catena è per le
    // orecchie e per la voce. Un compressore prima di un decodificatore non
    // migliora niente e rovina la stima del rapporto segnale-rumore.
    Text {
        Layout.fillWidth: true
        text: root.picked === ""
              ? qsTr("Premi un blocco per aprirne i comandi. Il percorso dei decoder non passa di qui: resta lineare.")
              : qsTr("Il percorso dei decoder non passa di qui: resta lineare.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── I comandi del blocco scelto ──────────────────────────────────────
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: detail.implicitHeight + 2 * Theme.spacing
        visible: root.picked !== ""
        radius: Theme.radiusSmall
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.borderStrong

        ColumnLayout {
            id: detail

            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spacingTight

            Text {
                Layout.fillWidth: true
                text: root.detailTitle
                font.pixelSize: Theme.fontSmall
                font.bold: true
                color: Theme.accent
            }

            Text {
                Layout.fillWidth: true
                text: root.detailHint
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            // Il multibanda ha quattro misure e un comando: un cursore solo
            // non basterebbe, e le quattro colonne sono la ragione per cui si
            // guarda questo pannello invece di fidarsi dell'orecchio.
            CfcBands {
                Layout.fillWidth: true
                visible: root.picked === "cfc"
            }

            // Un cursore solo, quello che quel blocco ha davvero. I blocchi
            // con più di una regolazione — l'AGC, il filtro — mandano al loro
            // pannello: duplicare qui dieci comandi vorrebbe dire tenerne due
            // copie, e due copie divergono.
            RowLayout {
                Layout.fillWidth: true
                visible: root.detailFrom < root.detailTo
                spacing: Theme.spacing

                DsdrSlider {
                    id: detailSlider

                    Layout.fillWidth: true
                    from: root.detailFrom
                    to: root.detailTo
                    value: root.detailValue
                    onMoved: root.applyDetail(value)
                }

                Text {
                    text: root.detailReadout
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textPrimary
                }
            }
        }
    }

    // ── Che cosa sa dire ogni blocco ─────────────────────────────────────
    //
    // Una tabella sola invece di un ramo per blocco sparso nel file: quando se
    // ne aggiunge uno si tocca un posto, e quando se ne sbaglia uno si vede
    // accanto agli altri.
    readonly property var detailTitle: ({
        "mic": qsTr("Microfono"),
        "comp": qsTr("Compressore"),
        "txfilter": qsTr("Filtro di trasmissione"),
        "drive": qsTr("Drive"),
        "input": qsTr("Ingresso RF"),
        "nb": qsTr("Soppressore di impulsi"),
        "rxfilter": qsTr("Filtro di canale"),
        "nr": qsTr("Riduzione di rumore"),
        "notch": qsTr("Notch"),
        "dnr": qsTr("DECODIUM NR"),
        "agc": qsTr("AGC"),
        "eq": qsTr("Equalizzatore d'ascolto"),
        "cfc": qsTr("Compressore multibanda"),
        "gate": qsTr("Gate"),
        "lev": qsTr("Leveller"),
        "lim": qsTr("Limiter"),
    })[picked] || ""

    readonly property var detailHint: ({
        "mic": qsTr("Il guadagno d'ingresso. Si alza finché il livello tocca il giallo sui picchi della voce, non oltre: sopra si tosa, e tosare qui vuol dire mandarlo in aria."),
        "comp": qsTr("Quanto stringe la dinamica. Sei decibel si sentono in più senza sentirsi; oltre i dodici la voce diventa piatta e il rumore della stanza sale con lei."),
        "txfilter": qsTr("La banda che si occupa. 300–2700 è il compromesso classico; più larga si è più naturali e più si dà fastidio ai vicini."),
        "drive": qsTr("Quanto del fondo scala si consegna alla radio. Non è la potenza del finale: quella la conosce solo lei."),
        "input": qsTr("Il picco all'ingresso del convertitore. Quando satura è lo spettro a mentire per primo: il fondo si alza e i deboli spariscono."),
        "nb": qsTr("La soglia oltre la quale un campione è considerato un impulso. Troppo bassa e comincia a bucare il segnale insieme al disturbo."),
        "rxfilter": qsTr("La larghezza del canale. Si regola dalla targa o dal pannello dei canali, dove ci sono anche le larghezze pronte per il modo."),
        "nr": qsTr("Quanto aggressivamente si toglie il fondo. Alzandola troppo la voce diventa metallica: è il rumore che manca, non la voce che migliora."),
        "notch": qsTr("Il notch automatico toglie i fischi. Quelli a mano si piazzano con il tasto destro sullo spettro."),
        "dnr": qsTr("Lo stadio neurale. Costa un thread e qualche millisecondo di ritardo, e agisce solo sull'ascolto: il flusso verso i decoder resta lineare."),
        "agc": qsTr("Il modo e la soglia si scelgono dalla targa o dal pannello dei canali: sono due comandi, e qui ce ne sta uno."),
        "eq": qsTr("Cinque campane sull'ascolto. La curva si trascina nello studio audio, sopra lo spettro vivo: si muove il punto e si vede la voce cambiare forma sotto. Non tocca il percorso dei decoder."),
        "cfc": qsTr("La voce divisa in quattro bande, ognuna con il suo compressore: una sibilante non abbassa più il corpo della voce, e un colpo sul microfono non fa sparire la presenza. Un comando solo muove le quattro soglie."),
        "gate": qsTr("Toglie la stanza fra una frase e l'altra. La soglia si regola sul rumore del proprio posto, non su un valore di libro: si alza finché il respiro sparisce e non un decibel di più, o sparisce anche la coda delle parole."),
        "lev": qsTr("Un AGC lento che corregge la distanza dal microfono. Sta prima del compressore perché insegue i secondi, mentre quello insegue i millisecondi: una costante di tempo sola non può fare bene entrambe."),
        "lim": qsTr("L'ultimo, e l'unico che non deve mai lasciar passare niente oltre il tetto: oltre, il modulatore tosa, e tosare in banda base vuol dire allargarsi sui vicini. Guarda avanti di due millisecondi, che è il ritardo che aggiunge."),
    })[picked] || ""

    readonly property real detailFrom: ({
        "mic": 0, "comp": 0, "drive": 0, "nb": 1, "nr": 0, "dnr": 0,
        "gate": -70, "lev": -35, "lim": -8,
    })[picked] !== undefined ? ({
        "mic": 0, "comp": 0, "drive": 0, "nb": 1, "nr": 0, "dnr": 0,
        "gate": -70, "lev": -35, "lim": -8,
    })[picked] : 0

    readonly property real detailTo: ({
        "mic": 40, "comp": 20, "drive": 1, "nb": 12, "nr": 10, "dnr": 1,
        "gate": -20, "lev": -8, "lim": 0,
    })[picked] !== undefined ? ({
        "mic": 40, "comp": 20, "drive": 1, "nb": 12, "nr": 10, "dnr": 1,
        "gate": -20, "lev": -8, "lim": 0,
    })[picked] : 0

    readonly property real detailValue: {
        switch (picked) {
        case "mic":   return Session.micGainDb
        case "comp":  return Session.txCompressionDb
        case "drive": return Session.txDrive
        case "nb":    return Session.noiseBlankerThreshold
        case "nr":    return nrStrength
        case "dnr":   return Session.neuralIntensity
        case "gate":  return Session.gateThresholdDb
        case "lev":   return Session.levellerTargetDb
        case "lim":   return Session.limiterCeilingDb
        }
        return 0
    }

    readonly property string detailReadout: {
        switch (picked) {
        case "mic":   return qsTr("%1 dB").arg(Session.micGainDb.toFixed(0))
        case "comp":  return qsTr("%1 dB").arg(Session.txCompressionDb.toFixed(0))
        case "drive": return qsTr("%1%").arg(Math.round(Session.txDrive * 100))
        case "nb":    return qsTr("%1×").arg(Session.noiseBlankerThreshold.toFixed(1))
        case "nr":    return nrStrength.toFixed(0)
        case "dnr":   return qsTr("%1%").arg(Math.round(Session.neuralIntensity * 100))
        case "gate":  return qsTr("%1 dB").arg(Session.gateThresholdDb.toFixed(0))
        case "lev":   return qsTr("%1 dB").arg(Session.levellerTargetDb.toFixed(0))
        case "lim":   return qsTr("%1 dB · %2 ms")
                             .arg(Session.limiterCeilingDb.toFixed(1))
                             .arg(Session.limiterLatencyMs.toFixed(1))
        }
        return ""
    }

    function applyDetail(value) {
        switch (picked) {
        case "mic":   Session.micGainDb = value; break
        case "comp":  Session.txCompressionDb = value; break
        case "drive": Session.txDrive = value; break
        case "nb":    Session.setNoiseBlanker(Session.noiseBlanker, value); break
        case "nr":    Session.setChannelNoiseReduction(row, nrEnabled, value); break
        case "dnr":   Session.neuralIntensity = value; break
        case "gate":  Session.gateThresholdDb = value; break
        case "lev":   Session.levellerTargetDb = value; break
        case "lim":   Session.limiterCeilingDb = value; break
        }
    }
}
