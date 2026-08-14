// SPDX-License-Identifier: GPL-3.0-or-later
// Selezione del backend e dei device trovati.
//
// La pagina è identica per ogni backend: elenca ciò che la discovery emette e
// mostra le capability dichiarate. Nessun ramo sul tipo di radio.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLoose
        spacing: Theme.spacing

        Text {
            text: qsTr("Sorgente")
            font.pixelSize: Theme.fontLarge
            font.bold: true
            color: Theme.textPrimary
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            DsdrComboBox {
                id: backendSelector
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: Session.availableBackends
                onActivated: Session.selectBackend(currentValue)

                Component.onCompleted: {
                    for (let i = 0; i < model.length; ++i) {
                        if (model[i].id === Session.backendId) {
                            currentIndex = i
                            break
                        }
                    }
                }
            }

            DsdrButton {
                text: Session.discovering ? qsTr("Ricerca…") : qsTr("Cerca")
                enabled: !Session.discovering
                onClicked: Session.startDiscovery()
            }

            // Cerca in rete anche le famiglie che questa versione non sa
            // ancora aprire. È una domanda diversa da «quali sorgenti posso
            // usare», e per questo è un pulsante diverso.
            DsdrButton {
                text: Session.scoutingNetwork ? qsTr("In rete…") : qsTr("Cerca in rete")
                enabled: !Session.scoutingNetwork
                onClicked: Session.scoutNetwork()
            }
        }


        // ── Dichiara la radio a mano (SPEC-004) ──────────────────────────
        //
        // Il rilevamento sonda le porte e annuncia una radio solo quando
        // qualcuno risponde. È la cosa giusta — non si mette nell'elenco un
        // apparato che non c'è — ma lascia fuori il caso più frequente che
        // esista: **la porta è occupata da un altro programma**. Su una
        // stazione dove gira anche DECODIUM 4 la seriale ce l'ha lui, la sonda
        // trova «Accesso negato», e l'elenco resta vuoto senza che si possa
        // fare niente.
        //
        // Da qui la radio si dichiara invece di cercarla. Non è un
        // aggiramento: è il caso in cui il rilevamento non può funzionare, e
        // chi opera la propria stazione sa che radio ha molto meglio di
        // quanto possa scoprirlo una sonda.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            spacing: Theme.spacingTight
            // La capability, non il nome del backend: la UI non deve sapere
            // che radio ci sia dall'altra parte (CONSTITUTION §7).
            visible: Session.capabilities.manualDeviceEntry

            property var ports: Session.capabilities.manualDeviceEntry
                                ? (Session.nativeCommand("device.serialPorts", {}) || [])
                                : []
            property var drivers: Session.capabilities.manualDeviceEntry
                                  ? (Session.nativeCommand("device.catDrivers", {}) || [])
                                  : []

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight

                Text {
                    text: qsTr("DICHIARA LA RADIO")
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Theme.textDisabled
                }

                Item { Layout.fillWidth: true }

                DsdrButton {
                    implicitWidth: 72
                    implicitHeight: 24
                    fontSize: Theme.fontSmall
                    text: qsTr("AGGIORNA")
                    onClicked: {
                        parent.parent.ports =
                            Session.nativeCommand("device.serialPorts", {}) || []
                    }
                }
            }

            // Il driver: è la cosa che cambia di più fra una radio e l'altra,
            // e sbagliarlo dà una radio che compare e non risponde a niente.
            DsdrComboBox {
                id: driverBox

                Layout.fillWidth: true
                model: parent.drivers.map(function(d) { return d.label })
            }

            // Le porte, **comprese quelle occupate**. Una tendina senza COM5
            // è un mistero; «COM5 · occupata» è un'informazione, e dice pure
            // da che parte guardare.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: driverBox.currentIndex >= 0
                         && parent.drivers.length > driverBox.currentIndex
                         && parent.drivers[driverBox.currentIndex].id !== "rigctld"

                DsdrComboBox {
                    id: portBox

                    Layout.fillWidth: true
                    model: {
                        const list = []
                        for (let i = 0; i < parent.parent.ports.length; ++i) {
                            const p = parent.parent.ports[i]
                            list.push(p.busy
                                      ? qsTr("%1 · occupata — %2").arg(p.port)
                                            .arg(p.description)
                                      : qsTr("%1 · %2").arg(p.port).arg(p.description))
                        }
                        return list
                    }
                }

                // Zero significa «prova tutte»: è la differenza fra «scegli la
                // radio e funziona» e una tendina di velocità da indovinare.
                DsdrComboBox {
                    id: baudBox

                    Layout.preferredWidth: 110
                    model: [qsTr("automatica"), "4800", "9600", "19200",
                            "38400", "57600", "115200"]
                }
            }

            // L'indirizzo del demone, quando il driver è rigctld: là non c'è
            // una porta seriale e una velocità non vuol dire niente.
            TextField {
                id: netField

                Layout.fillWidth: true
                visible: driverBox.currentIndex >= 0
                         && parent.drivers.length > driverBox.currentIndex
                         && parent.drivers[driverBox.currentIndex].id === "rigctld"
                text: "127.0.0.1:4532"
                placeholderText: qsTr("indirizzo:porta del demone")
                font.pixelSize: Theme.fontNormal
                font.family: Theme.monoFamily
                color: Theme.textPrimary
                placeholderTextColor: Theme.textDisabled
                selectByMouse: true

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceSunken
                    border.width: 1
                    border.color: netField.activeFocus ? Theme.accent : Theme.border
                }
            }

            DsdrButton {
                Layout.fillWidth: true
                implicitHeight: 26
                text: qsTr("METTI NELL'ELENCO")
                enabled: driverBox.currentIndex >= 0
                onClicked: {
                    const drivers = parent.drivers
                    if (driverBox.currentIndex >= drivers.length)
                        return
                    const driver = drivers[driverBox.currentIndex].id
                    const ports = parent.ports

                    let port = netField.text
                    let baud = 0
                    if (driver !== "rigctld") {
                        if (portBox.currentIndex < 0 || portBox.currentIndex >= ports.length)
                            return
                        port = ports[portBox.currentIndex].port
                        baud = baudBox.currentIndex > 0
                               ? parseInt(baudBox.currentText) : 0
                    }

                    Session.nativeCommand("device.declare", {
                        "driver": driver, "port": port, "baud": baud
                    })
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("La radio compare nell'elenco qui sopra e si apre come le altre. Se la porta è occupata da un altro programma, il collegamento fallirà dicendolo.")
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
                wrapMode: Text.WordWrap
            }
        }

        // ── Radio viste in rete ──────────────────────────────────────────
        //
        // Non sono sorgenti: sono radio che esistono e che non sappiamo
        // ancora aprire. Compaiono qui e non nell'elenco dei device perché
        // quello elenca ciò che si può usare — ma sapere che la radio c'è, a
        // quale indirizzo e con quale firmware, è la sola risposta possibile
        // alla domanda «l'ho collegata, perché non la vedo?».
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingTight
            visible: Session.networkRadios.length > 0

            Text {
                text: qsTr("Trovate in rete")
                font.pixelSize: Theme.fontSmall
                font.bold: true
                color: Theme.textSecondary
            }

            Repeater {
                model: Session.networkRadios

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: found.implicitHeight + 2 * Theme.spacing
                    radius: Theme.radiusSmall
                    color: Theme.surfaceSunken
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: found
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: Theme.spacing
                        spacing: 2

                        Text {
                            text: modelData.family + " · " + modelData.model
                            font.pixelSize: Theme.fontNormal
                            color: Theme.textPrimary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.address
                                  + (modelData.detail ? " — " + modelData.detail : "")
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingTight

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Riconosciuta, non ancora apribile da questa versione.")
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textDisabled
                                wrapMode: Text.WordWrap
                            }

                            // Per il Flex si può fare un passo in più: aprire
                            // il canale di comando e farsi dire chi è. Non
                            // riceve campioni, ma distingue un problema di
                            // rete da un problema di programma.
                            DsdrButton {
                                visible: modelData.describable === true
                                implicitWidth: 96
                                implicitHeight: 24
                                fontSize: Theme.fontSmall
                                text: qsTr("Interroga")
                                onClicked: Session.probeNetworkRadio(modelData.address)
                            }
                        }
                    }
                }
            }
        }

        // Compare solo per le sorgenti che stanno dietro una rete: è una
        // capability, non un ramo sul tipo di backend (CONSTITUTION §7).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            visible: Session.capabilities.remoteCapable

            TextField {
                id: endpointField

                Layout.fillWidth: true
                placeholderText: qsTr("indirizzo:porta  (es. 192.168.1.20:1234)")
                color: Theme.textPrimary
                placeholderTextColor: Theme.textDisabled
                font.pixelSize: Theme.fontNormal
                selectByMouse: true

                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surfaceSunken
                    border.width: 1
                    border.color: endpointField.activeFocus ? Theme.accent : Theme.border
                }

                onAccepted: addEndpointButton.clicked()
            }

            DsdrButton {
                id: addEndpointButton
                text: qsTr("Aggiungi")
                enabled: endpointField.text.trim().length > 0
                onClicked: {
                    if (Session.addRemoteEndpoint(endpointField.text)) {
                        endpointField.clear()
                        Session.startDiscovery()
                    }
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            spacing: Theme.spacingTight
            model: Session.devices

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: deviceEntry

                required property int index
                required property string displayName
                required property string model
                required property string serial
                required property string transport

                width: ListView.view.width
                height: 56
                radius: Theme.radiusSmall
                color: mouse.containsMouse ? Theme.surfaceRaised : Theme.surfaceSunken
                border.width: 1
                border.color: mouse.containsMouse ? Theme.accent : Theme.border

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onDoubleClicked: Session.connectToDevice(deviceEntry.index)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: deviceEntry.displayName
                            font.pixelSize: Theme.fontNormal
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: deviceEntry.transport + "  ·  " + deviceEntry.serial
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.monoFamily
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    DsdrButton {
                        text: qsTr("Connetti")
                        onClicked: Session.connectToDevice(deviceEntry.index)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: parent.count === 0 && !Session.discovering
                text: qsTr("Premi «Cerca» per elencare i device")
                font.pixelSize: Theme.fontNormal
                color: Theme.textDisabled
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: Session.discovering
                visible: running
            }
        }

        // ── Capability dichiarate dal backend (§4.2) ─────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: capsFlow.implicitHeight + 2 * Theme.spacing
            radius: Theme.radiusSmall
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border

            Flow {
                id: capsFlow
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacingTight

                Repeater {
                    model: [
                        { text: qsTr("%1 canali RX").arg(Session.capabilities.maxRxChannels), on: true },
                        { text: qsTr("RX coerenti"), on: Session.capabilities.coherentRx },
                        { text: qsTr("TX"), on: Session.capabilities.canTransmit },
                        { text: qsTr("DSP client"), on: Session.capabilities.clientDemod },
                        { text: qsTr("Spettro client"), on: Session.capabilities.clientSpectrum },
                        { text: qsTr("Remoto"), on: Session.capabilities.remoteCapable },
                        { text: qsTr("Registrazione"), on: Session.capabilities.supportsRecording },
                    ]

                    delegate: Rectangle {
                        required property var modelData

                        visible: modelData.on
                        width: capText.implicitWidth + 2 * Theme.spacing
                        height: 22
                        radius: 11
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                        border.width: 1
                        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

                        Text {
                            id: capText
                            anchors.centerIn: parent
                            text: parent.modelData.text
                            font.pixelSize: Theme.fontSmall
                            color: Theme.accent
                        }
                    }
                }
            }
        }
    }
}
