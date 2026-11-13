// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import DecodiumSdr

Dialog {
    id: root
    title: qsTr("Laboratorio registrazioni IQ")
    modal: false
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 30)
    height: Math.min(570, parent.height - 30)
    standardButtons: Dialog.Close
    property int selectedSlot: 0
    property string pathA: ""
    property string pathB: ""
    function run(slot, path) {
        if (!Session.iqAnalysis.analyze(path, slot, Number(startTime.text),
                                        Number(duration.text), Number(fftSize.currentText), windowChoice.currentIndex))
            inputError.text = qsTr("Parametri non validi: inserire tempi non negativi e una FFT valida.")
        else inputError.text = ""
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Scegli una registrazione IQ")
        nameFilters: [qsTr("File WAV (*.wav *.WAV)"), qsTr("Tutti i file (*)")]
        onAccepted: {
            if (root.selectedSlot === 0) root.pathA = selectedFile.toString()
            else root.pathB = selectedFile.toString()
            root.run(root.selectedSlot, selectedFile.toString())
        }
    }
    background: Rectangle { color: Theme.surfaceRaised; border.color: Theme.border; radius: Theme.radiusSmall }
    contentItem: ColumnLayout {
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: qsTr("Statistiche sull’intervallo; spettro della prima finestra FFT completa. Durata 0 = fino alla fine. La larghezza −3 dB è una stima a bin.")
            color: Theme.textSecondary; wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Apri A…"); enabled: !Session.iqAnalysis.running; onClicked: { root.selectedSlot = 0; fileDialog.open() } }
            Button { text: qsTr("Apri B…"); enabled: !Session.iqAnalysis.running; onClicked: { root.selectedSlot = 1; fileDialog.open() } }
            Button { text: qsTr("Annulla analisi"); enabled: Session.iqAnalysis.running; onClicked: Session.iqAnalysis.cancel() }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Inizio (s)"); color: Theme.textPrimary }
            TextField { id: startTime; text: "0"; Layout.preferredWidth: 75; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            Label { text: qsTr("Durata (s)"); color: Theme.textPrimary }
            TextField { id: duration; text: "0"; Layout.preferredWidth: 75; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            Label { text: "FFT"; color: Theme.textPrimary }
            ComboBox { id: fftSize; model: ["256", "512", "1024", "2048", "4096", "8192", "16384"]; currentIndex: 2 }
            ComboBox { id: windowChoice; model: [qsTr("Rettangolare"), "Hann", "Blackman-Harris"]; currentIndex: 1 }
        }
        RowLayout {
            Button { text: qsTr("Rianalizza A"); enabled: root.pathA !== "" && !Session.iqAnalysis.running; onClicked: root.run(0, root.pathA) }
            Button { text: qsTr("Rianalizza B"); enabled: root.pathB !== "" && !Session.iqAnalysis.running; onClicked: root.run(1, root.pathB) }
        }
        Text { id: inputError; Layout.fillWidth: true; color: Theme.warning; wrapMode: Text.WordWrap }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: Session.iqAnalysis.progress; visible: Session.iqAnalysis.running }
        Text { Layout.fillWidth: true; visible: Session.iqAnalysis.error !== ""; text: Session.iqAnalysis.error; color: Theme.warning; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            Repeater {
                model: [Session.iqAnalysis.resultA, Session.iqAnalysis.resultB]
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: Theme.surface; border.color: Theme.border; radius: Theme.radiusSmall
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10
                        Canvas {
                            id: plot
                            Layout.fillWidth: true; Layout.preferredHeight: 110
                            property var bins: modelData.spectrum || []
                            onBinsChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.fillStyle = "#0a111a"; ctx.fillRect(0, 0, width, height)
                                if (!bins.length) return
                                ctx.strokeStyle = "#63cbe3"; ctx.lineWidth = 1
                                ctx.beginPath()
                                const columns = Math.max(1, Math.floor(width))
                                for (let x = 0; x < columns; ++x) {
                                    let db = -140
                                    const from = Math.floor(x * bins.length / columns)
                                    const to = Math.max(from + 1, Math.floor((x + 1) * bins.length / columns))
                                    for (let k = from; k < Math.min(to, bins.length); ++k) db = Math.max(db, bins[k])
                                    const y = height * (1 - Math.max(0, Math.min(1, (db + 140) / 150)))
                                    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.stroke()
                            }
                        }
                        Text { Layout.fillWidth: true; color: Theme.textSecondary; text: modelData.spectrum ? qsTr("−Fs/2          Offset dal centro          +Fs/2  |  −140…+10 dBFS") : ""; font.pixelSize: Theme.fontSmall }
                        ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        Text {
                            width: parent.width
                            color: Theme.textPrimary; wrapMode: Text.WrapAnywhere
                            text: {
                                const r = modelData
                                const heading = index === 0 ? "A" : "B"
                                if (!r.name) return heading + " — " + qsTr("nessuna analisi")
                                return heading + " — " + r.name + "\n\n" +
                                    qsTr("FFT: %1, Δf %2 Hz, inizio %3 s").arg(r.fftSize).arg(Number(r.binHz).toFixed(2)).arg(Number(r.fftStartSeconds).toFixed(3)) + "\n" +
                                    (r.peakValid ? qsTr("Picco: offset %1 Hz, %2 dBFS\nLarghezza −3 dB: %3 Hz (bin)").arg(Number(r.peakOffsetHz).toFixed(2)).arg(Number(r.peakDbfs).toFixed(1)).arg(Number(r.bandwidth3dbHz).toFixed(2)) : qsTr("Nessun picco misurabile")) + "\n\n" +
                                    (r.peakValid && r.centerHz > 0 ? qsTr("Picco RF: %1 Hz").arg(Number(r.peakRfHz).toFixed(2)) + "\n" : "") +
                                    qsTr("Campioni: %1").arg(r.frames) + "\n" +
                                    qsTr("Durata: %1 s").arg(Number(r.durationSeconds).toFixed(3)) + "\n" +
                                    qsTr("Campionamento: %1 Hz").arg(r.sampleRate) + "\n" +
                                    (r.centerHz > 0 ? qsTr("Centro: %1 Hz %2").arg(r.centerHz).arg(r.hasSidecar ? "" : qsTr("(stimato dal nome)")) : qsTr("Centro: sconosciuto")) + "\n\n" +
                                    qsTr("Media I: %1").arg(Number(r.meanI).toFixed(6)) + "\n" +
                                    qsTr("Media Q: %1").arg(Number(r.meanQ).toFixed(6)) + "\n" +
                                    qsTr("RMS: %1").arg(Number(r.rms).toFixed(6)) + "\n" +
                                    qsTr("Picco: %1").arg(Number(r.peak).toFixed(6)) + "\n" +
                                    qsTr("Fondo scala: %1%").arg(Number(r.clippedPercent).toFixed(3))
                            }
                        }
                        }
                    }
                }
            }
        }
    }
}
