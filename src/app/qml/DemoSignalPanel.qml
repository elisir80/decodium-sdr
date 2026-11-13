// SPDX-License-Identifier: GPL-3.0-or-later
// Pannello nativo RX del backend demo. L'host deve includerlo nel modulo QML.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

PanelFrame {
    id: root
    title: qsTr("GENERATORE SINTETICO RX")
    property var controller: Session
    property string errorText: ""

    function refresh() {
        const s = controller.nativeCommand("demo.synthetic.status", {}) || ({})
        active.checked = s.enabled === true
        mode.currentIndex = s.mode === "am" ? 1 : s.mode === "fm" ? 2 : 0
        offset.text = String(s.offsetHz !== undefined ? s.offsetHz : 1000)
        amplitude.text = String(s.amplitude !== undefined ? s.amplitude : 0.2)
        modulation.text = String(s.modulationHz !== undefined ? s.modulationHz : 1000)
        depth.text = String(s.amDepth !== undefined ? s.amDepth : 0.5)
        deviation.text = String(s.fmDeviationHz !== undefined ? s.fmDeviationHz : 2500)
        noise.text = String(s.noiseRms !== undefined ? s.noiseRms : 0)
        seed.text = String(s.seed !== undefined ? s.seed : 1)
        period.text = String(s.impulsePeriod !== undefined ? s.impulsePeriod : 0)
        impulse.text = String(s.impulseAmplitude !== undefined ? s.impulseAmplitude : 0)
        drift.text = String(s.driftHz !== undefined ? s.driftHz : 0)
        driftPeriod.text = String(s.driftPeriodSec !== undefined ? s.driftPeriodSec : 10)
        interferenceOffset.text = String(s.interfererOffsetHz !== undefined ? s.interfererOffsetHz : 3000)
        interferenceAmplitude.text = String(s.interfererAmplitude !== undefined ? s.interfererAmplitude : 0)
        fading.text = String(s.fadingDepth !== undefined ? s.fadingDepth : 0)
        fadingPeriod.text = String(s.fadingPeriodSec !== undefined ? s.fadingPeriodSec : 5)
    }
    function apply() {
        const fields = [offset, amplitude, modulation, depth, deviation, noise, seed, period, impulse,
                        drift, driftPeriod, interferenceOffset, interferenceAmplitude, fading, fadingPeriod]
        for (let i = 0; i < fields.length; ++i) {
            if (!fields[i].acceptableInput) {
                errorText = qsTr("Controlla i valori inseriti.")
                return false
            }
        }
        const result = controller.nativeCommand("demo.synthetic.apply", {
            enabled: active.checked, mode: ["tone", "am", "fm"][mode.currentIndex],
            offsetHz: Number(offset.text), amplitude: Number(amplitude.text),
            modulationHz: Number(modulation.text), amDepth: Number(depth.text),
            fmDeviationHz: Number(deviation.text), noiseRms: Number(noise.text),
            seed: Number(seed.text), impulsePeriod: Number(period.text),
            impulseAmplitude: Number(impulse.text), driftHz: Number(drift.text),
            driftPeriodSec: Number(driftPeriod.text), interfererOffsetHz: Number(interferenceOffset.text),
            interfererAmplitude: Number(interferenceAmplitude.text), fadingDepth: Number(fading.text),
            fadingPeriodSec: Number(fadingPeriod.text)
        }) || ({})
        errorText = result.ok === true ? "" : (result.error || qsTr("Impossibile applicare."))
        return result.ok === true
    }
    Component.onCompleted: refresh()

    CheckBox { id: active; objectName: "syntheticEnabled"; text: qsTr("Attiva generatore RX") }
    Text { text: qsTr("Solo ricezione simulata; Applica riavvia la sequenza."); color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
    GridLayout {
        columns: 2
        Layout.fillWidth: true
        Label { text: qsTr("Segnale") }
        ComboBox { id: mode; objectName: "syntheticMode"; Layout.fillWidth: true; model: [qsTr("Portante"), "AM", "FM"] }
        Label { text: qsTr("Offset dal centro (Hz)") }
        TextField { id: offset; objectName: "syntheticOffset"; Layout.fillWidth: true; validator: DoubleValidator { bottom: -90000; top: 90000; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Ampiezza (0–1)") }
        TextField { id: amplitude; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Modulazione (Hz)") }
        TextField { id: modulation; Layout.fillWidth: true; validator: DoubleValidator { bottom: 1; top: 20000; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Profondità AM (0–1)") }
        TextField { id: depth; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Deviazione FM (Hz)") }
        TextField { id: deviation; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 75000; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Rumore RMS per I/Q") }
        TextField { id: noise; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Seed") }
        TextField { id: seed; Layout.fillWidth: true; validator: RegularExpressionValidator { regularExpression: /[0-9]{1,10}/ } }
        Label { text: qsTr("Periodo impulsi (campioni, 0 = off)") }
        TextField { id: period; Layout.fillWidth: true; validator: IntValidator { bottom: 0; top: 100000000 } }
        Label { text: qsTr("Deriva massima (Hz)") }
        TextField { id: drift; objectName: "syntheticDrift"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 90000; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Periodo deriva (s)") }
        TextField { id: driftPeriod; objectName: "syntheticDriftPeriod"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0.1; top: 3600; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Offset interferente (Hz)") }
        TextField { id: interferenceOffset; objectName: "syntheticInterfererOffset"; Layout.fillWidth: true; validator: DoubleValidator { bottom: -90000; top: 90000; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Ampiezza interferente (0–1)") }
        TextField { id: interferenceAmplitude; objectName: "syntheticInterfererAmplitude"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Profondità fading (0–1)") }
        TextField { id: fading; objectName: "syntheticFading"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Periodo fading (s)") }
        TextField { id: fadingPeriod; objectName: "syntheticFadingPeriod"; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0.1; top: 3600; notation: DoubleValidator.StandardNotation; locale: "C" } }
        Label { text: qsTr("Ampiezza impulso") }
        TextField { id: impulse; Layout.fillWidth: true; validator: DoubleValidator { bottom: 0; top: 1; notation: DoubleValidator.StandardNotation; locale: "C" } }
    }
    Text { text: root.errorText; visible: text.length > 0; color: Theme.demoSignalError; wrapMode: Text.Wrap; Layout.fillWidth: true }
    DsdrButton { objectName: "syntheticApply"; text: qsTr("Applica / Ripeti"); onClicked: root.apply() }
}
