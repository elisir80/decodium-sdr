// SPDX-License-Identifier: GPL-3.0-or-later
// Barra dei menu.
//
// Fino a ieri le impostazioni non avevano una casa: la lingua stava in fondo
// alla barra di stato, la resa dello spettro in un riquadro flottante, il
// resto da nessuna parte. Qui dentro va quello che non si usa ogni minuto ma
// deve esistere — e ci va una volta sola, così chi lo cerca sa dove guardare.
//
// Non ci sono voci che non fanno nulla: un menu pieno di comandi grigi è un
// modo elaborato per far perdere tempo.
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

MenuBar {
    id: root
    objectName: "mainMenu"

    /// Il panadattatore, per i comandi di vista.
    required property PanadapterView panadapter

    /// Da che parte sta la colonna dei pannelli. La finestra la possiede, il
    /// menu la commuta e basta.
    property bool panelsOnLeft: false

    /// Aperture richieste alla finestra: il menu non sa dove vivano quei
    /// pannelli, si limita a chiederlo.
    signal quitRequested()
    signal sourceRequested()
    signal aboutRequested()
    signal operationsRequested()
    signal iqAnalysisRequested()
    signal adsbRequested()
    signal aprsRequested()
    signal aisRequested()
    signal pocsagRequested()
    signal acarsRequested()
    signal vdl2Requested()
    property bool vdl2Available: false
    signal vorRequested()
    signal selcalRequested()
    signal dscRequested()
    signal navtexRequested()
    signal panelSideToggled()

    // Popup is hosted in the window overlay, so retry remains accessible even
    // while the menu and main content are disabled during shutdown.
    property BackendShutdownDialog shutdownPrompt: BackendShutdownDialog {
        visible: Session.shutdownPending && !Session.shutdownFencePassed
        statusText: Session.shutdownBackendMessage || qsTr("Arresto della radio in corso.")
        retryAllowed: Session.shutdownBackendRetryAllowed
        onRetryRequested: Session.retryBackendShutdown()
    }

    background: Rectangle {
        color: Theme.surface

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    delegate: MenuBarItem {
        id: menuBarItem

        contentItem: Text {
            text: menuBarItem.text
            font.pixelSize: Theme.fontSmall
            color: menuBarItem.enabled ? Theme.textSecondary : Theme.textDisabled
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: menuBarItem.highlighted ? Theme.surfaceRaised : "transparent"
        }
    }

    // Le voci dei menu hanno il loro aspetto perché lo stile Basic non ne ha
    // uno: senza questo comparirebbero grigie di sistema, fuori dal tema.
    component ThemedMenu: Menu {
        id: themedMenu

        implicitWidth: 220

        background: Rectangle {
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusSmall
        }

        delegate: MenuItem {
            id: menuItem

            implicitHeight: 26

            contentItem: Item {
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: menuItem.text
                    font.pixelSize: Theme.fontSmall
                    color: menuItem.enabled ? Theme.textPrimary : Theme.textDisabled
                }

                // Il segno di spunta al posto di un indicatore separato: le
                // voci a due stati sono poche e la riga resta compatta.
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    visible: menuItem.checkable && menuItem.checked
                    text: "✓"
                    font.pixelSize: Theme.fontSmall
                    color: Theme.accent
                }
            }

            background: Rectangle {
                color: menuItem.highlighted ? Theme.surface : "transparent"
            }
        }
    }

    ThemedMenu {
        title: qsTr("File")

        MenuItem {
            text: qsTr("Sorgente…")
            onTriggered: root.sourceRequested()
        }

        MenuItem {
            text: qsTr("Disconnetti")
            enabled: Session.connected
            onTriggered: Session.disconnectDevice()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Esci")
            onTriggered: root.quitRequested()
        }
    }

    ThemedMenu {
        title: qsTr("Vista")

        MenuItem {
            text: qsTr("Waterfall piatto")
            checkable: true
            checked: root.panadapter.waterfallMode === PanadapterView.Flat
            onTriggered: root.panadapter.waterfallMode = PanadapterView.Flat
        }

        MenuItem {
            text: qsTr("Waterfall in rilievo")
            checkable: true
            checked: root.panadapter.waterfallMode === PanadapterView.Relief
            onTriggered: root.panadapter.waterfallMode = PanadapterView.Relief
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Scala automatica")
            checkable: true
            checked: root.panadapter.autoRange
            onTriggered: root.panadapter.autoRange = !root.panadapter.autoRange
        }

        MenuSeparator {}

        // Da che parte stanno i comandi non è una preferenza estetica: chi
        // usa il mouse con la sinistra, o chi tiene il quaderno del log a
        // destra, passa la serata ad attraversare lo schermo.
        MenuItem {
            text: qsTr("Pannelli a sinistra")
            checkable: true
            checked: root.panelsOnLeft
            onTriggered: root.panelSideToggled()
        }
    }

    ThemedMenu {
        title: qsTr("Strumenti")

        MenuItem {
            // La registrazione esiste solo se la sorgente la consente: §7,
            // le capability comandano la UI anche qui dentro.
            text: Session.recorder.recording
                  ? qsTr("Ferma la registrazione")
                  : qsTr("Registra il flusso IQ")
            enabled: Session.connected && Session.capabilities.supportsRecording
            onTriggered: Session.toggleRecording()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Laboratorio registrazioni IQ…")
            onTriggered: root.iqAnalysisRequested()
        }

        MenuItem {
            text: qsTr("Ricezione ADS-B sperimentale…")
            onTriggered: root.adsbRequested()
        }

        MenuItem {
            text: qsTr("Ricezione APRS sperimentale…")
            onTriggered: root.aprsRequested()
        }

        MenuItem {
            text: qsTr("Ricezione AIS sperimentale…")
            onTriggered: root.aisRequested()
        }

        MenuItem {
            text: qsTr("Ricezione POCSAG sperimentale…")
            onTriggered: root.pocsagRequested()
        }

        MenuItem {
            text: qsTr("Ricezione ACARS sperimentale…")
            onTriggered: root.acarsRequested()
        }

        MenuItem {
            objectName: "vdl2MenuItem"
            enabled: root.vdl2Available
            text: root.vdl2Available ? qsTr("Ricezione VDL2 sperimentale…") : qsTr("VDL2 non incluso in questa build")
            onTriggered: root.vdl2Requested()
        }

        MenuItem {
            text: qsTr("Ricezione VOR sperimentale…")
            onTriggered: root.vorRequested()
        }

        MenuItem {
            text: qsTr("Ricezione SELCAL sperimentale…")
            onTriggered: root.selcalRequested()
        }

        MenuItem {
            text: qsTr("Ricezione DSC sperimentale…")
            onTriggered: root.dscRequested()
        }

        MenuItem {
            text: qsTr("Ricezione NAVTEX sperimentale…")
            onTriggered: root.navtexRequested()
        }

        MenuItem {
            text: qsTr("Scheduler e moduli IQ…")
            onTriggered: root.operationsRequested()
        }

        MenuSeparator {}

        ThemedMenu {
            id: languageMenu
            title: qsTr("Lingua")

            // Un Menu non prende i figli da un Repeater: le voci vanno
            // inserite nel suo contenuto, e l'Instantiator è il modo previsto
            // per farlo da un modello.
            //
            // L'elenco è filtrato su ciò che ha davvero una traduzione
            // compilata: una lingua che non cambia nulla è una promessa non
            // mantenuta.
            Instantiator {
                model: Session.language.availableLanguages.filter(l => l.available)

                delegate: MenuItem {
                    required property var modelData

                    text: modelData.name
                    checkable: true
                    checked: Session.language.currentLanguage === modelData.code
                    onTriggered: Session.language.setLanguage(modelData.code)
                }

                onObjectAdded: (index, object) => languageMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => languageMenu.removeItem(object)
            }
        }
    }

    ThemedMenu {
        title: qsTr("Aiuto")

        MenuItem {
            text: qsTr("Informazioni su DECODIUM SDR")
            onTriggered: root.aboutRequested()
        }
    }
}
