// SPDX-License-Identifier: GPL-3.0-or-later
// Controlli comuni a librtlsdr e SoapyRTLSDR.
//
// L'IF non è una decorazione della frequenza: il backend trasla i campioni IQ
// prima del DSP. Questo pannello conserva quindi una configurazione completa,
// ma lascia alla HAL la validazione dell'hardware e della banda scelta.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root

    title: qsTr("DEVICE  ·  RTL-SDR")

    readonly property var gainRange: Session.nativeCommand("rtlsdr.gainRange", {}) || ({})
    property int refresh: 0
    readonly property var directInfo: {
        root.refresh
        return Session.nativeCommand("rtlsdr.directSamplingInfo", {}) || ({})
    }
    readonly property bool ppmSupported: Session.nativeCommand("rtlsdr.ppmSupported", {}) === true

    property bool automaticGain: true
    property real manualGainDb: 28.0
    property int ppm: 0
    property bool biasTee: false
    property bool directSamplingEnabled: false
    property bool offsetTuning: false

    property bool ifOutputEnabled: false
    property int ifFrequencyHz: 8830000
    property string ifSideband: "auto"
    property int ifUsbShiftHz: 1500
    property int ifLsbShiftHz: -1500
    property bool ifSpectrumInverted: false

    Settings {
        category: "rtlsdr"
        property alias automaticGain: root.automaticGain
        property alias manualGainDb: root.manualGainDb
        property alias ppm: root.ppm
        property alias biasTee: root.biasTee
        property alias directSamplingEnabled: root.directSamplingEnabled
        property alias offsetTuning: root.offsetTuning
        property alias ifOutputEnabled: root.ifOutputEnabled
        property alias ifFrequencyHz: root.ifFrequencyHz
        property alias ifSideband: root.ifSideband
        property alias ifUsbShiftHz: root.ifUsbShiftHz
        property alias ifLsbShiftHz: root.ifLsbShiftHz
        property alias ifSpectrumInverted: root.ifSpectrumInverted
    }

    Connections {
        target: Session
        function onCenterFrequencyChanged() { root.refresh++ }
        function onSampleRateChanged() { root.refresh++ }
    }

    function applyGain() {
        Session.nativeCommand("rtlsdr.setGain",
                              { "db": automaticGain ? -1 : manualGainDb })
    }

    function ifRequest() {
        return {
            "enabled": ifOutputEnabled,
            "frequencyHz": ifFrequencyHz,
            "sideband": ifSideband,
            "usbShiftHz": ifUsbShiftHz,
            "lsbShiftHz": ifLsbShiftHz,
            "spectrumInverted": ifSpectrumInverted
        }
    }

    function applyIfSettings() {
        const effective = Session.nativeCommand("rtlsdr.setIfSettings", ifRequest()) || ({})
        if (effective.enabled !== undefined)
            ifOutputEnabled = effective.enabled === true
        if (effective.frequencyHz !== undefined)
            ifFrequencyHz = effective.frequencyHz
        if (effective.sideband !== undefined)
            ifSideband = effective.sideband
        if (effective.usbShiftHz !== undefined)
            ifUsbShiftHz = effective.usbShiftHz
        if (effective.lsbShiftHz !== undefined)
            ifLsbShiftHz = effective.lsbShiftHz
        if (effective.spectrumInverted !== undefined)
            ifSpectrumInverted = effective.spectrumInverted === true
        refresh++
    }

    function applyDirectSampling(enabled) {
        const effective = Session.nativeCommand("rtlsdr.setDirectSampling",
                                                 { "mode": enabled ? 2 : 0 })
        if (effective !== undefined && effective !== null && !isNaN(Number(effective)))
            directSamplingEnabled = Number(effective) !== 0
        if (directSamplingEnabled)
            offsetTuning = false
        refresh++
    }

    Component.onCompleted: {
        // L'IF va applicata prima del Q ADC: con una radio che ha una IF fissa
        // l'input fisico è già nella banda 500 kHz–24 MHz, anche se il VFO
        // mostrato dall'app è per esempio sui 40 metri.
        Qt.callLater(function() {
            root.applyIfSettings()
            // Il backend apre gia' il tuner in modalita' normale. Inviare
            // ogni volta il valore 0 fa produrre a librtlsdr diagnostica
            // estranea al log strutturato, senza modificare l'hardware.
            if (root.directSamplingEnabled)
                root.applyDirectSampling(true)
            root.applyGain()
            // Zero e' il valore di reset con cui un device appena aperto e'
            // gia' configurato; alcuni V4 lo rifiutano come comando pur non
            // avendo alcun problema di sintonia.
            if (root.ppmSupported && root.ppm !== 0)
                Session.nativeCommand("rtlsdr.setPpm", { "ppm": root.ppm })
            if (root.biasTee)
                Session.nativeCommand("rtlsdr.setBiasTee", { "enabled": true })
            if (!root.directSamplingEnabled && root.offsetTuning)
                Session.nativeCommand("rtlsdr.setOffsetTuning",
                                      { "enabled": true })
        })
    }

    // ── Guadagno ────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing

        Text {
            text: qsTr("Guadagno")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        Item { Layout.fillWidth: true }
        DsdrButton {
            text: qsTr("Auto")
            implicitWidth: 56
            implicitHeight: 22
            checkable: true
            checked: root.automaticGain
            enabled: !root.directSamplingEnabled && root.gainRange.hasAgc !== false
            onToggled: {
                root.automaticGain = checked
                root.applyGain()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        enabled: !root.automaticGain && !root.directSamplingEnabled

        DsdrSlider {
            Layout.fillWidth: true
            from: root.gainRange.min !== undefined ? root.gainRange.min : 0
            to: root.gainRange.max !== undefined ? root.gainRange.max : 49.6
            value: root.manualGainDb
            onMoved: {
                root.manualGainDb = value
                root.applyGain()
            }
        }
        Text {
            text: root.automaticGain ? qsTr("auto") : root.manualGainDb.toFixed(1) + " dB"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: root.automaticGain ? Theme.textDisabled : Theme.textPrimary
            Layout.preferredWidth: 54
            horizontalAlignment: Text.AlignRight
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.directSamplingEnabled
        text: qsTr("Il Q ADC bypassa il tuner: guadagno e AGC del tuner non si applicano.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        visible: root.ppmSupported
        Text {
            text: qsTr("Correzione")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrSlider {
            Layout.fillWidth: true
            from: -100; to: 100; stepSize: 1
            value: root.ppm
            onMoved: {
                root.ppm = Math.round(value)
                Session.nativeCommand("rtlsdr.setPpm", { "ppm": root.ppm })
            }
        }
        Text {
            text: root.ppm + " ppm"
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            Layout.preferredWidth: 62
            horizontalAlignment: Text.AlignRight
        }
    }

    // ── Percorso d'ingresso ─────────────────────────────────────────────
    Text {
        Layout.fillWidth: true
        text: qsTr("Ingresso HF")
        font.pixelSize: Theme.fontSmall
        font.bold: true
        color: Theme.textSecondary
        topPadding: Theme.spacingTight
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTight

        DsdrButton {
            Layout.fillWidth: true
            implicitHeight: 24
            text: qsTr("Tuner")
            checkable: true
            checked: !root.directSamplingEnabled
            onClicked: root.applyDirectSampling(false)
        }
        DsdrButton {
            Layout.fillWidth: true
            implicitHeight: 24
            text: qsTr("Q ADC")
            checkable: true
            checked: root.directSamplingEnabled
            enabled: root.directSamplingEnabled || root.directInfo.canEnableNow === true
            onClicked: root.applyDirectSampling(true)
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.directInfo.message || qsTr("Direct sampling non disponibile su questo driver.")
        font.pixelSize: Theme.fontSmall
        color: root.directInfo.canEnableNow === true || root.directSamplingEnabled
               ? Theme.textSecondary : Theme.warning
        wrapMode: Text.WordWrap
    }

    // ── Uscita IF fissa ─────────────────────────────────────────────────
    Text {
        Layout.fillWidth: true
        text: qsTr("USCITA IF DEL RICEVITORE")
        font.pixelSize: Theme.fontSmall
        font.bold: true
        color: Theme.textSecondary
        topPadding: Theme.spacingTight
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing
        Text {
            text: qsTr("Usa IF")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        Item { Layout.fillWidth: true }
        DsdrButton {
            text: root.ifOutputEnabled ? qsTr("Attiva") : qsTr("Spenta")
            implicitWidth: 72
            implicitHeight: 22
            checkable: true
            checked: root.ifOutputEnabled
            onToggled: {
                root.ifOutputEnabled = checked
                root.applyIfSettings()
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.spacingTight
        rowSpacing: Theme.spacingTight
        enabled: root.ifOutputEnabled

        Text {
            text: qsTr("Frequenza IF")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        TextField {
            id: ifFrequencyField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            text: String(root.ifFrequencyHz)
            selectByMouse: true
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            validator: IntValidator { bottom: 100000; top: 1766000000 }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: ifFrequencyField.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                root.ifFrequencyHz = Number(text)
                root.applyIfSettings()
            }
        }

        Text {
            text: qsTr("Banda laterale")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrComboBox {
            id: ifSidebandBox
            Layout.fillWidth: true
            model: [qsTr("Automatica"), qsTr("USB"), qsTr("LSB")]
            currentIndex: root.ifSideband === "lsb" ? 2 : root.ifSideband === "usb" ? 1 : 0
            onActivated: {
                root.ifSideband = ["auto", "usb", "lsb"][currentIndex]
                root.applyIfSettings()
            }
        }

        Text {
            text: qsTr("Shift USB")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        TextField {
            id: usbShiftField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            text: String(root.ifUsbShiftHz)
            selectByMouse: true
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            validator: IntValidator { bottom: -500000; top: 500000 }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: usbShiftField.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                root.ifUsbShiftHz = Number(text)
                root.applyIfSettings()
            }
        }

        Text {
            text: qsTr("Shift LSB")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        TextField {
            id: lsbShiftField
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            text: String(root.ifLsbShiftHz)
            selectByMouse: true
            font.pixelSize: Theme.fontSmall
            font.family: Theme.monoFamily
            color: Theme.textPrimary
            validator: IntValidator { bottom: -500000; top: 500000 }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.surfaceSunken
                border.width: 1
                border.color: lsbShiftField.activeFocus ? Theme.accent : Theme.border
            }
            onEditingFinished: {
                root.ifLsbShiftHz = Number(text)
                root.applyIfSettings()
            }
        }

        Text {
            text: qsTr("Spettro invertito")
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }
        DsdrButton {
            text: root.ifSpectrumInverted ? qsTr("Invertito") : qsTr("Normale")
            implicitHeight: 22
            checkable: true
            checked: root.ifSpectrumInverted
            onToggled: {
                root.ifSpectrumInverted = checked
                root.applyIfSettings()
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.ifOutputEnabled
        text: qsTr("Il VFO resta sulla frequenza radio; RTL-SDR riceve IF più lo shift USB/LSB e raddrizza lo spettro prima del DSP.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
        wrapMode: Text.WordWrap
    }

    // ── Alimentazione e spur del tuner ──────────────────────────────────
    DsdrButton {
        Layout.fillWidth: true
        implicitHeight: 24
        text: root.biasTee ? qsTr("Bias tee acceso") : qsTr("Bias tee spento")
        checkable: true
        checked: root.biasTee
        danger: root.biasTee
        onToggled: {
            root.biasTee = checked
            Session.nativeCommand("rtlsdr.setBiasTee", { "enabled": checked })
        }
    }

    DsdrButton {
        Layout.fillWidth: true
        implicitHeight: 24
        visible: !root.directSamplingEnabled
        text: root.offsetTuning ? qsTr("Offset tuning acceso") : qsTr("Offset tuning spento")
        checkable: true
        checked: root.offsetTuning
        onToggled: {
            root.offsetTuning = checked
            Session.nativeCommand("rtlsdr.setOffsetTuning", { "enabled": checked })
        }
    }
}
