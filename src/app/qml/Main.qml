// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — finestra principale.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import DecodiumSdr

ApplicationWindow {
    id: window

    // Dimensione desiderata, ridotta a ciò che lo schermo concede davvero.
    //
    // Il fattore di scala del sistema cambia tutto: un monitor da 1966×823
    // fisici al 175% offre appena 1123×443 punti, e una finestra "normale" da
    // 1440×860 non ci sta nemmeno lontanamente. Qui si parte dalla dimensione
    // comoda e si scende a quella possibile, lasciando un margine.
    readonly property int availableWidth: Screen.desktopAvailableWidth
    readonly property int availableHeight: Screen.desktopAvailableHeight

    width: Math.min(1440, availableWidth - 60)
    height: Math.min(860, availableHeight - 60)

    // I minimi devono stare dentro lo schermo più piccolo su cui vogliamo
    // girare, altrimenti sono loro a spingere la finestra fuori dal bordo:
    // è esattamente ciò che accadeva con un minimo di 560 punti su un
    // desktop che ne offre 443.
    minimumWidth: Math.min(720, availableWidth - 40)
    minimumHeight: Math.min(480, availableHeight - 40)
    visible: true
    color: Theme.background
    title: Session.connected
           ? qsTr("DECODIUM SDR — %1").arg(Session.deviceName)
           : qsTr("DECODIUM SDR")

    /// Da che parte sta la colonna dei pannelli. Si ricorda, come tutto il
    /// resto della disposizione: chi la sposta lo fa una volta sola.
    property bool panelsOnLeft: false

    Settings {
        category: "window"
        property alias panelsOnLeft: window.panelsOnLeft
    }

    // ── Barra dei menu ───────────────────────────────────────────────────
    menuBar: MainMenu {
        panadapter: scope.panadapterView
        panelsOnLeft: window.panelsOnLeft
        onSourceRequested: {
            if (!Session.discovering)
                Session.startDiscovery()
            discoveryPopup.open()
        }
        onAboutRequested: aboutPopup.open()
        onPanelSideToggled: window.panelsOnLeft = !window.panelsOnLeft
    }

    // ── Barra superiore ──────────────────────────────────────────────────
    header: Rectangle {
        implicitHeight: Theme.headerHeight
        color: Theme.surface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLoose
            anchors.rightMargin: Theme.spacingLoose
            spacing: Theme.spacingLoose

            Text {
                text: "DECODIUM SDR"
                font.pixelSize: Theme.fontLarge
                font.bold: true
                font.letterSpacing: 1.5
                color: Theme.accent
            }

            Rectangle {
                width: 1
                Layout.fillHeight: true
                Layout.topMargin: Theme.spacing
                Layout.bottomMargin: Theme.spacing
                color: Theme.border
            }

            // Frequenza centrale del device.
            ColumnLayout {
                spacing: 0

                Text {
                    text: qsTr("Centro")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                FrequencyDisplay {
                    frequencyHz: Session.centerFrequency
                    minimumHz: Session.capabilities.minFrequency
                    maximumHz: Session.capabilities.maxFrequency > 0
                               ? Session.capabilities.maxFrequency : 30000000
                    editable: Session.connected
                    digitSize: Theme.fontLarge
                    // `tuneTo` e non `centerFrequency`: muovere la sola
                    // finestra lasciava il ricevitore indietro, e da fuori
                    // banda un canale non si sente più — è così che RX 1 era
                    // finito lontano da tutto ciò che si vedeva.
                    onTuneRequested: (hz) => Session.tuneTo(hz)
                }
            }

            ColumnLayout {
                spacing: 0
                visible: Session.connected

                Text {
                    text: qsTr("Campionamento")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                DsdrComboBox {
                    implicitWidth: 132
                    implicitHeight: 24
                    model: Session.capabilities.sampleRates.map(r => (r / 1e6).toFixed(3) + " MS/s")
                    currentIndex: Session.capabilities.sampleRates.indexOf(Session.sampleRate)
                    onActivated: Session.sampleRate = Session.capabilities.sampleRates[currentIndex]
                }
            }

            Item { Layout.fillWidth: true }

            // Volume generale.
            RowLayout {
                spacing: Theme.spacing
                visible: Session.connected

                Text {
                    text: qsTr("Volume")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }

                DsdrSlider {
                    implicitWidth: 130
                    from: 0; to: 1
                    value: Session.audio.volume
                    onMoved: Session.audio.volume = value
                }
            }

            // Registrazione IQ: compare solo se la sorgente la consente.
            DsdrButton {
                visible: Session.connected && Session.audio.active
                text: Session.audioRecorder.recording
                      ? qsTr("■ WAV") : qsTr("● WAV")
                checkable: true
                checked: Session.audioRecorder.recording
                danger: Session.audioRecorder.recording
                implicitWidth: 76
                enabled: Session.connected && Session.audio.active
                onToggled: Session.toggleAudioRecording()
            }

            // Registrazione IQ: compare solo se la sorgente la consente.
            DsdrButton {
                visible: Session.capabilities.supportsRecording
                text: Session.recorder.recording
                      ? qsTr("■ %1").arg(Qt.formatTime(new Date(Session.recorder.durationMs), "mm:ss"))
                      : qsTr("● REC")
                checkable: true
                checked: Session.recorder.recording
                danger: Session.recorder.recording
                implicitWidth: 96
                enabled: Session.connected
                onToggled: Session.toggleRecording()
            }

            // §4.2: se il backend non trasmette, il PTT non esiste — non è
            // disabilitato, proprio non viene creato.
            DsdrButton {
                visible: Session.capabilities.canTransmit
                text: Session.transmitting ? qsTr("TX ON") : qsTr("PTT")
                checkable: true
                checked: Session.transmitting
                danger: true
                implicitWidth: 88
                enabled: Session.connected
                onToggled: Session.setPtt(checked)
            }

            DsdrButton {
                text: Session.connected ? qsTr("Disconnetti") : qsTr("Sorgente")
                onClicked: {
                    if (Session.connected)
                        Session.disconnectDevice()
                    else
                        discoveryPopup.open()
                }
            }
        }
    }

    // ── Corpo ────────────────────────────────────────────────────────────
    //
    // Il rail sta fuori dallo SplitView: è largo quanto è largo e non si
    // trascina. Dentro lo SplitView finirebbe fra le maniglie, e prima o poi
    // qualcuno lo ridurrebbe a una striscia inservibile.
    RowLayout {
        anchors.fill: parent
        spacing: 0

        CommandRail {
            Layout.fillHeight: true
            panadapter: scope.panadapterView
            scope: scope
        }

        // Lo SplitView si specchia invece di riordinare i figli: spostare un
        // elemento da una parte all'altra lo distruggerebbe e lo ricreerebbe,
        // e con lui se ne andrebbero lo zoom del panadattatore, la posizione
        // dello scorrimento e la storia del waterfall. `childrenInherit` resta
        // falso perché a specchiarsi dev'essere la disposizione delle due
        // colonne, non il contenuto: dentro, i cursori vanno ancora da sinistra
        // a destra e i numeri si leggono nello stesso verso.
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            LayoutMirroring.enabled: window.panelsOnLeft
            LayoutMirroring.childrenInherit: false

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.pressed ? Theme.accent
                     : SplitHandle.hovered ? Theme.borderStrong : Theme.border
            }

            PanadapterPane {
                id: scope
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
            }

            ChannelStrip {
                SplitView.preferredWidth: 340
                SplitView.minimumWidth: 300
                SplitView.maximumWidth: 520
                panadapter: scope.panadapterView
            }
        }
    }

    // ── Barra di stato ───────────────────────────────────────────────────
    footer: Rectangle {
        implicitHeight: 26
        color: Theme.surface

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLoose
            anchors.rightMargin: Theme.spacingLoose
            spacing: Theme.spacingLoose

            Rectangle {
                width: 8; height: 8; radius: 4
                color: Session.connected ? Theme.success : Theme.textDisabled
            }

            Text {
                text: Session.statusMessage
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            // La salute del collegamento, dove c'è una rete di mezzo.
            NetworkQuality {
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                visible: Session.connected
                text: qsTr("audio %1 · %2 ms").arg(Session.audio.deviceName).arg(Session.audio.latencyMs)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textDisabled
            }

            Text {
                visible: Session.iqModuleNames.length > 0
                text: qsTr("moduli IQ %1").arg(Session.iqModuleNames.join(", "))
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
                elide: Text.ElideRight
            }

            Text {
                visible: Session.iqModuleCatalog.length > 0
                text: qsTr("catalogo IQ %1")
                      .arg(Session.iqModuleCatalog.map(m => m.name).join(", "))
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
                elide: Text.ElideRight
                HoverHandler { id: moduleHover }
                ToolTip.visible: moduleHover.hovered
                ToolTip.text: Session.iqModuleCatalog.map(m => m.path).join("\n")
            }

            Text {
                text: Session.backendName
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }

            // L'ora UTC: è quella con cui si scrive un log e si dà un
            // appuntamento in banda, e nessuno vuole calcolarsela a mente.
            // Il selettore della lingua se n'è andato nel menu Strumenti, dove
            // stanno le cose che si scelgono una volta.
            Text {
                text: utcClock.stamp
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary

                Timer {
                    id: utcClock

                    property string stamp: ""

                    // Un secondo di risoluzione basta a un orologio da parete,
                    // e a un timer che gira per tutta la sessione conviene
                    // chiedere il minimo indispensabile.
                    interval: 1000
                    running: true
                    repeat: true
                    triggeredOnStart: true
                    onTriggered: stamp = new Date().toISOString().substring(11, 19) + " UTC"
                }
            }
        }
    }

    // ── Selezione sorgente ───────────────────────────────────────────────
    Popup {
        id: discoveryPopup

        anchors.centerIn: Overlay.overlay
        width: 560
        height: 520
        modal: true
        focus: true
        padding: 0

        background: Rectangle {
            color: "transparent"
        }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }

        contentItem: DiscoveryPane {}

        Connections {
            target: Session
            function onConnectionChanged() {
                if (Session.connected)
                    discoveryPopup.close()
            }
        }
    }

    // ── Informazioni ─────────────────────────────────────────────────────
    Popup {
        id: aboutPopup

        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        padding: Theme.spacingLoose

        background: Rectangle {
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.borderStrong
            radius: Theme.radius
        }

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.6)
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing

            Text {
                text: "DECODIUM SDR"
                font.pixelSize: Theme.fontLarge
                font.bold: true
                font.letterSpacing: 1.5
                color: Theme.accent
            }

            Text {
                text: qsTr("Client SDR universale dell'ecosistema DECODIUM.")
                font.pixelSize: Theme.fontNormal
                color: Theme.textPrimary
            }

            Text {
                text: qsTr("Backend attivo: %1").arg(Session.backendName)
                font.pixelSize: Theme.fontSmall
                font.family: Theme.monoFamily
                color: Theme.textSecondary
            }

            Text {
                text: "GPL-3.0-or-later · decodium.it"
                font.pixelSize: Theme.fontSmall
                color: Theme.textDisabled
            }

            DsdrButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Chiudi")
                onClicked: aboutPopup.close()
            }
        }
    }

    // Posizione e dimensione sopravvivono alla chiusura: chi opera dispone le
    // finestre come gli servono e non vuole rifarlo a ogni avvio.
    Settings {
        id: windowGeometry
        category: "window"
        property int x: -1
        property int y: -1
        property int width: 0
        property int height: 0
    }

    /// Riporta la finestra dentro l'area utile.
    ///
    /// Serve in due casi concreti: una geometria salvata su un monitor che
    /// ora non c'è più, e un cambio di risoluzione o di scala mentre
    /// l'applicazione era chiusa. In entrambi la finestra si ritroverebbe
    /// fuori schermo, dove non la si può nemmeno afferrare per riportarla
    /// indietro.
    function clampToScreen() {
        width = Math.max(minimumWidth, Math.min(width, availableWidth))
        height = Math.max(minimumHeight, Math.min(height, availableHeight))
        x = Math.max(0, Math.min(x, availableWidth - width))
        y = Math.max(0, Math.min(y, availableHeight - height))
    }

    onClosing: {
        windowGeometry.x = window.x
        windowGeometry.y = window.y
        windowGeometry.width = window.width
        windowGeometry.height = window.height
    }

    // Se lo schermo cambia mentre l'applicazione è aperta — un monitor
    // scollegato, una scala modificata — la finestra va riportata dentro.
    onAvailableWidthChanged: clampToScreen()
    onAvailableHeightChanged: clampToScreen()

    // All'avvio si parte dalla scelta della sorgente: senza device non c'è
    // nulla di sensato da mostrare.
    Component.onCompleted: {
        if (windowGeometry.width > 0 && windowGeometry.height > 0) {
            width = windowGeometry.width
            height = windowGeometry.height
        }
        if (windowGeometry.x >= 0 && windowGeometry.y >= 0) {
            x = windowGeometry.x
            y = windowGeometry.y
        } else {
            // Primo avvio: al centro dell'area utile.
            x = Math.max(0, (availableWidth - width) / 2)
            y = Math.max(0, (availableHeight - height) / 2)
        }
        clampToScreen()

        if (Session.connected || Session.discovering)
            return
        Session.startDiscovery()
        discoveryPopup.open()
    }
}
