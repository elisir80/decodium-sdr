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

    /// Il panadattatore, per i comandi di vista.
    required property PanadapterView panadapter

    /// Aperture richieste alla finestra: il menu non sa dove vivano quei
    /// pannelli, si limita a chiederlo.
    signal sourceRequested()
    signal aboutRequested()

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
            onTriggered: Qt.quit()
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
