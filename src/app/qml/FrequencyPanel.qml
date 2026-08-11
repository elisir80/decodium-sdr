// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello di sintonia: bande, inserimento diretto, riferimenti.
//
// Il display a cifre serve a muoversi *dentro* una banda; questo pannello
// serve a **scegliere** dove andare. Sono due gesti diversi e meritano due
// controlli diversi.
//
// Il bandstack è il comportamento che ogni operatore si aspetta: ogni banda
// ricorda dove l'avevi lasciata, così tornarci non significa ripartire dal
// bordo inferiore, che di solito è vuoto.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("SINTONIA")

    /// Bandstack e memorie stanno nel singleton `Memories`: le stesse cose si
    /// comandano dalla targa del ricevitore, e due archivi separati sono due
    /// archivi che divergono al primo salvataggio.
    readonly property var favorites: Memories.entries
    readonly property var currentBand: Memories.currentBand

    function reachable(hz) { return Memories.reachable(hz) }
    function goTo(hz) { return Memories.goTo(hz) }
    function addFavorite() { Memories.storeCurrent() }
    function removeFavorite(hz) { Memories.remove(hz) }
    function selectBand(band) { Memories.selectBand(band) }

    // ── Bande ────────────────────────────────────────────────────────────
    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: Theme.spacingTight
        rowSpacing: Theme.spacingTight

        Repeater {
            model: BandPlan.bands

            delegate: DsdrButton {
                required property var modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 26
                text: modelData.name

                // Una banda che il ricevitore non copre non si mostra
                // accesa: la copertura viene dalle capability, non dal nome
                // del backend (CONSTITUTION §7).
                enabled: Session.connected && root.reachable(modelData.home)
                checked: root.currentBand && root.currentBand.name === modelData.name

                onClicked: root.selectBand(modelData)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            text: Session.scanning ? qsTr("■ Stop scanner") : qsTr("Scanner banda")
            checkable: true
            checked: Session.scanning
            enabled: Session.connected && root.currentBand !== null
            onClicked: {
                if (Session.scanning) {
                    Session.stopScan()
                    return
                }
                const band = root.currentBand
                const step = band.name === "FM 88–108" ? 100000
                           : band.name === "6m" ? 25000 : 5000
                Session.startScan(band.start, band.end, step, 350)
            }
        }

        Text {
            text: Session.scanResults.length > 0
                  ? qsTr("%1 segnali").arg(Session.scanResults.length)
                  : qsTr("soglia S-meter −75 dBFS")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            Layout.fillWidth: true
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: Session.scanResults.length > 0

        Repeater {
            model: Session.scanResults

            delegate: DsdrButton {
                required property var modelData
                text: (modelData.frequencyHz / 1e6).toFixed(6) + " MHz"
                      + " (" + Math.round(modelData.signalDb) + " dB)"
                implicitHeight: 22
                onClicked: Session.setChannelFrequency(
                               Session.channels.currentIndex, modelData.frequencyHz)
            }
        }
    }

    // ── Inserimento diretto ──────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        TextField {
            id: entry

            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            enabled: Session.connected
            placeholderText: qsTr("14.225   ·   14225   ·   7.100.000")
            color: Theme.textPrimary
            placeholderTextColor: Theme.textDisabled
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            selectByMouse: true

            property bool invalid: false

            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: entry.invalid ? Theme.danger
                            : entry.activeFocus ? Theme.accent : Theme.border

                Behavior on border.color {
                    ColorAnimation { duration: Theme.animationFast }
                }
            }

            onTextChanged: invalid = false

            onAccepted: {
                const hz = BandPlan.parseFrequency(text)
                // Un campo che si svuota da solo dopo un errore fa perdere
                // ciò che si era scritto: si segnala e si lascia correggere.
                if (hz <= 0 || !root.goTo(hz)) {
                    invalid = true
                    return
                }
                text = ""
            }
        }

        DsdrButton {
            text: qsTr("Vai")
            implicitWidth: 56
            enabled: Session.connected && entry.text.trim().length > 0
            onClicked: entry.accepted()
        }

        DsdrButton {
            text: qsTr("★")
            implicitWidth: 36
            enabled: Session.connected
            onClicked: root.addFavorite()
        }
    }

    Text {
        Layout.fillWidth: true
        visible: entry.invalid
        text: qsTr("Frequenza non valida o fuori dalla copertura del ricevitore.")
        font.pixelSize: Theme.fontSmall
        color: Theme.danger
        wrapMode: Text.WordWrap
    }

    // ── Frequenze memorizzate ───────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        visible: root.favorites.length > 0

        Text {
            text: qsTr("Memorie")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingTight

            Repeater {
                model: root.favorites

                delegate: Row {
                    required property var modelData
                    spacing: 2

                    DsdrButton {
                        text: modelData.label
                        implicitHeight: 22
                        onClicked: root.goTo(modelData.frequency)
                    }

                    DsdrButton {
                        text: "×"
                        implicitWidth: 22
                        implicitHeight: 22
                        onClicked: root.removeFavorite(modelData.frequency)
                    }
                }
            }
        }
    }

    // ── Riferimenti ──────────────────────────────────────────────────────
    // Le emissioni campione servono a capire se antenna e catena di ricezione
    // funzionano davvero, prima di dare la colpa al software.
    //
    // Griglia a colonne fisse e non Flow: un Flow con figli che si
    // dimensionano sul proprio testo entra in un ciclo di polish — il
    // contenitore misura i figli, i figli chiedono di rimisurare il
    // contenitore — e Qt lo segnala centinaia di volte al secondo.
    GridLayout {
        Layout.fillWidth: true
        columns: 3
        columnSpacing: Theme.spacingTight
        rowSpacing: Theme.spacingTight

        Repeater {
            model: BandPlan.references

            delegate: DsdrButton {
                required property var modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 22
                text: modelData.name
                enabled: Session.connected && root.reachable(modelData.frequency)
                onClicked: root.goTo(modelData.frequency)
            }
        }
    }
}
