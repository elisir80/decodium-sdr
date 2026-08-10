// SPDX-License-Identifier: GPL-3.0-or-later
// Il comando della riduzione neurale (DSDR-IMPL-001 §7.1).
//
// Tre cose e non una di più: se è accesa, quanto forte, e con quale modello.
// Più il distintivo dello stato, che è la parte a cui serve davvero
// un'occhiata — perché «Degraded» vuol dire che la macchina non ce la fa, e
// senza vederlo si crede che la riduzione sia accesa mentre non lo è.
//
// Compare solo se questa compilazione ha un motore: un interruttore che non
// può fare niente è peggio di un interruttore assente (CONSTITUTION §7).
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

ColumnLayout {
    id: root

    spacing: Theme.spacingTight
    visible: Session.neuralAvailable

    /// Le scelte sopravvivono alla chiusura: rifarle a ogni avvio sarebbe un
    /// rito, e ci si accorgerebbe di averle dimenticate ascoltando.
    property bool savedEnabled: false
    property real savedIntensity: 100
    property string savedModel: ""

    Settings {
        category: "neural-nr"
        property alias enabled: root.savedEnabled
        property alias intensity: root.savedIntensity
        property alias model: root.savedModel
    }

    Component.onCompleted: {
        Session.neuralIntensity = savedIntensity
        if (savedEnabled)
            Session.setNeuralNr(true)
    }

    // ── Interruttore e stato ─────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            implicitWidth: 96
            implicitHeight: 26
            fontSize: Theme.fontSmall
            text: qsTr("DECODIUM NR")
            checkable: true
            checked: Session.neuralEnabled
            enabled: Session.connected
            onToggled: {
                Session.setNeuralNr(checked)
                root.savedEnabled = checked
            }
        }

        // Il distintivo: verde a regime, giallo mentre sale, rosso quando si
        // è arreso. È l'unico posto in cui si legge che lo stadio ha smesso
        // di lavorare da solo.
        Rectangle {
            implicitWidth: badge.implicitWidth + 2 * Theme.spacing
            implicitHeight: 20
            radius: Theme.radiusSmall
            visible: Session.neuralState !== "Bypass"
            color: {
                if (Session.neuralState === "Degraded")
                    return Theme.danger
                if (Session.neuralState === "Warmup")
                    return Theme.warning
                return Theme.accent
            }

            Text {
                id: badge
                anchors.centerIn: parent
                text: Session.neuralState
                font.pixelSize: Theme.fontSmall
                font.bold: true
                color: Theme.background
            }
        }

        Item { Layout.fillWidth: true }

        Text {
            // Latenza e costo misurati, non promessi: sono i due numeri che
            // dicono se questo stadio si può tenere acceso.
            text: qsTr("%1 ms · %2 %")
                  .arg(Session.neuralLatencyMs.toFixed(0))
                  .arg((Session.neuralLoad * 100).toFixed(0))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.neuralLoad > 0.7 ? Theme.warning : Theme.textDisabled
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Session.neuralState === "Degraded"
        text: qsTr("CPU insufficiente per Decodium NR: torna da sé fra trenta secondi.")
        font.pixelSize: Theme.fontSmall
        color: Theme.danger
        wrapMode: Text.WordWrap
    }

    // ── Intensità ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: qsTr("Intensità")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: 0; to: 100
            stepSize: 5
            value: Session.neuralIntensity
            onMoved: {
                Session.neuralIntensity = value
                root.savedIntensity = value
            }
        }

        Text {
            text: Math.round(Session.neuralIntensity) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 46
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Modello ──────────────────────────────────────────────────────────
    //
    // L'elenco può essere vuoto, ed è il caso normale: RNNoise ha i pesi
    // dentro di sé. Il selettore compare quando c'è qualcosa da scegliere.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: Session.neuralModels.count > 0

        Text {
            text: qsTr("Modello")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrComboBox {
            Layout.fillWidth: true
            model: Session.neuralModels
            textRole: "name"
            onActivated: function (index) {
                root.savedModel = Session.neuralModels.pathAt(index)
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Session.neuralModels.count === 0
        // Il percorso si dice, non si nasconde: chi ha un modello da mettere
        // deve poterlo leggere, e una cartella dentro i dati applicativi è il
        // posto giusto per starci e quello sbagliato per trovarla.
        text: qsTr("Nessun modello: RNNoise ha i pesi dentro di sé. Per DeepFilterNet, metti il file in %1")
              .arg(Session.neuralModels.directory)
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // Il confine del §8.3, detto dove si accende l'interruttore.
    Text {
        Layout.fillWidth: true
        visible: Session.neuralEnabled
        text: qsTr("Agisce solo sull'ascolto: il flusso verso i decoder resta lineare.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
