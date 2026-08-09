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
    required property bool notchEnabled
    required property real notchFrequencyHz
    required property real notchWidthHz

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

            DsdrButton {
                text: entry.muted ? qsTr("Muto") : qsTr("Attivo")
                implicitWidth: 66
                checkable: true
                checked: entry.muted
                danger: entry.muted
                onToggled: Session.setChannelMuted(entry.index, checked)
            }

            DsdrButton {
                text: "×"
                implicitWidth: 28
                enabled: Session.channels.count > 1
                onClicked: Session.removeChannel(entry.index)
            }
        }

        // ── Frequenza ────────────────────────────────────────
        Text {
            text: (entry.frequencyHz / 1e6).toFixed(6) + " MHz"
            font.pixelSize: Theme.fontLarge
            font.family: Theme.monoFamily
            color: entry.current ? Theme.accent : Theme.textPrimary
            Layout.fillWidth: true

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeVerCursor
                onWheel: (wheel) => {
                    const step = wheel.modifiers & Qt.ControlModifier ? 1000
                               : wheel.modifiers & Qt.ShiftModifier ? 10 : 100
                    Session.nudgeChannel(entry.index,
                                         wheel.angleDelta.y > 0 ? step : -step)
                }
            }
        }

        SignalMeter {
            Layout.fillWidth: true
            levelDb: entry.signalDb
        }

        SignalMeter {
            Layout.fillWidth: true
            levelDb: entry.audioLevelDb
            floorDb: -60
            ceilingDb: 0
            showSUnits: false
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("RF ") + Math.round(entry.signalDb) + " dBFS · "
                  + qsTr("fondo ") + Math.round(entry.noiseFloorDb) + " dBFS · "
                  + qsTr("SNR ") + Math.round(entry.snrDb) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }

        // ── Modo e AGC ───────────────────────────────────────
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacingTight

            Text {
                text: qsTr("Modo")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            Text {
                text: qsTr("AGC")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.mode !== 10
            }

            DsdrComboBox {
                Layout.fillWidth: true
                model: Session.modeNames()
                currentIndex: entry.mode
                // Solo se la demodulazione è del client il modo è
                // davvero nostro; con DSP a bordo lo decide la radio.
                enabled: Session.capabilities.clientDemod
                onActivated: Session.setChannelMode(entry.index, currentIndex)
            }

            DsdrComboBox {
                Layout.fillWidth: true
                model: Session.agcModeNames()
                currentIndex: entry.agcMode
                visible: entry.mode !== 10
                enabled: Session.capabilities.clientAgc
                onActivated: Session.setChannelAgcMode(entry.index, currentIndex)
            }
        }

        // ── AGC-T ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: Session.capabilities.clientAgc && entry.agcMode !== 0
                     && entry.mode !== 10

            Text {
                text: qsTr("AGC-T")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: -130; to: -30
                value: entry.agcThresholdDb
                onMoved: Session.setChannelAgcThreshold(entry.index, value)
            }

            Text {
                text: Math.round(entry.agcThresholdDb) + " dB"
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
                Layout.preferredWidth: 52
            }
        }

        // SDR++ espone attacco e decadimento del rilevatore AGC
        // per AM/SSB/CW; qui sono disponibili per ogni modo con
        // AGC client, mantenendo "Auto" come preset originale.
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacingTight
            visible: Session.capabilities.clientAgc
                     && entry.agcMode !== 0 && entry.mode !== 10

            Text {
                text: qsTr("AGC attacco")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                DsdrSlider {
                    Layout.fillWidth: true
                    from: 0.5; to: 500; stepSize: 0.5
                    value: entry.agcAttackMs
                    onMoved: Session.setChannelAgcAttack(entry.index, value)
                }

                Text {
                    text: Number(entry.agcAttackMs).toFixed(1) + " ms"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }
            }

            Text {
                text: qsTr("AGC decadimento")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                DsdrSlider {
                    Layout.fillWidth: true
                    from: 0; to: 3000; stepSize: 10
                    value: entry.agcDecayMs
                    onMoved: Session.setChannelAgcDecay(entry.index, value)
                }

                Text {
                    text: entry.agcDecayMs <= 0 ? qsTr("Auto")
                                                : Math.round(entry.agcDecayMs) + " ms"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }
            }
        }

        // Passa-alto AF analogo al controllo radio di SDR++.
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacingTight
            visible: entry.highPassAllowed && Session.capabilities.clientDemod

            Text {
                text: qsTr("Audio high-pass")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                DsdrButton {
                    text: entry.audioHighPassEnabled ? qsTr("On") : qsTr("Off")
                    checkable: true
                    checked: entry.audioHighPassEnabled
                    onToggled: Session.setChannelAudioHighPassEnabled(
                                   entry.index, checked)
                }

                DsdrSlider {
                    Layout.fillWidth: true
                    from: 20; to: 1000; stepSize: 10
                    value: entry.audioHighPassHz
                    enabled: entry.audioHighPassEnabled
                    onMoved: Session.setChannelAudioHighPassHz(entry.index, value)
                }

                Text {
                    text: Math.round(entry.audioHighPassHz) + " Hz"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: entry.amMode && Session.capabilities.clientDemod

            Text {
                text: qsTr("AM carrier AGC")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrButton {
                text: entry.amCarrierAgc ? qsTr("On") : qsTr("Off")
                checkable: true
                checked: entry.amCarrierAgc
                onToggled: Session.setChannelAmCarrierAgc(entry.index, checked)
            }
        }

        // ── Filtro ───────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: Session.capabilities.clientDemod && entry.mode !== 10

            Text {
                text: qsTr("Filtro")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: entry.wideFm ? -120000 : -6000
                to: entry.wideFm ? 120000 : 6000
                stepSize: entry.wideFm ? 500 : 50
                value: entry.filterLowHz
                onMoved: Session.setChannelFilter(entry.index, value, entry.filterHighHz)
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: entry.wideFm ? -120000 : -6000
                to: entry.wideFm ? 120000 : 6000
                stepSize: entry.wideFm ? 500 : 50
                value: entry.filterHighHz
                onMoved: Session.setChannelFilter(entry.index, entry.filterLowHz, value)
            }
        }

        // ── Catena radio FM ─────────────────────────────────
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacingTight
            visible: (entry.fmMode || entry.squelchAllowed)
                     && Session.capabilities.clientDemod

            Text {
                text: qsTr("FM audio")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.fmMode
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.fmMode

                DsdrButton {
                    text: qsTr("Stereo")
                    checkable: true
                    checked: entry.fmStereo
                    enabled: entry.wideFm
                    onToggled: Session.setChannelFmStereo(entry.index, checked)
                }

                DsdrButton {
                    text: qsTr("Low pass")
                    checkable: true
                    checked: entry.fmAudioLowPass
                    onToggled: Session.setChannelFmAudioLowPass(
                                   entry.index, checked)
                }

                DsdrComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Off"), qsTr("22 µs"), qsTr("50 µs"),
                            qsTr("75 µs")]
                    currentIndex: entry.fmDeemphasisUs === 0 ? 0
                                : entry.fmDeemphasisUs === 22 ? 1
                                : entry.fmDeemphasisUs === 75 ? 3 : 2
                    onActivated: Session.setChannelFmDeemphasis(
                        entry.index, currentIndex === 0 ? 0
                                                      : currentIndex === 1 ? 22
                                                      : currentIndex === 3 ? 75 : 50)
                }
            }

            Text {
                text: qsTr("RDS")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.wideFm
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.wideFm

                DsdrButton {
                    text: entry.fmRds ? qsTr("On") : qsTr("Off")
                    checkable: true
                    checked: entry.fmRds
                    onToggled: Session.setChannelFmRds(entry.index, checked)
                }

                DsdrButton {
                    text: qsTr("AF auto")
                    checkable: true
                    checked: entry.rdsAutomaticAf
                    enabled: entry.fmRds
                    onToggled: Session.setChannelRdsAutomaticAf(
                                   entry.index, checked)
                }

                DsdrComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Europe"), qsTr("North America")]
                    currentIndex: entry.rdsRegion
                    onActivated: Session.setChannelRdsRegion(entry.index, currentIndex)
                }

                Text {
                    text: entry.rdsSynced
                          ? (entry.rdsProgramService.length > 0
                             ? entry.rdsProgramService + " · " + entry.rdsPi
                               + (entry.rdsCallsign.length > 0
                                  ? " (" + entry.rdsCallsign + ")" : "")
                               + " · " + entry.rdsProgramType
                             : qsTr("PI %1 · %2")
                                 .arg(entry.rdsPi).arg(entry.rdsProgramType))
                          : qsTr("nessun sync")
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: entry.rdsSynced ? Theme.success : Theme.textDisabled
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            Text {
                text: qsTr("RDS") + " CC " + entry.rdsCountryCode
                      + " · " + qsTr("copertura") + " "
                      + entry.rdsProgramCoverage + " · Ref "
                      + entry.rdsReferenceNumber
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
                Layout.columnSpan: 2
                visible: entry.wideFm && entry.rdsSynced
            }

            Text {
                text: qsTr("RadioText")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.wideFm && entry.rdsRadioText.length > 0
            }

            Text {
                text: entry.rdsRadioText
                font.pixelSize: Theme.fontSmall
                color: Theme.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: entry.wideFm && entry.rdsRadioText.length > 0
            }

            Text {
                text: qsTr("AF")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.wideFm && entry.rdsAlternateFrequencies.length > 0
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.wideFm && entry.rdsAlternateFrequencies.length > 0

                Text {
                    text: entry.rdsAlternateFrequencies
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                DsdrButton {
                    text: qsTr("AF →")
                    onClicked: Session.followRdsAf(entry.index)
                }
            }

            Text {
                text: qsTr("Squelch")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.squelchAllowed
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.squelchAllowed

                DsdrButton {
                    text: entry.squelchEnabled ? qsTr("On") : qsTr("Off")
                    checkable: true
                    checked: entry.squelchEnabled
                    onToggled: Session.setChannelSquelchEnabled(entry.index, checked)
                }

                DsdrSlider {
                    Layout.fillWidth: true
                    from: -130; to: -20
                    value: entry.squelchThresholdDb
                    onMoved: Session.setChannelSquelchThreshold(entry.index, value)
                }

                Text {
                    text: Math.round(entry.squelchThresholdDb) + " dB"
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                }
            }

            Text {
                text: qsTr("CTCSS")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.mode === 7
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.mode === 7

                DsdrButton {
                    text: entry.ctcssEnabled ? qsTr("On") : qsTr("Off")
                    checkable: true
                    checked: entry.ctcssEnabled
                    onToggled: Session.setChannelCtcssEnabled(entry.index, checked)
                }

                DsdrComboBox {
                    implicitWidth: 90
                    model: [qsTr("Mute"), qsTr("Decode")]
                    currentIndex: entry.ctcssDecodeOnly ? 1 : 0
                    onActivated: Session.setChannelCtcssDecodeOnly(
                        entry.index, currentIndex === 1)
                }

                DsdrComboBox {
                    Layout.fillWidth: true
                    model: entry.ctcssTones.map(t => t.toFixed(1) + " Hz")
                    currentIndex: {
                        let nearest = 0
                        let distance = Math.abs(entry.ctcssTones[0] - entry.ctcssToneHz)
                        for (let i = 1; i < entry.ctcssTones.length; ++i) {
                            const candidate = Math.abs(entry.ctcssTones[i] - entry.ctcssToneHz)
                            if (candidate < distance) {
                                nearest = i
                                distance = candidate
                            }
                        }
                        return nearest
                    }
                    onActivated: Session.setChannelCtcssTone(
                        entry.index, entry.ctcssTones[currentIndex])
                }
            }

            Text {
                text: qsTr("IF noise reduction")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                visible: entry.fmMode
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: entry.fmMode

                DsdrButton {
                    text: entry.fmIfNoiseReductionEnabled ? qsTr("On") : qsTr("Off")
                    checkable: true
                    checked: entry.fmIfNoiseReductionEnabled
                    onToggled: Session.setChannelFmIfNoiseReductionEnabled(
                                   entry.index, checked)
                }

                DsdrComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Voice"), qsTr("Narrow band"), qsTr("Broadcast")]
                    currentIndex: entry.fmIfNoiseReductionPreset
                    onActivated: Session.setChannelFmIfNoiseReductionPreset(
                                     entry.index, currentIndex)
                }
            }
        }

        Text {
            text: entry.mode === 10
                  ? qsTr("IQ / RAW · banda piena")
                  : entry.filterLowHz + " … " + entry.filterHighHz + " Hz    "
                    + qsTr("guadagno AGC ") + Math.round(entry.agcGainDb) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textDisabled
        }

        // ── Volume ───────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: qsTr("Vol")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: 0; to: 1
                value: entry.volume
                accentColor: entry.channelColor
                onMoved: Session.setChannelVolume(entry.index, value)
            }
        }

        // ── Disturbi per canale (SPEC-003 §5) ────────────────
        //
        // Il blanker non è qui: toglie impulsi che arrivano su tutta la banda,
        // quindi vive nella catena e non nel canale — pannello «CATENA RX».
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: Session.capabilities.clientDemod

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
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
                text: qsTr("ANF")
                checkable: true
                checked: entry.anfEnabled
                onToggled: Session.setChannelAutoNotch(entry.index, checked)
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 24
                text: qsTr("NOTCH")
                checkable: true
                checked: entry.notchEnabled
                onToggled: Session.setChannelNotch(entry.index, checked,
                                                   entry.notchFrequencyHz,
                                                   entry.notchWidthHz)
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

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: entry.notchEnabled

            Text {
                text: qsTr("Notch")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }

            DsdrSlider {
                Layout.fillWidth: true
                from: 200; to: 3000
                value: entry.notchFrequencyHz
                onMoved: Session.setChannelNotch(entry.index, true, value,
                                                 entry.notchWidthHz)
            }

            Text {
                text: Math.round(entry.notchFrequencyHz) + " Hz"
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                Layout.preferredWidth: 62
            }
        }

        // ── Spostamento del passa-banda (IF shift, SPEC-003 §7) ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: Session.capabilities.clientDemod

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

        Binding {
            target: shiftSlider
            property: "value"
            value: entry.passbandShiftHz
            when: !shiftSlider.pressed
            restoreMode: Binding.RestoreNone
        }
    }
}
