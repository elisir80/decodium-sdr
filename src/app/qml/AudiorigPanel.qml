// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello della radio tradizionale: da dove arriva l'audio, e come va il CAT.
//
// Sono le due sole cose che possono andare storte in questo backend, e vanno
// storte in modo silenzioso: un ingresso audio sbagliato fa ascoltare la
// stanza invece della banda, e un CAT che non risponde lascia il
// panadattatore ancorato a una frequenza che la radio non ha più.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("RADIO")

    // I comandi nativi si rileggono a ogni cambio di stato della sessione: la
    // frequenza arriva dal CAT cinque volte al secondo, e legarci il pannello
    // lo tiene aggiornato senza un timer suo.
    readonly property var status: Session.connected
        ? (Session.nativeCommand("audiorig.status", {}) || ({}))
        : ({})
    readonly property var inputs: Session.nativeCommand("audiorig.inputs", {}) || []
    readonly property var outputs: Session.nativeCommand("audiorig.outputs", {}) || []

    property int refresh: 0

    Connections {
        target: Session
        function onCenterFrequencyChanged() { root.refresh++ }
    }

    // ── Il collegamento ──────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Rectangle {
            implicitWidth: 44
            implicitHeight: 22
            radius: Theme.radiusSmall
            color: Session.connected ? Theme.surfaceSunken : Theme.surface
            border.width: 1
            border.color: Session.connected ? Theme.accent : Theme.border

            Text {
                anchors.centerIn: parent
                text: qsTr("CAT")
                font.pixelSize: Theme.fontSmall
                font.bold: true
                color: Session.connected ? Theme.accent : Theme.textDisabled
            }
        }

        Text {
            Layout.fillWidth: true
            text: {
                root.refresh   // rilettura a ogni aggiornamento dal CAT
                const s = root.status
                if (!Session.connected)
                    return qsTr("non collegata")
                const radio = s.radio || qsTr("radio")
                const port = s.catPort || "—"
                // Zero baud vuol dire che non è una seriale: con rigctld la
                // velocità di linea la governa il demone, e stamparne una
                // farebbe cercare a qualcuno una porta che non esiste.
                if (!s.catBaud)
                    return qsTr("%1 · %2").arg(radio).arg(port)
                return qsTr("%1 · %2 a %3 baud").arg(radio).arg(port).arg(s.catBaud)
            }
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            elide: Text.ElideRight
        }
    }

    // ── L'ingresso audio ─────────────────────────────────────────────────
    //
    // Si indovina dalla descrizione, e quasi sempre bene. Quando sbaglia, però,
    // sbaglia in modo difficile da riconoscere: si sente qualcosa — la stanza —
    // e sembra che la radio riceva male.
    Text {
        Layout.fillWidth: true
        text: qsTr("Ingresso audio")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    DsdrComboBox {
        Layout.fillWidth: true
        enabled: Session.connected
        model: root.inputs.map(function (input) {
            // Una stella accanto a quelli che sembrano il codec di una radio:
            // in un elenco pieno di cavi virtuali è l'unico modo di trovarlo
            // senza provarli tutti.
            return (input.likelyRadio ? "★ " : "   ") + input.name
        })
        currentIndex: {
            const name = root.status.audioInput || ""
            for (let i = 0; i < root.inputs.length; ++i) {
                if (root.inputs[i].name === name)
                    return i
            }
            return -1
        }
        onActivated: function (index) {
            const input = root.inputs[index]
            if (input)
                Session.nativeCommand("audiorig.setAudioInput", { "id": input.id })
        }
    }

    // ── L'uscita audio ───────────────────────────────────────────────────
    //
    // È il percorso di trasmissione: quello che esce di qui entra nel
    // connettore dati della radio. Sbagliarlo non produce un errore, produce
    // un PTT che manda in portante e non trasmette una parola.
    Text {
        Layout.fillWidth: true
        text: qsTr("Uscita audio (trasmissione)")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    DsdrComboBox {
        Layout.fillWidth: true
        enabled: Session.connected
        model: root.outputs.map(function (output) {
            return (output.likelyRadio ? "★ " : "   ") + output.name
        })
        currentIndex: {
            const name = root.status.audioOutput || ""
            for (let i = 0; i < root.outputs.length; ++i) {
                if (root.outputs[i].name === name)
                    return i
            }
            return -1
        }
        onActivated: function (index) {
            const output = root.outputs[index]
            if (output)
                Session.nativeCommand("audiorig.setAudioOutput", { "id": output.id })
            root.refresh++
        }
    }

    Text {
        Layout.fillWidth: true
        text: {
            root.refresh
            const s = root.status
            if (!Session.connected)
                return ""
            if (s.txAudioActive === false)
                return qsTr("L'uscita di trasmissione non si è aperta.")
            if (!s.audioActive)
                return qsTr("L'ingresso audio non si è aperto.")
            return qsTr("S-meter %1 · scarti %2").arg(s.sMeterRaw >= 0 ? s.sMeterRaw : "—")
                                                 .arg(s.micOverruns || 0)
        }
        font.pixelSize: Theme.fontSmall
        color: root.status.audioActive === false ? Theme.danger : Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── Quello che la radio deve avere impostato ─────────────────────────
    //
    // Sono i due menù che decidono se questo backend funziona o no, e nessuno
    // li ricorda: dirli qui costa tre righe e risparmia mezz'ora di ricerca
    // nel manuale.
    Text {
        Layout.fillWidth: true
        text: qsTr("Sulla radio: filtri DATA LCUT/HCUT aperti — sono loro a decidere quanto spettro si vede — e sorgente di modulazione USB per i modi dati.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
