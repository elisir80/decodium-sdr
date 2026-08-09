// SPDX-License-Identifier: GPL-3.0-or-later
// Macchina del tempo: riascoltare la banda appena passata.
//
// Non è il registratore. Il registratore lo si accende prima, e serve a
// conservare; questo è già acceso da sempre e serve a rimediare — il
// nominativo che è passato mentre si scriveva, la chiamata coperta da un
// QRM. Nessuna radio sa farlo da sé: il passato lo tiene il nostro motore,
// per qualunque backend, ed è per questo che i comandi stanno qui e non nel
// pannello di un device.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("MACCHINA DEL TEMPO")

    readonly property real delaySeconds: Session.replayDelaySeconds
    readonly property real historySeconds: Session.replayHistorySeconds

    /// Il salto più lungo che abbia senso proporre adesso: offrire «30 s»
    /// quando in memoria ce ne sono cinque è una promessa che si scopre falsa
    /// solo premendo.
    function jumpEnabled(seconds) {
        return Session.connected && root.historySeconds >= 1
    }

    function formatSeconds(s) {
        const total = Math.max(0, Math.round(s))
        if (total < 60)
            return total + " s"
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    // ── Dove si sta ascoltando ───────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Rectangle {
            width: 8; height: 8; radius: 4
            // In diretta è verde come lo stato di connessione; in riascolto
            // cambia colore, perché tutto ciò che si vede sullo schermo — non
            // solo l'audio — è di qualche secondo fa.
            color: Session.replaying ? Theme.warning : Theme.success
        }

        Text {
            Layout.fillWidth: true
            text: Session.replaying
                  ? qsTr("%1 indietro").arg(root.formatSeconds(root.delaySeconds))
                  : qsTr("In diretta")
            font.pixelSize: Theme.fontNormal
            font.family: Theme.monoFamily
            color: Session.replaying ? Theme.warning : Theme.textPrimary
        }

        Text {
            text: qsTr("storia %1").arg(root.formatSeconds(root.historySeconds))
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textSecondary
        }
    }

    // ── Scorrimento continuo ─────────────────────────────────────────────
    //
    // A destra c'è adesso, a sinistra il punto più lontano a cui la memoria
    // arriva: la stessa direzione del waterfall, che scende verso il passato.
    //
    // La scala è quella della capacità, non della storia raccolta finora: se
    // gli estremi seguissero la storia, che si allunga a ogni secondo, la
    // stessa posizione della maniglia significherebbe un istante diverso a
    // ogni battito. Chiedere più passato di quanto ce ne sia è comunque
    // innocuo — il motore accorcia il salto a quello disponibile.
    DsdrSlider {
        id: scrub

        Layout.fillWidth: true
        enabled: Session.connected && root.historySeconds > 1
        from: -Math.max(1, Session.replayCapacitySeconds)
        to: 0
        value: -root.delaySeconds
        accentColor: Theme.warning
        onMoved: Session.setReplayDelay(-value)
    }

    // Trascinato il cursore, il binding su `value` si romperebbe e la maniglia
    // resterebbe ferma su un ritardo che il motore ha già corretto. Questo lo
    // rimette al suo posto quando non lo si sta tenendo.
    Binding {
        target: scrub
        property: "value"
        value: -root.delaySeconds
        when: !scrub.pressed
        restoreMode: Binding.RestoreNone
    }

    // ── Salti ────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Repeater {
            model: [10, 30, 60]

            delegate: DsdrButton {
                required property int modelData

                Layout.fillWidth: true
                implicitWidth: 0
                implicitHeight: 26
                text: "◀◀ " + modelData + "s"
                enabled: root.jumpEnabled(modelData)
                onClicked: Session.rewind(modelData)
            }
        }

        DsdrButton {
            Layout.preferredWidth: 76
            implicitWidth: 0
            implicitHeight: 26
            text: qsTr("Diretta")
            // Acceso solo quando c'è davvero da tornare: un pulsante che non
            // fa niente insegna a non fidarsi degli altri.
            enabled: Session.replaying
            onClicked: Session.returnToLive()
        }
    }

    Text {
        Layout.fillWidth: true
        // La profondità dipende dal ritmo di campionamento, e cambiarlo la
        // cambia: meglio dire il numero vero che lasciarlo scoprire.
        text: qsTr("Memoria: %1 a %2 MS/s. Cambiare banda o campionamento riparte da zero.")
              .arg(root.formatSeconds(Session.replayCapacitySeconds))
              .arg((Session.sampleRate / 1e6).toFixed(3))
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
