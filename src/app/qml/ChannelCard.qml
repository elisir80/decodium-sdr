// SPDX-License-Identifier: GPL-3.0-or-later
// La scheda di un canale RX.
//
// Riunisce i comandi delle due linee di lavoro: da una parte modo, filtro,
// AGC, squelch, CTCSS, RDS e catena FM; dall'altra i filtri di disturbo per
// canale (NR, ANF, notch), lo spostamento del passa-banda e la misura del
// rapporto segnale/rumore.
//
// Sta in un file suo perché è il blocco più lungo dell'interfaccia: la colonna
// che lo ospita deve restare leggibile per chi cerca l'ordine dei pannelli.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: entry


    required property int index
    required property color channelColor
    required property string label
    required property real frequencyHz
    required property int mode
    required property string modeName
    required property int filterLowHz
    required property int filterHighHz
    required property int agcMode
    required property real agcThresholdDb
    required property real agcAttackMs
    required property real agcDecayMs
    required property bool amCarrierAgc
    required property bool agcAutoThreshold
    required property real volume
    required property bool muted
    required property bool audioHighPassEnabled
    required property real audioHighPassHz
    required property bool fmStereo
    required property bool fmAudioLowPass
    required property real fmDeemphasisUs
    required property bool fmRds
    required property bool rdsAutomaticAf
    required property int rdsRegion
    required property bool rdsSynced
    required property string rdsPi
    required property int rdsCountryCode
    required property int rdsProgramCoverage
    required property int rdsReferenceNumber
    required property string rdsCallsign
    required property string rdsProgramType
    required property string rdsAlternateFrequencies
    required property string rdsProgramService
    required property string rdsRadioText
    required property bool squelchEnabled
    required property real squelchThresholdDb
    required property bool ctcssEnabled
    required property bool ctcssDecodeOnly
    required property real ctcssToneHz
    required property bool fmIfNoiseReductionEnabled
    required property int fmIfNoiseReductionPreset
    required property real signalDb
    required property real noiseFloorDb
    required property real snrDb
    required property real audioLevelDb
    required property real agcGainDb
    required property real passbandShiftHz
    required property bool nrEnabled
    required property real nrStrength
    required property bool anfEnabled
    required property var notches
    required property bool apfEnabled
    required property real apfQ
    required property bool binauralCw
    required property int samSideband

    readonly property bool current: Session.channels.currentIndex === index

    /// Toni CTCSS normalizzati (EIA/TIA-603). Stavano nel contenitore quando la
    /// scheda era un delegate scritto lì dentro; ora che è un componente suo se
    /// li porta appresso, altrimenti dipenderebbe da chi la ospita.
    readonly property var ctcssTones: [67.0, 71.9, 74.4, 77.0, 79.7, 82.5,
        85.4, 88.5, 91.5, 94.8, 97.4, 100.0, 103.5, 107.2, 110.9,
        114.8, 118.8, 123.0, 127.3, 131.8, 136.5, 141.3, 146.2,
        151.4, 156.7, 162.2, 167.9, 173.8, 179.9, 186.2, 192.8,
        203.5, 210.7, 218.1, 225.7, 233.6, 241.8, 250.3]
    // DemodMode::Fm è il settimo elemento dell'enum (indice 6).
    // In Wide-FM il canale RF occupa circa 180 kHz, mentre gli
    // altri modi usano il controllo stretto già esistente.
    readonly property bool wideFm: entry.mode === 6
    readonly property bool fmMode: entry.mode === 6 || entry.mode === 7
    readonly property bool highPassAllowed: entry.mode !== 2
                                         && entry.mode !== 3
                                         && entry.mode !== 10
    readonly property bool squelchAllowed: entry.mode !== 2
                                           && entry.mode !== 3
                                           && entry.mode !== 10
    readonly property bool amMode: entry.mode === 4

    Layout.fillWidth: true
    height: layout.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: current ? Theme.surfaceRaised : Theme.surface
    border.width: 1
    border.color: current ? entry.channelColor : Theme.border

    MouseArea {
        anchors.fill: parent
        onClicked: Session.channels.currentIndex = entry.index
        z: -1
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacingTight

        // ── Intestazione ─────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Rectangle {
                width: 10; height: 10; radius: 5
                color: entry.channelColor
            }

            Text {
                text: entry.label
                font.pixelSize: Theme.fontNormal
                font.bold: true
                color: Theme.textPrimary
            }

            Item { Layout.fillWidth: true }

            // Chiudere il ricevitore da qui, che è dove si guarda l'elenco.
            // Si poteva già fare dal flag sul waterfall, ma solo se il flag
            // era in vista: con lo zoom stretto su un'altra porzione di banda
            // il canale restava aperto e irraggiungibile.
            //
            // L'ultimo non si chiude: una sessione senza ricevitori non
            // demodula niente e non c'è modo di riaprirne uno se non dallo
            // spettro. Il tasto sparisce invece di rifiutare il clic — un
            // comando che non fa niente si preme due volte.
            Rectangle {
                id: closeButton

                visible: Session.channels.count > 1
                implicitWidth: 18
                implicitHeight: 18
                radius: Theme.radiusSmall
                color: closeHover.hovered ? Theme.danger : "transparent"
                border.width: 1
                border.color: closeHover.hovered ? Theme.danger : Theme.border

                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
                }

                Text {
                    anchors.centerIn: parent
                    text: "×"
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    color: closeHover.hovered ? Theme.background : Theme.textSecondary
                }

                HoverHandler {
                    id: closeHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: Session.removeChannel(entry.index)
                }
            }
        }

        // ── Modo, filtro, AGC ────────────────────────────────
        //
        // `ModeSelector` esisteva già e non lo istanziava nessuno: la fusione
        // con la v1.0.0 ha portato la metà «filtri di disturbo» di questa
        // scheda e ha lasciato per strada l'altra. Modo e larghezza erano
        // spariti da **tutti** i backend, non solo da quelli che demodulano a
        // bordo — ma con una radio tradizionale, dove il modo si cambia di
        // continuo, la mancanza si nota subito.
        ModeSelector {
            Layout.fillWidth: true
            channelIndex: entry.index
            mode: entry.mode
            filterLowHz: entry.filterLowHz
            filterHighHz: entry.filterHighHz
        }

        // Wide-FM è l'unico modo che contiene il multiplex RDS. I dati
        // arrivano già dal DSP e sono ruoli del modello del canale; senza
        // questa vista restavano dichiarati ma non venivano mai mostrati.
        RdsPanel {
            Layout.fillWidth: true
            visible: entry.wideFm
            channelIndex: entry.index
            fmStereo: entry.fmStereo
            fmRds: entry.fmRds
            rdsAutomaticAf: entry.rdsAutomaticAf
            rdsRegion: entry.rdsRegion
            rdsSynced: entry.rdsSynced
            rdsCountryCode: entry.rdsCountryCode
            rdsProgramCoverage: entry.rdsProgramCoverage
            rdsReferenceNumber: entry.rdsReferenceNumber
            rdsPi: entry.rdsPi
            rdsCallsign: entry.rdsCallsign
            rdsProgramType: entry.rdsProgramType
            rdsAlternateFrequencies: entry.rdsAlternateFrequencies
            rdsProgramService: entry.rdsProgramService
            rdsRadioText: entry.rdsRadioText
        }

        // ── Spostamento del passa-banda (IF shift, SPEC-003 §7) ──
        //
        // Lo esegue la nostra catena, quindi vale con qualunque sorgente:
        // anche l'audio di una radio tradizionale passa di lì.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            Text {
                text: qsTr("Shift")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                id: shiftSlider
                Layout.fillWidth: true
                from: -2000; to: 2000
                stepSize: 10
                value: entry.passbandShiftHz
                onMoved: Session.setChannelPassbandShift(entry.index, value)
            }

            // Riportare a zero con un cursore è un esercizio di mira: il
            // valore centrale è largo un pixel.
            DsdrButton {
                implicitWidth: 64
                implicitHeight: 22
                text: Math.round(entry.passbandShiftHz) + " Hz"
                enabled: Math.abs(entry.passbandShiftHz) > 1
                onClicked: Session.setChannelPassbandShift(entry.index, 0)
            }
        }

        Text {
            text: qsTr("AGC")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            Repeater {
                model: Session.agcModeNames()

                delegate: DsdrButton {
                    required property int index
                    required property string modelData

                    Layout.fillWidth: true
                    implicitWidth: 0
                    implicitHeight: 24
                    fontSize: Theme.fontSmall
                    boldWhenChecked: false
                    text: modelData
                    checkable: true
                    checked: entry.agcMode === index
                    onClicked: Session.setChannelAgcMode(entry.index, index)
                }
            }
        }


        // ── AGC-T ────────────────────────────────────────────
        //
        // La soglia sotto la quale il guadagno smette di salire: è il comando
        // che «abbassa il rumore di banda» senza toccare il volume. È anche il
        // più trascurato, e per un motivo pratico — va rimesso a mano a ogni
        // cambio di banda, di antenna, di ora del giorno.
        //
        // Per questo c'è AUTO. Con l'automatico acceso il cursore mostra dove
        // la soglia è arrivata e non si tocca: un cursore che non corrisponde a
        // ciò che accade è peggio di nessun cursore.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            Text {
                text: qsTr("AGC-T")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                Layout.preferredWidth: 44
            }

            DsdrButton {
                implicitWidth: 54
                implicitHeight: 22
                fontSize: Theme.fontSmall
                boldWhenChecked: false
                text: qsTr("AUTO")
                checkable: true
                checked: entry.agcAutoThreshold
                onToggled: Session.setChannelAgcAutoThreshold(entry.index, checked)
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: -140; to: -20
                stepSize: 1
                enabled: !entry.agcAutoThreshold
                value: entry.agcThresholdDb
                onMoved: Session.setChannelAgcThreshold(entry.index, value)
            }

            Text {
                text: Math.round(entry.agcThresholdDb) + " dB"
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: entry.agcAutoThreshold ? Theme.accent : Theme.textPrimary
                Layout.preferredWidth: 52
                horizontalAlignment: Text.AlignRight
            }
        }

        Text {
            Layout.fillWidth: true
            visible: entry.agcAutoThreshold
            text: qsTr("Segue il fondo del rumore, sei dB sopra: adesso %1 dBFS")
                  .arg(entry.noiseFloorDb.toFixed(0))
            font.pixelSize: Theme.fontSmall
            color: Theme.textDisabled
        }

        // ── Filtri di disturbo ───────────────────────────────
        //
        // Gli interruttori mancavano del tutto: c'erano i cursori, che
        // comparivano solo a stadio già acceso — e non c'era modo di
        // accenderlo. È l'ultimo pezzo rimasto indietro dalla fusione.
        //
        // Il blanker a banda piena non sta qui e non è una svista: un impulso
        // è dell'ambiente, arriva su tutta la banda campionata e va tolto una
        // volta sola prima che i canali decimino la loro fetta (SPEC-003 §4).
        // Sta nel pannello CATENA RX, dove vale per tutti.
        Text {
            text: qsTr("Disturbi")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                fontSize: Theme.fontSmall
                text: qsTr("NR")
                checkable: true
                checked: entry.nrEnabled
                onToggled: Session.setChannelNoiseReduction(entry.index, checked,
                                                            entry.nrStrength)
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                fontSize: Theme.fontSmall
                text: qsTr("ANF")
                checkable: true
                checked: entry.anfEnabled
                // In CW la nota che si ascolta è una riga fissa, ed è
                // esattamente ciò che l'ANF toglie: il motore lo esclude da
                // sé, e qui si dice invece di lasciar premere a vuoto.
                enabled: entry.modeName.indexOf("CW") !== 0
                onToggled: Session.setChannelAutoNotch(entry.index, checked)
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                fontSize: Theme.fontSmall
                text: qsTr("NOTCH")
                // Un notch si mette dove dà fastidio: il tasto destro sul
                // panadattatore lo pianta lì. Questo lo aggiunge sulla
                // portante, che è il caso in cui si sa già dove sta.
                onClicked: Session.addChannelNotch(entry.index, entry.frequencyHz)
            }
        }

        // Un solo comando per il NR, come vuole la specifica: alza e abbassa
        // il fondo del guadagno. Più in alto si sente meno rumore e più
        // campanellini, ed è l'unica cosa che si giudica a orecchio.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: entry.nrEnabled

            Text {
                text: qsTr("NR")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: 0; to: 10
                stepSize: 1
                value: entry.nrStrength
                onMoved: Session.setChannelNoiseReduction(entry.index, true, value)
            }

            Text {
                text: Math.round(entry.nrStrength)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                Layout.preferredWidth: 20
            }
        }

        // In CW la nota che si ascolta è una riga fissa, ed è esattamente ciò
        // che l'ANF toglie: il motore lo esclude da sé, e qui lo si dice.
        Text {
            Layout.fillWidth: true
            visible: entry.anfEnabled && entry.modeName.indexOf("CW") === 0
            text: qsTr("In CW il notch automatico resta escluso.")
            font.pixelSize: Theme.fontSmall
            color: Theme.warning
            wrapMode: Text.WordWrap
        }

        // ── Filtro di picco, solo in CW ──────────────────────
        //
        // Una campana stretta sulla nota che si sta copiando: il segnale
        // emerge e tutto ciò che non è alla frequenza giusta si allontana. Su
        // una voce suonerebbe come un telefono, e infatti compare solo in CW.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: entry.modeName.indexOf("CW") === 0

            DsdrButton {
                text: qsTr("APF")
                implicitWidth: 56
                implicitHeight: 24
                checkable: true
                checked: entry.apfEnabled
                onToggled: Session.setChannelPeakFilter(entry.index, checked, entry.apfQ)
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: 5; to: 50
                enabled: entry.apfEnabled
                value: entry.apfQ
                onMoved: Session.setChannelPeakFilter(entry.index, true, value)
            }

            Text {
                text: qsTr("Q %1").arg(Math.round(entry.apfQ))
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: entry.apfEnabled ? Theme.textPrimary : Theme.textDisabled
                Layout.preferredWidth: 42
            }

            // Due stazioni che si accavallano smettono di stare nello stesso
            // punto: è il trucco più economico che esista in un pile-up.
            DsdrButton {
                text: qsTr("BIN")
                implicitWidth: 52
                implicitHeight: 24
                checkable: true
                checked: entry.binauralCw
                onToggled: Session.setChannelBinaural(entry.index, checked)
            }
        }

        // ── Banda laterale della AM sincrona ─────────────────
        //
        // L'interferenza adiacente di solito sta da un lato solo, e in AM le
        // due bande portano la stessa informazione: buttarne via una non
        // costa nulla in fedeltà e toglie il disturbo.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: entry.modeName === "SAM"

            Text {
                text: qsTr("SAM")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            Repeater {
                model: [qsTr("DSB"), qsTr("LSB"), qsTr("USB")]

                delegate: DsdrButton {
                    required property int index
                    required property string modelData

                    Layout.fillWidth: true
                    implicitWidth: 0
                    implicitHeight: 24
                    text: modelData
                    checkable: true
                    checked: entry.samSideband === index
                    onClicked: Session.setChannelSamSideband(entry.index, index)
                }
            }
        }

        // ── Notch messi a mano ───────────────────────────────
        //
        // Stanno su una frequenza RF, non su un tono audio: restano sul
        // disturbo anche quando ci si sintonizza altrove. Si mettono col
        // tasto destro sullo spettro, dove il fischio si vede.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: entry.notches.length > 0

            Text {
                text: qsTr("Notch")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            Repeater {
                model: entry.notches

                delegate: RowLayout {
                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacingTight

                    Text {
                        Layout.fillWidth: true
                        text: (modelData.frequencyHz / 1e6).toFixed(6) + " MHz  ·  "
                              + Math.round(modelData.widthHz) + " Hz"
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.monoFamily
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }

                    DsdrButton {
                        text: "×"
                        implicitWidth: 26
                        implicitHeight: 20
                        onClicked: Session.removeChannelNotch(entry.index, index)
                    }
                }
            }
        }

        Binding {
            target: shiftSlider
            property: "value"
            value: entry.passbandShiftHz
            when: !shiftSlider.pressed
            restoreMode: Binding.RestoreNone
        }
    }
}
