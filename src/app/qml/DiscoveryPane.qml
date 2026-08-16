// SPDX-License-Identifier: GPL-3.0-or-later
// Selezione del backend e dei device trovati.
//
// La pagina è identica per ogni backend: elenca ciò che la discovery emette e
// mostra le capability dichiarate. Nessun ramo sul tipo di radio.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
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
            id: manualRadioEntry

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
            // Una radio dichiarata non è una preferenza effimera: porta,
            // formato seriale e linee di controllo sono parte della sua
            // identità operativa. Senza questa memoria, a ogni riavvio si
            // rischia di selezionare il primo adattatore della lista invece
            // del CAT della radio.
            property bool savedProfileDeclared: false
            readonly property bool serialDriver: driverBox.currentIndex >= 0
                                                 && drivers.length > driverBox.currentIndex
                                                 && drivers[driverBox.currentIndex].id !== "rigctld"
            readonly property bool hamlibLocalDriver: driverBox.currentIndex >= 0
                                                      && drivers.length > driverBox.currentIndex
                                                      && drivers[driverBox.currentIndex].id === "hamlib-local"
            readonly property string manualEntryAction:
                Session.nativeCommand("device.manualEntryAction", {})
                || qsTr("METTI NELL'ELENCO")
            readonly property string manualEntryHint:
                Session.nativeCommand("device.manualEntryHint", {}) || ""
            // L'elenco arriva dal rigctld installato: non promettiamo una
            // radio in una lista scritta a mano se Hamlib non la supporta.
            property var hamlibModels: hamlibLocalDriver
                                      ? (Session.nativeCommand("device.hamlibModels", {}) || [])
                                      : []
            property var filteredHamlibModels: {
                const needle = hamlibModelSearch.text.trim().toLowerCase()
                if (!needle)
                    return hamlibModels
                const found = []
                for (let i = 0; i < hamlibModels.length; ++i) {
                    const candidate = hamlibModels[i]
                    if (String(candidate.id).indexOf(needle) >= 0
                            || String(candidate.label).toLowerCase().indexOf(needle) >= 0)
                        found.push(candidate)
                }
                return found
            }

            Settings {
                id: manualRadioSettings

                category: "radio/manual-cat"
                property string driverId: ""
                property string port: ""
                property int baud: 0
                property int dataBits: 8
                property int parity: 0
                property int stopBits: 1
                property int flowControl: -1
                property bool dtr: false
                property bool rts: false
                property int hamlibModelId: 2011
            }

            function driverIndex(driverId) {
                for (let i = 0; i < drivers.length; ++i) {
                    if (drivers[i].id === driverId)
                        return i
                }
                return -1
            }

            function portIndex(portName) {
                for (let i = 0; i < ports.length; ++i) {
                    if (ports[i].port === portName)
                        return i
                }
                return -1
            }

            function indexOf(model, value) {
                const text = String(value)
                for (let i = 0; i < model.length; ++i) {
                    if (String(model[i]) === text)
                        return i
                }
                return -1
            }

            function hamlibModelIndex(modelId) {
                const id = Number(modelId)
                for (let i = 0; i < filteredHamlibModels.length; ++i) {
                    if (Number(filteredHamlibModels[i].id) === id)
                        return i
                }
                return -1
            }

            function selectedHamlibModelId() {
                if (hamlibLocalDriver && hamlibModelBox.currentIndex >= 0
                        && hamlibModelBox.currentIndex < filteredHamlibModels.length)
                    return Number(filteredHamlibModels[hamlibModelBox.currentIndex].id)
                return manualRadioSettings.hamlibModelId
            }

            function currentProfile() {
                const driversList = drivers
                if (driverBox.currentIndex < 0 || driverBox.currentIndex >= driversList.length)
                    return null

                const driver = driversList[driverBox.currentIndex].id
                let model = driverBox.currentText
                let hamlibModel = 0
                if (hamlibLocalDriver) {
                    if (hamlibModelBox.currentIndex < 0
                            || hamlibModelBox.currentIndex >= filteredHamlibModels.length)
                        return null
                    const selectedModel = filteredHamlibModels[hamlibModelBox.currentIndex]
                    model = selectedModel.label
                    hamlibModel = Number(selectedModel.id)
                    if (!hamlibModel)
                        return null
                }
                let port = netField.text.trim()
                let baud = 0
                if (serialDriver) {
                    if (portBox.currentIndex < 0 || portBox.currentIndex >= ports.length)
                        return null
                    port = ports[portBox.currentIndex].port
                    baud = baudBox.currentIndex > 0 ? parseInt(baudBox.currentText) : 0
                }
                if (!port)
                    return null

                return {
                    "driver": driver,
                    "model": model,
                    "hamlibModel": hamlibModel,
                    "port": port,
                    "baud": baud,
                    "dataBits": parseInt(dataBitsBox.currentText),
                    "parity": parityBox.currentIndex,
                    "stopBits": [1, 15, 2][stopBitsBox.currentIndex],
                    "flowControl": handshakeBox.currentIndex - 1,
                    "dtr": dtrButton.checked,
                    "rts": rtsButton.checked
                }
            }

            function saveProfile(profile) {
                manualRadioSettings.driverId = profile.driver
                manualRadioSettings.port = profile.port
                manualRadioSettings.baud = profile.baud
                manualRadioSettings.dataBits = profile.dataBits
                manualRadioSettings.parity = profile.parity
                manualRadioSettings.stopBits = profile.stopBits
                manualRadioSettings.flowControl = profile.flowControl
                manualRadioSettings.dtr = profile.dtr
                manualRadioSettings.rts = profile.rts
                manualRadioSettings.hamlibModelId = profile.hamlibModel
            }

            function applyDriverDefaults() {
                if (driverBox.currentIndex < 0 || driverBox.currentIndex >= drivers.length)
                    return
                const driver = drivers[driverBox.currentIndex].id
                if (driver === "hamlib-local" && !hamlibModelSearch.text.trimmed())
                    hamlibModelSearch.text = String(manualRadioSettings.hamlibModelId || 2011)
                const hamlibModel = selectedHamlibModelId()
                const defaults = Session.nativeCommand("device.catDefaults",
                                                       { "driver": driver,
                                                         "hamlibModel": hamlibModel }) || ({})
                if (Object.keys(defaults).length === 0)
                    return
                const baud = indexOf(baudBox.model, defaults.baud)
                baudBox.currentIndex = baud >= 0 ? baud : baudBox.currentIndex
                const dataBits = indexOf(dataBitsBox.model, defaults.dataBits)
                dataBitsBox.currentIndex = dataBits >= 0 ? dataBits : dataBitsBox.currentIndex
                parityBox.currentIndex = defaults.parity
                const stopBits = indexOf([1, 15, 2], defaults.stopBits)
                stopBitsBox.currentIndex = stopBits >= 0 ? stopBits : stopBitsBox.currentIndex
                handshakeBox.currentIndex = defaults.flowControl + 1
                dtrButton.checked = defaults.dtr === true
                rtsButton.checked = defaults.rts === true
                if (driver === "hamlib-local" && hamlibModel)
                    manualRadioSettings.hamlibModelId = hamlibModel
            }

            // Ripristina anche la selezione grafica: vedere "debug-console"
            // mentre l'app sta per aprire un IC-7300 salvato è ambiguo quanto
            // non conservare nulla. Il profilo viene dichiarato, ma non
            // aperto: la porta seriale resta intatta fino a «Connetti».
            function restoreSavedProfile() {
                if (!visible || savedProfileDeclared || !manualRadioSettings.driverId)
                    return

                const driver = driverIndex(manualRadioSettings.driverId)
                if (driver < 0)
                    return
                driverBox.currentIndex = driver

                if (serialDriver) {
                    const port = portIndex(manualRadioSettings.port)
                    if (port < 0)
                        return // radio scollegata: non si sostituisce con una porta a caso
                    portBox.currentIndex = port
                    const baud = manualRadioSettings.baud > 0
                                 ? indexOf(baudBox.model, manualRadioSettings.baud) : 0
                    baudBox.currentIndex = baud >= 0 ? baud : 0
                } else {
                    netField.text = manualRadioSettings.port
                }

                if (hamlibLocalDriver) {
                    hamlibModelSearch.text = String(manualRadioSettings.hamlibModelId)
                    const modelIndex = hamlibModelIndex(manualRadioSettings.hamlibModelId)
                    hamlibModelBox.currentIndex = modelIndex >= 0 ? modelIndex : 0
                }

                const dataBits = indexOf(dataBitsBox.model, manualRadioSettings.dataBits)
                dataBitsBox.currentIndex = dataBits >= 0 ? dataBits : 0
                parityBox.currentIndex = Math.max(0, Math.min(parityBox.model.length - 1,
                                                               manualRadioSettings.parity))
                stopBitsBox.currentIndex = indexOf([1, 15, 2], manualRadioSettings.stopBits)
                if (stopBitsBox.currentIndex < 0)
                    stopBitsBox.currentIndex = 0
                handshakeBox.currentIndex = Math.max(0, Math.min(handshakeBox.model.length - 1,
                                                                   manualRadioSettings.flowControl + 1))
                dtrButton.checked = manualRadioSettings.dtr
                rtsButton.checked = manualRadioSettings.rts

                const profile = currentProfile()
                if (profile && Session.nativeCommand("device.declare", profile))
                    savedProfileDeclared = true
            }

            Timer {
                id: restoreSavedProfileTimer

                interval: 0
                repeat: false
                onTriggered: manualRadioEntry.restoreSavedProfile()
            }

            Component.onCompleted: restoreSavedProfileTimer.start()
            onVisibleChanged: {
                if (visible)
                    restoreSavedProfileTimer.restart()
                else
                    savedProfileDeclared = false
            }
            // In caso di cambio backend, la lista arriva dopo che il pannello
            // è diventato visibile. Ritentiamo solo finché non è stata
            // dichiarata la radio salvata, senza sovrascrivere una scelta che
            // l'operatore sta modificando a mano.
            onPortsChanged: {
                if (visible && !savedProfileDeclared)
                    restoreSavedProfileTimer.restart()
            }
            onDriversChanged: {
                if (visible && !savedProfileDeclared)
                    restoreSavedProfileTimer.restart()
            }

            // Un nome di chip, da solo, non identifica il cavo giusto quando
            // ci sono più convertitori USB-seriali. Aggiungiamo quindi
            // produttore e seriale USB: la radio può esporre qui il proprio
            // modello/numero di serie (per esempio "IC-7300 03018172").
            function serialPortLabel(port) {
                const detail = []
                if (port.description)
                    detail.push(port.description)
                if (port.manufacturer && port.manufacturer !== port.description)
                    detail.push(port.manufacturer)
                if (port.serial)
                    detail.push(port.serial)
                const suffix = detail.join(" · ") || qsTr("porta seriale")
                return port.busy
                       ? qsTr("%1 · occupata — %2").arg(port.port).arg(suffix)
                       : qsTr("%1 · %2").arg(port.port).arg(suffix)
            }

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
                onActivated: manualRadioEntry.applyDriverDefaults()
            }

            // Un unico driver locale copre tutte le radio nel catalogo della
            // versione Hamlib installata. La ricerca evita una tendina con
            // centinaia di modelli e conserva l'ID esatto nel profilo.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: manualRadioEntry.hamlibLocalDriver

                Text {
                    text: qsTr("MODELLO HAMLIB")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textDisabled
                }

                TextField {
                    id: hamlibModelSearch
                    Layout.fillWidth: true
                    placeholderText: qsTr("cerca modello o ID — es. TS-940 o 2011")
                    font.pixelSize: Theme.fontNormal
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textDisabled
                    selectByMouse: true

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surfaceSunken
                        border.width: 1
                        border.color: hamlibModelSearch.activeFocus ? Theme.accent : Theme.border
                    }
                }

                DsdrComboBox {
                    id: hamlibModelBox
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "id"
                    model: manualRadioEntry.filteredHamlibModels
                    onActivated: {
                        const selected = manualRadioEntry.selectedHamlibModelId()
                        if (selected > 0) {
                            manualRadioSettings.hamlibModelId = selected
                            manualRadioEntry.applyDriverDefaults()
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: manualRadioEntry.hamlibModels.length === 0
                    text: qsTr("Hamlib/rigctld non e' disponibile: installalo o imposta DSDR_RIGCTLD_BIN.")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.warning
                    wrapMode: Text.WordWrap
                }
            }

            // Le porte, **comprese quelle occupate**. Una tendina senza COM5
            // è un mistero; «COM5 · occupata» è un'informazione, e dice pure
            // da che parte guardare.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTight
                visible: manualRadioEntry.serialDriver

                DsdrComboBox {
                    id: portBox

                    Layout.fillWidth: true
                    model: {
                        const list = []
                        for (let i = 0; i < parent.parent.ports.length; ++i) {
                            const p = parent.parent.ports[i]
                            list.push(manualRadioEntry.serialPortLabel(p))
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
                    currentIndex: 0
                }
            }

            // Una velocità sola non descrive una seriale. Il valore di
            // fabbrica resta 8N1 senza linee attive, così collegare un cavo
            // non può far partire il PTT; ogni altro valore qui sotto viene
            // passato al driver senza essere reinterpretato dalla UI.
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: Theme.spacingTight
                rowSpacing: Theme.spacingTight
                visible: manualRadioEntry.serialDriver

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("BIT DATI")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrComboBox {
                        id: dataBitsBox

                        Layout.fillWidth: true
                        model: ["8", "7", "6", "5"]
                        currentIndex: 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("PARITÀ")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrComboBox {
                        id: parityBox

                        Layout.fillWidth: true
                        model: [qsTr("nessuna"), qsTr("pari"), qsTr("dispari"),
                                qsTr("mark"), qsTr("space")]
                        currentIndex: 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("BIT STOP")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrComboBox {
                        id: stopBitsBox

                        Layout.fillWidth: true
                        model: ["1", "1,5", "2"]
                        currentIndex: 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("HANDSHAKE")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrComboBox {
                        id: handshakeBox

                        Layout.fillWidth: true
                        model: [qsTr("automatico"), qsTr("nessuno"),
                                "RTS/CTS", "XON/XOFF"]
                        currentIndex: 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("DTR")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrButton {
                        id: dtrButton

                        Layout.fillWidth: true
                        checkable: true
                        text: checked ? qsTr("DTR ON") : qsTr("DTR OFF")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: qsTr("RTS")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textDisabled
                    }

                    DsdrButton {
                        id: rtsButton

                        Layout.fillWidth: true
                        checkable: true
                        enabled: handshakeBox.currentIndex !== 2
                        text: handshakeBox.currentIndex === 2
                              ? qsTr("GESTITO")
                              : (checked ? qsTr("RTS ON") : qsTr("RTS OFF"))
                    }
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
                text: manualRadioEntry.manualEntryAction
                enabled: driverBox.currentIndex >= 0
                onClicked: {
                    const profile = manualRadioEntry.currentProfile()
                    if (!profile)
                        return
                    manualRadioEntry.saveProfile(profile)
                    if (Session.nativeCommand("device.declare", profile))
                        manualRadioEntry.savedProfileDeclared = true
                }
            }

            Text {
                Layout.fillWidth: true
                text: manualRadioEntry.manualEntryHint.length > 0
                      ? manualRadioEntry.manualEntryHint
                      : manualRadioEntry.serialDriver
                      ? qsTr("Il profilo CAT viene salvato quando lo metti nell'elenco e ricompare al prossimo avvio senza aprire la porta. Predefiniti sicuri: 8N1 e DTR/RTS bassi. L'handshake automatico prova prima nessuno; con RTS/CTS, RTS è gestito dal protocollo. Se la porta è occupata da un altro programma, il collegamento fallirà dicendolo.")
                      : qsTr("La radio compare nell'elenco qui sopra e si apre come le altre. Se il demone di rete non risponde, il collegamento fallirà dicendolo.")
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
                placeholderText: qsTr("rtl_tcp host:porta, tcp://, udp:// o sdrpp://")
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
