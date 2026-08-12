// SPDX-License-Identifier: GPL-3.0-or-later
// Picco e valore efficace sulla stessa barra, con la tenuta del picco.
//
// Sono due misure diverse e servono a due cose diverse: il valore efficace
// dice quanto è forte davvero il segnale, il picco dice quanto manca al fondo
// scala. La distanza fra i due è il fattore di cresta — dodici decibel su una
// voce, tre su una portante — ed è il numero che dice se il compressore della
// radio sta esagerando. Su una barra sola si legge senza fare sottrazioni.
//
// La tacca del picco resta ferma un attimo e poi scende piano: un picco che
// dura un fotogramma non lo si vede, ed è proprio quello che si stava
// cercando.
import QtQuick
import DecodiumSdr

Item {
    id: root

    /// Gli estremi della scala, in dBFS. Sotto i sessanta sotto il fondo scala
    /// non c'è più niente da vedere: è il rumore del convertitore.
    property real floorDb: -60
    property real ceilingDb: 0

    property real peakDb: Session.audioPeakDb
    property real rmsDb: Session.audioRmsDb

    /// Con quanti decibel al secondo scende la tacca del picco.
    property real decayDbPerSecond: 20

    implicitHeight: 14

    function fraction(db) {
        const span = Math.max(1, ceilingDb - floorDb)
        return Math.max(0, Math.min(1, (db - floorDb) / span))
    }

    /// Il picco tenuto: scende da solo, e risale di scatto quando arriva un
    /// picco più alto.
    property real heldDb: -140

    Timer {
        interval: 50
        running: root.visible && Session.connected
        repeat: true
        onTriggered: {
            if (root.peakDb >= root.heldDb)
                root.heldDb = root.peakDb
            else
                root.heldDb -= root.decayDbPerSecond * 0.05
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 2
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
    }

    // Il valore efficace: la barra piena.
    Rectangle {
        x: 1
        y: 1
        width: Math.max(0, (root.width - 2) * root.fraction(root.rmsDb))
        height: root.height - 2
        radius: 1
        // Verde fin quasi in cima, ambra negli ultimi sei decibel, rosso oltre
        // il fondo scala: sono le soglie di qualunque strumento di registrazione,
        // e chi le conosce non deve impararne di nuove.
        color: root.peakDb > -0.5 ? Theme.danger
             : root.peakDb > -6 ? Theme.spectrumPeak
             : Theme.success
        opacity: 0.75

        Behavior on width {
            NumberAnimation { duration: 60 }
        }
    }

    // La tacca del picco tenuto.
    Rectangle {
        x: Math.max(1, Math.min(root.width - 3, (root.width - 2) * root.fraction(root.heldDb)))
        y: 1
        width: 2
        height: root.height - 2
        color: root.heldDb > -0.5 ? Theme.danger : Theme.textPrimary
        visible: root.heldDb > root.floorDb
    }

    // Le tacche della scala: −40, −20, −6, 0. Le ultime due sono quelle che
    // contano, ed è per questo che sono più vicine fra loro.
    Repeater {
        model: [-40, -20, -6]

        delegate: Rectangle {
            required property var modelData

            x: Math.round((root.width - 2) * root.fraction(modelData)) + 1
            y: 1
            width: 1
            height: root.height - 2
            color: Theme.border
            opacity: 0.8
        }
    }
}
