// SPDX-License-Identifier: GPL-3.0-or-later
// Pianificatore operativo e gestore dei moduli IQ.
//
// Sono nello stesso strumento perché entrambi agiscono fuori dal percorso
// caldo: una scadenza consegna un comando al core, un modulo viene caricato
// esclusivamente al confine con il thread DSP. Nessuno dei due programmi TX.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Popup {
    id: root

    anchors.centerIn: Overlay.overlay
    width: Math.min(920, Overlay.overlay.width - 40)
    height: Math.min(680, Overlay.overlay.height - 40)
    modal: true
    focus: true
    padding: Theme.spacingLoose
    clip: true

    property var actionIds: ["tune", "scan", "record-iq-start", "record-iq-stop",
                             "record-audio-start", "record-audio-stop"]
    property var actionLabels: [qsTr("Sintonizza"), qsTr("Avvia scansione"),
                                qsTr("Avvia registrazione IQ"), qsTr("Ferma registrazione IQ"),
                                qsTr("Avvia registrazione audio"), qsTr("Ferma registrazione audio")]

    function stateColor(state) {
        if (state === "active" || state === "completed")
            return Theme.success
        if (state === "error" || state === "failed" || state === "missed")
            return Theme.danger
        if (state === "running")
            return Theme.accent
        return Theme.textSecondary
    }

    function utcSoon() {
        return new Date(Date.now() + 10 * 60 * 1000).toISOString()
    }

    function addScheduledAction() {
        const action = root.actionIds[actionBox.currentIndex]
        const arguments = {}
        if (action === "tune") {
            arguments.frequencyHz = Math.round(Number(tuneFrequency.text) * 1000000)
        } else if (action === "scan") {
            arguments.startHz = Math.round(Number(scanStart.text) * 1000000)
            arguments.endHz = Math.round(Number(scanEnd.text) * 1000000)
            arguments.stepHz = Math.round(Number(scanStep.text))
            arguments.dwellMs = Math.round(Number(scanDwell.text))
        } else if (action === "record-iq-start" || action === "record-audio-start") {
            arguments.path = recordingPath.text.trim()
        }
        const id = Session.scheduleAction(action, utcField.text.trim(), arguments)
        schedulerNotice.text = id.length > 0
            ? qsTr("Operazione aggiunta alla coda.")
            : qsTr("Data UTC o parametri non validi: l'operazione non è stata aggiunta.")
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.borderStrong
        radius: Theme.radius
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.65)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true

                Text {
                    text: qsTr("Operazioni e moduli IQ")
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    color: Theme.textPrimary
                }

                Text {
                    text: qsTr("Lo scheduler gestisce esclusivamente sintonia, scansione e registrazioni RX; non può trasmettere.")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            DsdrButton {
                text: qsTr("Chiudi")
                onClicked: root.close()
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: qsTr("Scheduler") }
            TabButton { text: qsTr("Moduli IQ") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ScrollView {
                id: schedulerScroll
                clip: true

                ColumnLayout {
                    width: schedulerScroll.availableWidth
                    spacing: Theme.spacing

                    Text {
                        text: qsTr("Aggiungi un'operazione UTC")
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        color: Theme.textPrimary
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing
                        rowSpacing: Theme.spacingTight

                        Text {
                            text: qsTr("Operazione")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        DsdrComboBox {
                            id: actionBox
                            Layout.fillWidth: true
                            model: root.actionLabels
                        }

                        Text {
                            text: qsTr("Quando (UTC ISO-8601)")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        TextField {
                            id: utcField
                            Layout.fillWidth: true
                            text: root.utcSoon()
                            placeholderText: "2026-08-16T12:00:00.000Z"
                            selectByMouse: true
                        }

                        Text {
                            visible: root.actionIds[actionBox.currentIndex] === "tune"
                            text: qsTr("Frequenza MHz")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        TextField {
                            id: tuneFrequency
                            visible: root.actionIds[actionBox.currentIndex] === "tune"
                            Layout.fillWidth: true
                            text: Session.centerFrequency > 0
                                  ? (Session.centerFrequency / 1000000).toFixed(6) : "14.200000"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            selectByMouse: true
                        }

                        Text {
                            visible: root.actionIds[actionBox.currentIndex] === "scan"
                            text: qsTr("Intervallo scansione MHz")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            visible: root.actionIds[actionBox.currentIndex] === "scan"
                            Layout.fillWidth: true
                            spacing: Theme.spacingTight

                            TextField {
                                id: scanStart
                                Layout.fillWidth: true
                                text: "14.000000"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                            Text { text: "–"; color: Theme.textSecondary }
                            TextField {
                                id: scanEnd
                                Layout.fillWidth: true
                                text: "14.350000"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                        }

                        Text {
                            visible: root.actionIds[actionBox.currentIndex] === "scan"
                            text: qsTr("Passo Hz / sosta ms")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            visible: root.actionIds[actionBox.currentIndex] === "scan"
                            Layout.fillWidth: true
                            spacing: Theme.spacingTight

                            TextField {
                                id: scanStep
                                Layout.fillWidth: true
                                text: "1000"
                                inputMethodHints: Qt.ImhDigitsOnly
                            }
                            Text { text: "/"; color: Theme.textSecondary }
                            TextField {
                                id: scanDwell
                                Layout.fillWidth: true
                                text: "350"
                                inputMethodHints: Qt.ImhDigitsOnly
                            }
                        }

                        Text {
                            visible: root.actionIds[actionBox.currentIndex] === "record-iq-start"
                                     || root.actionIds[actionBox.currentIndex] === "record-audio-start"
                            text: qsTr("File (facoltativo)")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        TextField {
                            id: recordingPath
                            visible: root.actionIds[actionBox.currentIndex] === "record-iq-start"
                                     || root.actionIds[actionBox.currentIndex] === "record-audio-start"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Vuoto: cartella e nome predefiniti")
                            selectByMouse: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        DsdrButton {
                            text: qsTr("Pianifica")
                            enabled: utcField.text.trim().length > 0
                            onClicked: root.addScheduledAction()
                        }

                        DsdrButton {
                            text: qsTr("Pulisci cronologia")
                            onClicked: Session.clearScheduledHistory()
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            id: schedulerNotice
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                            Layout.maximumWidth: 420
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                    }

                    Text {
                        text: qsTr("Coda pianificata")
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        color: Theme.textPrimary
                    }

                    Text {
                        visible: Session.scheduledJobs.length === 0
                        text: qsTr("Nessuna operazione pianificata.")
                        color: Theme.textDisabled
                        font.pixelSize: Theme.fontSmall
                    }

                    Repeater {
                        model: Session.scheduledJobs

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: jobLayout.implicitHeight + 2 * Theme.spacing
                            radius: Theme.radiusSmall
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.border

                            ColumnLayout {
                                id: jobLayout
                                anchors.fill: parent
                                anchors.margins: Theme.spacing
                                spacing: Theme.spacingTight

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: modelData.action + " · " + modelData.atUtc
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontSmall
                                        font.family: Theme.monoFamily
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                    }

                                    Text {
                                        text: modelData.status
                                        color: root.stateColor(modelData.status)
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: modelData.message
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    DsdrButton {
                                        visible: modelData.status === "pending"
                                        text: modelData.enabled ? qsTr("Disattiva") : qsTr("Attiva")
                                        fontSize: Theme.fontSmall
                                        onClicked: Session.setScheduledActionEnabled(modelData.id,
                                                                                      !modelData.enabled)
                                    }

                                    DsdrButton {
                                        visible: modelData.status === "pending"
                                        text: qsTr("Annulla")
                                        fontSize: Theme.fontSmall
                                        danger: true
                                        onClicked: Session.cancelScheduledAction(modelData.id)
                                    }

                                    DsdrButton {
                                        visible: modelData.status !== "pending"
                                        text: qsTr("Rimuovi")
                                        fontSize: Theme.fontSmall
                                        onClicked: Session.removeScheduledAction(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ScrollView {
                id: modulesScroll
                clip: true

                ColumnLayout {
                    width: modulesScroll.availableWidth
                    spacing: Theme.spacing

                    Text {
                        text: qsTr("Catalogo moduli IQ")
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        color: Theme.textPrimary
                    }

                    Text {
                        text: qsTr("La ricerca non esegue librerie. Un modulo viene caricato solo quando lo abiliti; carica codice nativo, quindi usa file di cui ti fidi.")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        DsdrButton {
                            text: qsTr("Aggiorna catalogo")
                            onClicked: Session.refreshIqModules()
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: qsTr("Attivi: %1").arg(Session.iqModuleNames.length)
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing
                        rowSpacing: Theme.spacingTight

                        Text {
                            text: qsTr("Aggiungi cartella")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: moduleDirectory
                                Layout.fillWidth: true
                                placeholderText: "/percorso/cartella"
                                selectByMouse: true
                            }
                            DsdrButton {
                                text: qsTr("Aggiungi")
                                enabled: moduleDirectory.text.trim().length > 0
                                onClicked: {
                                    if (Session.addIqModuleDirectory(moduleDirectory.text))
                                        moduleDirectory.clear()
                                }
                            }
                        }

                        Text {
                            text: qsTr("Registra file")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: moduleFile
                                Layout.fillWidth: true
                                placeholderText: "/percorso/modulo.dylib"
                                selectByMouse: true
                            }
                            DsdrButton {
                                text: qsTr("Registra")
                                enabled: moduleFile.text.trim().length > 0
                                onClicked: {
                                    if (Session.addIqModuleFile(moduleFile.text))
                                        moduleFile.clear()
                                }
                            }
                        }
                    }

                    Text {
                        visible: Session.iqModuleDirectories.length > 0
                        text: qsTr("Cartelle cercate")
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                        color: Theme.textSecondary
                    }

                    Repeater {
                        model: Session.iqModuleDirectories

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true

                            Text {
                                text: modelData
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSmall
                                font.family: Theme.monoFamily
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }

                            DsdrButton {
                                // Le due cartelle standard restano: la UI
                                // non promette di cancellare una directory
                                // che il programma ricrea al prossimo avvio.
                                visible: Session.iqModuleDirectories.indexOf(modelData) >= 2
                                text: qsTr("Rimuovi")
                                fontSize: Theme.fontSmall
                                danger: true
                                onClicked: Session.removeIqModuleDirectory(modelData)
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                    }

                    Text {
                        visible: Session.iqModuleCatalog.length === 0
                        text: qsTr("Non è stato trovato alcun modulo IQ.")
                        color: Theme.textDisabled
                        font.pixelSize: Theme.fontSmall
                    }

                    Repeater {
                        model: Session.iqModuleCatalog

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: moduleLayout.implicitHeight + 2 * Theme.spacing
                            radius: Theme.radiusSmall
                            color: Theme.surface
                            border.width: 1
                            border.color: modelData.state === "error" ? Theme.danger : Theme.border

                            ColumnLayout {
                                id: moduleLayout
                                anchors.fill: parent
                                anchors.margins: Theme.spacing
                                spacing: Theme.spacingTight

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: modelData.name
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.fontNormal
                                        font.bold: true
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: modelData.state
                                        color: root.stateColor(modelData.state)
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                    }
                                }

                                Text {
                                    text: modelData.path
                                    color: Theme.textDisabled
                                    font.pixelSize: Theme.fontSmall
                                    font.family: Theme.monoFamily
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    visible: modelData.error.length > 0
                                    text: modelData.error
                                    color: Theme.danger
                                    font.pixelSize: Theme.fontSmall
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    Text {
                                        text: modelData.origin + (modelData.present ? "" : " · assente")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                        Layout.fillWidth: true
                                    }

                                    DsdrButton {
                                        text: modelData.enabled ? qsTr("Disabilita") : qsTr("Abilita")
                                        checkable: true
                                        checked: modelData.enabled
                                        fontSize: Theme.fontSmall
                                        onClicked: Session.setIqModuleEnabled(modelData.path,
                                                                             !modelData.enabled)
                                    }

                                    DsdrButton {
                                        visible: modelData.origin === "manual"
                                        text: qsTr("Dimentica")
                                        danger: true
                                        fontSize: Theme.fontSmall
                                        onClicked: Session.forgetIqModule(modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
