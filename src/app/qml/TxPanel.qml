// SPDX-License-Identifier: GPL-3.0-or-later
// Trasmissione: il microfono, la compressione, il livello, il PTT.
//
// Il pannello esiste solo se la radio trasmette (`slotActive` in ChannelStrip):
// su un ricevitore puro non viene creato, non disabilitato — un PTT grigio
// racconta di una funzione che c'è e non si può usare, e qui non c'è proprio
// (CONSTITUTION §7).
//
// Gli indicatori sono due e non uno perché dicono cose diverse: quello del
// microfono dice quanto si sta parlando, quello d'uscita quanto se ne sta
// mettendo in aria. Con un solo indicatore, un guadagno microfonico sbagliato
// e un livello d'uscita sbagliato hanno lo stesso aspetto.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("TRASMISSIONE")
    draggable: true

    // ── Dove si trasmetterebbe ───────────────────────────────────────────
    //
    // Si trasmette sul canale che si sta ascoltando, con il modo con cui lo si
    // ascolta. Dirlo qui evita la domanda «su quale VFO sto per andare?»,
    // che è quella che si fa dopo aver premuto.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: qsTr("Canale")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Text {
            Layout.fillWidth: true
            text: Session.txSummary
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            elide: Text.ElideRight
        }
    }

    // ── PTT e tasto ──────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.fillWidth: true
            implicitHeight: 34
            text: Session.transmitting ? qsTr("TX ON") : qsTr("PTT")
            checkable: true
            checked: Session.transmitting
            danger: true
            enabled: Session.connected
            onToggled: Session.setPtt(checked)
        }

        // In CW il PTT apre la trasmissione e il tasto la modula: sono due
        // gesti distinti, e un solo pulsante li confonderebbe.
        DsdrButton {
            visible: Session.txCw
            implicitWidth: 76
            implicitHeight: 34
            text: qsTr("TASTO")
            enabled: Session.transmitting
            onPressed: Session.setCwKeyDown(true)
            onReleased: Session.setCwKeyDown(false)
            onCanceled: Session.setCwKeyDown(false)
        }
    }

    // ── Microfono ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: !Session.txCw

        Text {
            text: qsTr("MIC")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: Theme.textSecondary
            Layout.preferredWidth: 34
        }

        LevelBar {
            Layout.fillWidth: true
            value: Session.micLevel
            // Sopra 0,9 si è a un passo dal limitatore: il colore lo dice
            // prima che la voce cominci a farsi dura.
            warnAbove: 0.9
        }

        Text {
            text: Session.micGainDb.toFixed(0) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 46
            horizontalAlignment: Text.AlignRight
        }
    }

    DsdrSlider {
        Layout.fillWidth: true
        visible: !Session.txCw
        from: -20; to: 40
        stepSize: 1
        value: Session.micGainDb
        onMoved: Session.micGainDb = value
    }

    // ── Compressione ─────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight
        visible: !Session.txCw

        Text {
            text: qsTr("COMP")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            Layout.preferredWidth: 40
        }

        DsdrSlider {
            Layout.fillWidth: true
            from: 0; to: 20
            stepSize: 1
            value: Session.txCompressionDb
            onMoved: Session.txCompressionDb = value
        }

        Text {
            // Non quanta se ne è chiesta: quanta ne sta lavorando adesso. È
            // l'unico numero che dice se il comando sta facendo qualcosa.
            text: Session.transmitting
                  ? "−" + Session.txCompressionMeter.toFixed(0) + " dB"
                  : Session.txCompressionDb.toFixed(0) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Session.txCompressionMeter > 12 ? Theme.warning : Theme.textPrimary
            Layout.preferredWidth: 52
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Livello d'uscita ─────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        Text {
            text: qsTr("OUT")
            font.pixelSize: Theme.fontSmall
            font.bold: true
            color: Theme.textSecondary
            Layout.preferredWidth: 34
        }

        LevelBar {
            Layout.fillWidth: true
            value: Session.txLevel
            // A uno si è al fondo scala del convertitore: oltre non c'è più
            // segnale, c'è solo distorsione che occupa la banda del vicino.
            warnAbove: 0.98
        }

        Text {
            text: Math.round(Session.txDrive * 100) + " %"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 46
            horizontalAlignment: Text.AlignRight
        }
    }

    DsdrSlider {
        Layout.fillWidth: true
        from: 0.1; to: 1.0
        stepSize: 0.01
        value: Session.txDrive
        onMoved: Session.txDrive = value
    }

    // ── Da dove viene la voce ────────────────────────────────────────────
    //
    // Non il predefinito di sistema: su una macchina con dei cavi audio
    // virtuali — e chi fa i modi digitali ne ha sempre — il predefinito è
    // quasi mai il microfono, e si finisce a trasmettere quello che passa di
    // lì senza capire perché.
    Text {
        Layout.fillWidth: true
        visible: !Session.txCw
        text: qsTr("Microfono")
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    DsdrComboBox {
        Layout.fillWidth: true
        visible: !Session.txCw

        readonly property var devices: Session.micDevices

        model: [qsTr("Predefinito di sistema")].concat(
                   devices.map(function (device) { return device.name }))

        currentIndex: {
            if (Session.micDeviceId === "")
                return 0
            for (let i = 0; i < devices.length; ++i) {
                if (devices[i].id === Session.micDeviceId)
                    return i + 1
            }
            return 0
        }

        onActivated: function (index) {
            Session.micDeviceId = index === 0 ? "" : devices[index - 1].id
        }
    }

    Settings {
        category: "tx"
        property alias microphone: root.savedMicrophone
    }

    /// Il microfono scelto sopravvive alla chiusura: cambiarlo a ogni avvio
    /// sarebbe un rito, e ci si accorgerebbe di averlo dimenticato solo
    /// trasmettendo.
    property string savedMicrophone: ""
    onSavedMicrophoneChanged: {
        if (savedMicrophone !== Session.micDeviceId)
            Session.micDeviceId = savedMicrophone
    }
    Component.onCompleted: {
        if (savedMicrophone !== "")
            Session.micDeviceId = savedMicrophone
    }
    Connections {
        target: Session
        function onTxChanged() { root.savedMicrophone = Session.micDeviceId }
    }

    Text {
        Layout.fillWidth: true
        visible: !Session.txCw && Session.micActive
        text: qsTr("In trasmissione: %1").arg(Session.micDeviceName)
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        elide: Text.ElideRight
    }

    // Quando modula la radio, il livello d'uscita non è potenza: è quanto
    // audio le si manda nell'ingresso dati. Il giudice è il suo ALC, non
    // questo cursore, e conviene dirlo dove il cursore sta.
    Text {
        Layout.fillWidth: true
        visible: !Session.capabilities.clientModulation
        text: qsTr("Modula la radio: alza OUT finché l'ALC comincia a muoversi, poi torna indietro.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }
}
