// SPDX-License-Identifier: GPL-3.0-or-later
// La colonna dei pannelli, e dentro di essa i canali RX.
//
// I pannelli non stanno in un ordine deciso da noi: si prendono per la
// maniglia e si mettono dove servono. Chi lavora in fonia tiene la sintonia in
// alto, chi caccia il DX vuole prima la macchina del tempo, e chi sta
// regolando l'immagine dello spettro vuole il waterfall sotto gli occhi.
// L'ordine si ricorda fra un avvio e l'altro, come lo stato di apertura.
//
// Tutta la colonna scorre. Prima scorreva soltanto l'elenco dei canali, e i
// pannelli sopra si prendevano l'altezza che volevano: su uno schermo basso
// gli ultimi comandi restavano sotto il bordo, irraggiungibili.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

Rectangle {
    id: root

    /// Il panadattatore da comandare dal pannello «Waterfall».
    ///
    /// Arriva da fuori invece di essere cercato: la colonna non sa dove viva
    /// lo spettro, e non deve saperlo.
    property PanadapterView panadapter: null

    color: Theme.surface
    border.width: 0

    // ── Ordine dei pannelli ──────────────────────────────────────────────
    //
    // Il modello è un ListModel e non una lista JavaScript perché `move()`
    // sposta i delegate invece di ricrearli: ricostruendoli, il trascinamento
    // in corso perderebbe sotto le dita l'oggetto che lo sta conducendo.
    ListModel {
        id: panelOrder

        ListElement { key: "sintonia" }
        ListElement { key: "tempo" }
        ListElement { key: "catena" }
        ListElement { key: "device" }
        ListElement { key: "waterfall" }
        ListElement { key: "canali" }
    }

    /// L'ordine corrente, per chi lo presidia con un test.
    readonly property alias panels: panelOrder

    /// Ordine salvato, come elenco di chiavi separate da virgola.
    property string savedOrder: ""

    Settings {
        category: "panels"
        property alias order: root.savedOrder
    }

    Component.onCompleted: restoreOrder()

    function restoreOrder() {
        if (savedOrder === "")
            return

        // Si riordina su ciò che esiste adesso, non su ciò che esisteva quando
        // l'ordine fu salvato: un pannello aggiunto da una versione nuova deve
        // comparire lo stesso, in fondo, invece di sparire perché non era
        // nell'elenco.
        const wanted = savedOrder.split(",")
        let target = 0
        for (const key of wanted) {
            for (let i = target; i < panelOrder.count; ++i) {
                if (panelOrder.get(i).key === key) {
                    if (i !== target)
                        panelOrder.move(i, target, 1)
                    ++target
                    break
                }
            }
        }
    }

    function storeOrder() {
        const keys = []
        for (let i = 0; i < panelOrder.count; ++i)
            keys.push(panelOrder.get(i).key)
        root.savedOrder = keys.join(",")
    }

    /// Sposta il pannello `from` là dove si trova il dito, in coordinate della
    /// scena. Il conto lo fa la colonna e non il pannello: è la colonna a
    /// sapere chi sta sopra a chi.
    function reorderTo(from, sceneY) {
        for (let i = 0; i < slots.count; ++i) {
            const item = slots.itemAt(i)
            if (!item || item.height <= 0)
                continue

            const top = item.mapToItem(null, 0, 0).y
            if (sceneY >= top && sceneY <= top + item.height) {
                if (i !== from)
                    panelOrder.move(from, i, 1)
                return
            }
        }
    }

    function componentFor(key) {
        switch (key) {
        case "sintonia":  return tuningPanel
        case "tempo":     return timeMachinePanel
        case "catena":    return rxChainPanel
        case "device":    return devicePanels
        case "waterfall": return waterfallPanel
        case "canali":    return channelsPanel
        default:          return null
        }
    }

    // ── La colonna ───────────────────────────────────────────────────────
    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: column

            width: flick.width
            spacing: Theme.spacing

            Repeater {
                id: slots
                model: panelOrder

                delegate: Item {
                    id: slot

                    required property int index
                    required property string key

                    width: column.width
                    // Un pannello nascosto non deve lasciare un buco: lo slot
                    // si chiude con lui.
                    height: loader.item && loader.item.visible ? loader.item.implicitHeight : 0
                    visible: height > 0

                    Loader {
                        id: loader
                        width: parent.width
                        sourceComponent: root.componentFor(slot.key)
                    }

                    // I pannelli che non sono una cornice nostra — quelli del
                    // backend — non emettono questi segnali: `ignoreUnknownSignals`
                    // lascia che restino dove sono senza far rumore.
                    Connections {
                        target: loader.item
                        ignoreUnknownSignals: true

                        function onDragMoved(sceneY) { root.reorderTo(slot.index, sceneY) }
                        function onDragEnded() { root.storeOrder() }
                    }
                }
            }
        }
    }

    // ── I pannelli ───────────────────────────────────────────────────────
    Component {
        id: tuningPanel

        FrequencyPanel {
            draggable: true
            visible: Session.connected
        }
    }

    Component {
        id: timeMachinePanel

        TimeMachinePanel {
            draggable: true
            visible: Session.connected
        }
    }

    Component {
        id: rxChainPanel

        RxChainPanel {
            visible: Session.connected
        }
    }

    Component {
        id: devicePanels

        BackendPanelHost {
            visible: Session.connected && Session.capabilities.nativePanels.length > 0
        }
    }

    Component {
        id: waterfallPanel

        PanelFrame {
            title: qsTr("Waterfall")
            draggable: true
            collapsed: true
            visible: root.panadapter !== null

            // Dietro un Loader e non direttamente: senza panadattatore —
            // capita nei test, e capiterebbe in una finestra staccata — i
            // binding di questi comandi leggerebbero proprietà di un oggetto
            // nullo, e ogni riga varrebbe un errore a runtime.
            Loader {
                Layout.fillWidth: true
                active: root.panadapter !== null

                sourceComponent: WaterfallControls {
                    panadapter: root.panadapter
                }
            }
        }
    }

    Component {
        id: channelsPanel

        PanelFrame {
            id: channelsFrame

            title: qsTr("CANALI")
            draggable: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                Text {
                    text: Session.channels.count + " / " + Session.capabilities.maxRxChannels
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.monoFamily
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                DsdrButton {
                    text: "+"
                    implicitWidth: 32
                    implicitHeight: 24
                    enabled: Session.connected
                             && Session.channels.count < Session.capabilities.maxRxChannels
                    onClicked: Session.addChannel(Session.centerFrequency)
                }
            }

            // Un Repeater e non una ListView: dentro una colonna che scorre
            // già, una seconda area scorrevole si contende il gesto, e il
            // primo giro di rotellina finisce sempre in quella sbagliata.
            Repeater {
                model: Session.channels

                delegate: ChannelCard {
                    Layout.fillWidth: true
                }
            }
        }
    }
}
