// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello del ColibriNANO: guadagno e calibrazione delle bande laterali.
//
// Sul ColibriNANO preamplificatore e attenuatore sono la stessa manopola: un
// solo valore fra −31,5 e +6 dB. Mostrarli come due controlli separati
// sarebbe più fedele ad altre radio, ma non a questa.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("COLIBRINANO")

    readonly property var preampRange: Session.nativeCommand("colibri.preampRange", {}) || ({})
    readonly property var health: Session.nativeCommand("colibri.health", {}) || ({})

    property real preampDb: 0

    // ── Guadagno ─────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("Guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: root.preampRange.min !== undefined ? root.preampRange.min : -31.5
            to: root.preampRange.max !== undefined ? root.preampRange.max : 6
            stepSize: 0.5
            value: root.preampDb
            onMoved: {
                root.preampDb = value
                Session.nativeCommand("colibri.setPreamp", { "db": value })
            }
        }

        Text {
            text: (root.preampDb >= 0 ? "+" : "") + root.preampDb.toFixed(1) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 58
            horizontalAlignment: Text.AlignRight
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Sotto lo zero attenua, sopra amplifica: è un'unica manopola.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── Sovraccarico dell'ADC ────────────────────────────────────────────
    // È l'unica telemetria che il device offre, e viaggia su ogni blocco di
    // campioni. Vale mostrarla: un ADC in saturazione produce prodotti di
    // intermodulazione che sembrano stazioni.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        visible: root.health.adcOverload === true

        Rectangle {
            width: 8; height: 8; radius: 4
            color: Theme.danger
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("ADC in sovraccarico — riduci il guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.danger
            wrapMode: Text.WordWrap
        }
    }

    // La calibrazione delle bande laterali non è più un comando: il
    // ColibriNANO consegna il flusso con la convenzione di segno opposta alla
    // nostra, sempre, e il backend lo coniuga sempre. Finché non si sapeva da
    // che parte stesse il vero era un interruttore; ora che si sa — provato
    // sull'hardware — lasciarlo vorrebbe dire offrire una posizione sbagliata
    // a chi lo trova girato dalla parte errata.
}
