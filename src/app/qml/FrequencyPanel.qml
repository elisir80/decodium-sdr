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

    /// Dove si era rimasti su ogni banda. In memoria per ora: la persistenza
    /// arriva con il SettingsStore, e allora questa mappa diventerà il suo
    /// contenuto invece di sparire alla chiusura.
    property var bandStack: ({})

    readonly property var currentBand: BandPlan.bandAt(Session.centerFrequency)

    function reachable(hz) {
        const caps = Session.capabilities
        if (caps.maxFrequency <= caps.minFrequency)
            return true       // copertura non dichiarata: non si vincola nulla
        return hz >= caps.minFrequency && hz <= caps.maxFrequency
    }

    function goTo(hz) {
        if (!Session.connected)
            return false
        if (!reachable(hz))
            return false

        // Prima di andarsene si annota dove si era: è ciò che rende utile
        // tornare indietro.
        const leaving = BandPlan.bandAt(Session.centerFrequency)
        if (leaving)
            bandStack[leaving.name] = Session.centerFrequency

        Session.centerFrequency = Math.round(hz)
        return true
    }

    function selectBand(band) {
        // Premere la banda in cui si è già riporta al punto di partenza:
        // è la via d'uscita quando ci si è persi in fondo alla banda.
        const target = (currentBand && currentBand.name === band.name)
            ? band.home
            : (bandStack[band.name] !== undefined ? bandStack[band.name] : band.home)
        goTo(target)
    }

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
    }

    Text {
        Layout.fillWidth: true
        visible: entry.invalid
        text: qsTr("Frequenza non valida o fuori dalla copertura del ricevitore.")
        font.pixelSize: Theme.fontSmall
        color: Theme.danger
        wrapMode: Text.WordWrap
    }

    // ── Riferimenti ──────────────────────────────────────────────────────
    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: BandPlan.references

            delegate: DsdrButton {
                required property var modelData

                implicitWidth: refText.implicitWidth + 2 * Theme.spacing
                implicitHeight: 22
                text: modelData.name
                enabled: Session.connected && root.reachable(modelData.frequency)
                onClicked: root.goTo(modelData.frequency)

                // Le emissioni campione servono a capire se l'antenna e la
                // catena funzionano davvero, prima di dare la colpa al software.
                TextMetrics {
                    id: refText
                    font.pixelSize: Theme.fontSmall
                    text: modelData.name
                }
            }
        }
    }
}
