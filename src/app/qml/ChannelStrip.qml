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
import QtQml
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
        ListElement { key: "strumento" }
        ListElement { key: "audio" }
        ListElement { key: "flusso" }
        ListElement { key: "tempo" }
        ListElement { key: "catena" }
        ListElement { key: "trasmissione" }
        ListElement { key: "device" }
        ListElement { key: "waterfall" }
        ListElement { key: "canali" }
        ListElement { key: "greyline" }
    }

    /// Il nome e il glifo di ogni pannello, per la fila di icone.
    ///
    /// Sta qui e non nel modello dell'ordine perché quello lo si riordina e lo
    /// si salva: un'etichetta tradotta dentro un ListModel che finisce in una
    /// stringa di preferenze è un modo di ritrovarsela in italiano su un
    /// programma in inglese.
    readonly property var panelInfo: [
        { key: "sintonia",     glyph: "⌗", label: qsTr("Sintonia") },
        { key: "strumento",    glyph: "◔", label: qsTr("Strumento") },
        { key: "audio",        glyph: "♫", label: qsTr("Studio audio") },
        { key: "flusso",       glyph: "⇉", label: qsTr("Flusso") },
        { key: "tempo",        glyph: "⏱", label: qsTr("Macchina del tempo") },
        { key: "catena",       glyph: "⨍", label: qsTr("Catena RX") },
        { key: "trasmissione", glyph: "▲", label: qsTr("Trasmissione") },
        { key: "device",       glyph: "⚙", label: qsTr("Device") },
        { key: "waterfall",    glyph: "▤", label: qsTr("Waterfall") },
        { key: "canali",       glyph: "≡", label: qsTr("Canali") },
        { key: "greyline",     glyph: "◐", label: qsTr("Linea grigia") },
    ]

    // ── Pannelli spenti ──────────────────────────────────────────────────
    //
    // Chiuso e spento sono due cose diverse: chiuso è ridotto a una riga di
    // titolo, spento è via. Nove righe di titolo sono comunque nove righe, e
    // chi opera in fonia non ha bisogno della macchina del tempo.
    property string hiddenPanels: ""

    readonly property var hiddenKeys:
        hiddenPanels === "" ? [] : hiddenPanels.split(",")

    // ── Pannelli staccati ────────────────────────────────────────────────
    //
    // Staccato vuol dire «sta in una finestra sua», non «non c'è più»: esce
    // dalla colonna e la sua icona resta accesa, perché il pannello è vivo.
    property string detachedPanels: ""

    readonly property var detachedKeys:
        detachedPanels === "" ? [] : detachedPanels.split(",")

    function detachPanel(key) {
        if (detachedKeys.indexOf(key) >= 0)
            return
        const keys = detachedKeys.slice()
        keys.push(key)
        detachedPanels = keys.join(",")
    }

    function reattachPanel(key) {
        const keys = detachedKeys.slice()
        const at = keys.indexOf(key)
        if (at < 0)
            return
        keys.splice(at, 1)
        detachedPanels = keys.join(",")
    }

    function togglePanel(key) {
        // L'icona di un pannello staccato lo riporta davanti, non in colonna.
        //
        // È la domanda che ci si fa davvero: «dov'è finita quella finestra?».
        // Riportarla in colonna sarebbe una risposta a una domanda diversa, e
        // per quella c'è la croce della finestra stessa — che rimette il
        // pannello dov'era.
        const detachedAt = detachedKeys.indexOf(key)
        if (detachedAt >= 0) {
            const window = detachedWindows.objectAt(detachedAt)
            if (window)
                window.bringForward()
            else
                reattachPanel(key)   // la finestra non c'è più: si recupera
            return
        }

        const keys = hiddenKeys.slice()
        const at = keys.indexOf(key)
        if (at >= 0)
            keys.splice(at, 1)
        else
            keys.push(key)
        hiddenPanels = keys.join(",")
    }

    /// L'ordine corrente, per chi lo presidia con un test.
    readonly property alias panels: panelOrder

    /// Ordine salvato, come elenco di chiavi separate da virgola.
    property string savedOrder: ""

    Settings {
        category: "panels"
        property alias order: root.savedOrder
        property alias hidden: root.hiddenPanels
        property alias detached: root.detachedPanels
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

    /// Se il pannello ha senso adesso. Lo decide la colonna, e non il pannello
    /// con un `visible`, per una ragione che è costata una colonna vuota: in
    /// QML un figlio eredita l'invisibilità del padre, quindi far dipendere
    /// l'altezza dello slot dal `visible` del pannello chiude un anello —
    /// altezza zero, slot invisibile, pannello invisibile, altezza zero — da
    /// cui non si esce più. Qui il Loader non crea nemmeno l'oggetto.
    function slotActive(key) {
        // Spento vuol dire che non viene proprio creato: un pannello invisibile
        // ma vivo continuerebbe a tenere i suoi binding e, nel caso dello
        // studio audio, un secondo rendering su GPU per qualcosa che nessuno
        // sta guardando.
        if (hiddenKeys.indexOf(key) >= 0)
            return false

        // Staccato è vivo, ma altrove: la colonna non lo costruisce una
        // seconda volta. Due copie dello stesso pannello sarebbero due stati
        // da tenere allineati, e non lo sarebbero mai.
        if (detachedKeys.indexOf(key) >= 0)
            return false

        return panelIsRelevant(key)
    }

    /// Se il pannello ha qualcosa da mostrare, adesso.
    ///
    /// Separata da `slotActive` perché la stessa domanda la fa anche una
    /// finestra staccata, dove «spento» e «staccato» non si applicano: senza
    /// questa distinzione lo studio audio in finestra restava aperto e vuoto
    /// con la radio scollegata, mentre in colonna sarebbe sparito.
    function panelIsRelevant(key) {
        switch (key) {
        case "waterfall": return root.panadapter !== null
        case "canali":    return true
        // Lo strumento ha senso quando c'è una radio: quale dei due mostrare
        // — il segnale o la potenza — lo decide il pannello, e la colonna non
        // ha motivo di saperlo.
        case "strumento": return Session.connected
        // Lo studio audio ha senso solo con dell'audio che scorra.
        case "audio":     return Session.connected
        // Il diagramma di flusso vale solo con una catena viva: senza, i
        // blocchi ci sarebbero e le misure no.
        case "flusso":    return Session.connected
        case "device":    return Session.connected
                              && Session.capabilities.nativePanels.length > 0
        // Su un ricevitore puro il pannello TX non viene creato: la UI si
        // genera dalle capability, non le disabilita (CONSTITUTION §7).
        case "trasmissione": return Session.connected && Session.capabilities.canTransmit
        default:          return Session.connected
        }
    }

    function componentFor(key) {
        switch (key) {
        case "sintonia":  return tuningPanel
        case "strumento": return sMeterPanel
        case "audio":     return audioStudioPanel
        case "greyline":  return greylinePanel
        case "flusso":    return chainFlowPanel
        case "tempo":     return timeMachinePanel
        case "catena":    return rxChainPanel
        case "trasmissione": return txPanel
        case "device":    return devicePanels
        case "waterfall": return waterfallPanel
        case "canali":    return channelsPanel
        default:          return null
        }
    }

    // ── La fila di icone ─────────────────────────────────────────────────
    //
    // Fuori dall'area che scorre: è l'indice della colonna, e un indice che
    // scorre via insieme al contenuto non è un indice.
    PanelSwitchboard {
        id: switchboard

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacing

        panels: root.panelInfo
        hidden: root.hiddenKeys
        onToggled: (key) => root.togglePanel(key)
    }

    Rectangle {
        anchors.top: switchboard.bottom
        anchors.topMargin: Theme.spacingTight
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacing
        height: 1
        color: Theme.border
    }

    // ── La colonna ───────────────────────────────────────────────────────
    Flickable {
        id: flick

        anchors.top: switchboard.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        anchors.topMargin: Theme.spacingLoose
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
                    // Un pannello che non serve non lascia un buco: non viene
                    // proprio creato, e lo slot si chiude con lui.
                    height: loader.item ? loader.item.implicitHeight : 0

                    Loader {
                        id: loader
                        width: parent.width
                        active: root.slotActive(slot.key)
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
                        function onDetachRequested() { root.detachPanel(slot.key) }
                    }
                }
            }
        }
    }

    // ── Le finestre staccate ─────────────────────────────────────────────
    //
    // Un Instantiator e non un Repeater: le finestre non sono Item, e un
    // Repeater sa creare solo figli visuali. Le chiavi staccate possono essere
    // più d'una — chi stacca lo studio audio sul secondo schermo di solito ci
    // mette accanto anche la catena RX — e serve una finestra per ciascuna.
    Instantiator {
        id: detachedWindows

        model: root.detachedKeys

        delegate: PanelWindow {
            required property string modelData

            // La stessa domanda che si fa la colonna: un pannello che non ha
            // niente da mostrare non lo mostra, e lo dice.
            contentRelevant: root.panelIsRelevant(modelData)
            panelKey: modelData
            panelTitle: {
                for (const entry of root.panelInfo) {
                    if (entry.key === modelData)
                        return entry.label
                }
                return modelData
            }
            panelComponent: root.componentFor(modelData)
            onReattachRequested: root.reattachPanel(modelData)
        }
    }

    // ── I pannelli ───────────────────────────────────────────────────────
    Component {
        id: txPanel

        TxPanel {}
    }

    Component {
        id: tuningPanel

        FrequencyPanel {
            draggable: true
        }
    }

    Component {
        id: sMeterPanel

        SMeterPanel {}
    }

    Component {
        id: audioStudioPanel

        AudioStudioPanel {}
    }

    Component {
        id: chainFlowPanel

        ChainFlowPanel {}
    }

    Component {
        id: greylinePanel

        GreylinePanel {}
    }

    Component {
        id: timeMachinePanel

        TimeMachinePanel {
            draggable: true
        }
    }

    Component {
        id: rxChainPanel

        RxChainPanel {
            draggable: true
        }
    }

    Component {
        id: devicePanels

        BackendPanelHost {}
    }

    Component {
        id: waterfallPanel

        PanelFrame {
            title: qsTr("Waterfall")
            draggable: true
            collapsed: true

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
