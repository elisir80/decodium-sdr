// SPDX-License-Identifier: GPL-3.0-or-later
// Targa del canale attivo, sopra lo spettro.
//
// Frequenza, livello, modo, filtro e AGC stavano in fondo alla colonna
// laterale: lontani dal punto in cui si guarda mentre si sintonizza. Chi opera
// tiene gli occhi sul segnale, non sul bordo destro della finestra.
//
// È il delegate del canale corrente, non una copia dei suoi dati: si aggiorna
// da sé quando il modello cambia, senza che nessuno debba ricordarsi di
// tenerlo allineato.
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    required property int index
    required property color channelColor
    required property string label
    required property real frequencyHz
    required property string modeName
    required property int filterLowHz
    required property int filterHighHz
    required property real signalDb
    required property int agcMode
    required property bool muted

    /// Larghezza del filtro: è il numero che si legge, non i due estremi.
    readonly property int filterWidthHz: Math.max(0, filterHighHz - filterLowHz)

    /// Nome dell'AGC. Il modello espone il numero del modo, non la sua
    /// etichetta; la tabella dei nomi la tiene la sessione, tradotta.
    readonly property string agcName: {
        const names = Session.agcModeNames()
        return (agcMode >= 0 && agcMode < names.length) ? names[agcMode] : ""
    }

    implicitWidth: row.implicitWidth + 2 * Theme.spacingLoose
    implicitHeight: row.implicitHeight + 2 * Theme.spacing

    radius: Theme.radius
    // Opaca, non velata: sotto scorre lo spettro. La lezione è già stata
    // imparata una volta con i comandi del waterfall.
    color: Theme.surface
    border.width: 1
    border.color: root.channelColor

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingLoose

        // ── Etichetta del canale ─────────────────────────────────────────
        Rectangle {
            implicitWidth: tag.implicitWidth + 2 * Theme.spacing
            implicitHeight: tag.implicitHeight + 4
            radius: Theme.radiusSmall
            color: root.channelColor

            Text {
                id: tag
                anchors.centerIn: parent
                text: root.label
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.letterSpacing: 1
                // Il testo sta sopra il colore del canale, che è chiaro: qui
                // il nero è il contrasto, non una tinta del tema.
                color: Theme.surfaceSunken
            }
        }

        // ── Frequenza ────────────────────────────────────────────────────
        FrequencyDisplay {
            frequencyHz: root.frequencyHz
            minimumHz: Session.capabilities.minFrequency
            maximumHz: Session.capabilities.maxFrequency > 0
                       ? Session.capabilities.maxFrequency : 30000000
            editable: Session.connected
            digitSize: 26
            onTuneRequested: (hz) => Session.setChannelFrequency(root.index, hz)
        }

        // ── Livello ──────────────────────────────────────────────────────
        ColumnLayout {
            spacing: 1

            SignalMeter {
                Layout.preferredWidth: 132
                Layout.preferredHeight: 18
                levelDb: root.signalDb
            }
        }

        // ── Passo di sintonia ────────────────────────────────────────────
        //
        // Era cablato nel codice della rotellina, e l'unico modo di sapere di
        // quanto ci si stesse muovendo era muoversi e guardare.
        RowLayout {
            spacing: 2

            Text {
                text: qsTr("STEP")
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }

            DsdrButton {
                text: "◀"
                implicitWidth: 22
                implicitHeight: 22
                enabled: Tuning.stepHz > Tuning.steps[0]
                onClicked: Tuning.shift(-1)
            }

            Text {
                text: Tuning.label(Tuning.stepHz)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.accent
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 30
            }

            DsdrButton {
                text: "▶"
                implicitWidth: 22
                implicitHeight: 22
                enabled: Tuning.stepHz < Tuning.steps[Tuning.steps.length - 1]
                onClicked: Tuning.shift(1)
            }
        }

        // ── Silenzia ─────────────────────────────────────────────────────
        //
        // Nella targa e non solo nel pannello: si silenzia un canale mentre si
        // ascolta, cioè guardando lo spettro, non la colonna laterale.
        DsdrButton {
            text: root.muted ? qsTr("MUTO") : qsTr("AUDIO")
            implicitWidth: 62
            implicitHeight: 24
            checkable: true
            checked: root.muted
            danger: root.muted
            onToggled: Session.setChannelMuted(root.index, checked)
        }

        // ── Modo, filtro, AGC ────────────────────────────────────────────
        //
        // Sola lettura: si regolano nel pannello del canale. Qui servono a
        // sapere in che stato si sta ascoltando, che è la domanda a cui si
        // risponde con un'occhiata.
        Repeater {
            model: [
                root.modeName,
                root.filterWidthHz >= 1000
                    ? qsTr("%1 kHz").arg((root.filterWidthHz / 1000).toFixed(1))
                    : qsTr("%1 Hz").arg(root.filterWidthHz),
                qsTr("AGC %1").arg(root.agcName),
            ]

            delegate: Rectangle {
                required property string modelData

                implicitWidth: chip.implicitWidth + 2 * Theme.spacing
                implicitHeight: chip.implicitHeight + 4
                radius: Theme.radiusSmall
                color: "transparent"
                border.width: 1
                border.color: Theme.border

                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: parent.modelData
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }
            }
        }
    }
}
