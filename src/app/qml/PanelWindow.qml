// SPDX-License-Identifier: GPL-3.0-or-later
// Un pannello staccato dalla colonna, in una finestra sua.
//
// La colonna è larga trecento punti, e ci sono pannelli a cui non bastano:
// lo studio dell'audio in una striscia stretta mostra uno spettro alto un
// dito, dove una banda di tre kilohertz occupa venti pixel e la forma d'onda
// è una linea. In una finestra propria si ingrandisce quanto serve, e si può
// mettere sul secondo schermo — che è dove finisce, sempre, lo strumento che
// si guarda mentre si fa altro.
//
// La finestra non contiene una copia del pannello: contiene *il* pannello. Il
// componente è lo stesso che la colonna avrebbe istanziato, e i suoi comandi
// agiscono sulla stessa sessione. Non c'è uno stato da tenere allineato fra
// due copie, perché non ci sono due copie.
import QtCore
import QtQuick
import QtQuick.Controls.Basic
import DecodiumSdr

Window {
    id: root

    /// Il componente da mostrare, e la chiave con cui se ne ricorda la
    /// geometria: due pannelli staccati non devono aprirsi uno sopra l'altro.
    required property Component panelComponent
    required property string panelKey
    required property string panelTitle

    /// Chiudendo la finestra il pannello torna in colonna: non sparisce.
    /// Sparire sarebbe il comportamento di una finestra di documento, e questo
    /// non è un documento — è uno strumento che stava da un'altra parte.
    signal reattachRequested()

    title: qsTr("DECODIUM SDR — %1").arg(panelTitle)
    color: Theme.background
    // Una misura di partenza in cui lo studio audio ha senso: lo spettro alto
    // abbastanza da leggerlo e la forma d'onda larga abbastanza da vederla.
    width: 720
    height: 560
    minimumWidth: 360
    minimumHeight: 260
    visible: true

    // La geometria si ricorda per pannello. Chi mette lo studio audio sul
    // secondo schermo lo ritrova lì, e non al centro del primo.
    Settings {
        category: "detached/" + root.panelKey
        property alias x: root.x
        property alias y: root.y
        property alias width: root.width
        property alias height: root.height
    }

    onClosing: root.reattachRequested()

    // Il pannello riempie la finestra e cresce con lei: è tutto il senso di
    // averlo staccato. `PanelFrame` di suo si dimensiona sul contenuto —
    // giusto in colonna, sbagliato qui, dove il contenuto deve dimensionarsi
    // sulla finestra.
    Loader {
        id: loader

        anchors.fill: parent
        anchors.margins: Theme.spacing
        sourceComponent: root.panelComponent

        onLoaded: {
            // Staccato non si chiude e non si trascina: la maniglia serviva a
            // ordinarlo in colonna, e il chevron a fargli spazio. Qui lo spazio
            // ce l'ha tutto.
            if (item.hasOwnProperty("collapsible"))
                item.collapsible = false
            if (item.hasOwnProperty("collapsed"))
                item.collapsed = false
            if (item.hasOwnProperty("draggable"))
                item.draggable = false
            if (item.hasOwnProperty("detached"))
                item.detached = true
        }
    }
}
