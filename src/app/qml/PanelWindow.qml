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

    /// Se il pannello ha qualcosa da mostrare adesso. La finestra resta —
    /// l'ha aperta qualcuno, e chiuderla da soli sarebbe una sorpresa — ma
    /// dentro non si tiene vivo un pannello che non ha dati.
    property bool contentRelevant: true

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

    // Sopra la finestra principale, sempre.
    //
    // È il punto su cui questa finestra è stata sbagliata due volte. Restando
    // figlia — `transientParent` alla finestra che la contiene — il sistema la
    // tiene sopra il genitore, che è quello che serve: la principale sta a
    // tutto schermo, e una finestra che le finisce dietro è indistinguibile da
    // una finestra che non si è aperta. Staccandola invece del tutto si
    // guadagna il pulsante nella barra delle applicazioni e si perde proprio
    // quella garanzia: nasce davanti e sparisce al primo clic sulla
    // principale.
    //
    // Si tiene la parentela, e il modo di ritrovarla è un altro: l'icona del
    // pannello, in colonna, la riporta davanti.
    flags: Qt.Window

    /// Porta la finestra davanti e dentro lo schermo.
    ///
    /// La seconda metà non è pignoleria: una finestra ricordata su un monitor
    /// che adesso non c'è più si riapre a coordinate che non esistono, e
    /// alzarla non la fa comparire. Succede ogni volta che si stacca un
    /// portatile dalla scrivania.
    function bringForward() {
        const view = Qt.application.screens.length > 0 ? screen : null
        if (view) {
            const margin = 40
            x = Math.max(view.virtualX,
                         Math.min(view.virtualX + view.width - margin, x))
            y = Math.max(view.virtualY,
                         Math.min(view.virtualY + view.height - margin, y))
        }
        show()
        raise()
        requestActivate()
    }

    // Nasce davanti. Chi ha appena premuto «stacca» si aspetta di vedere la
    // finestra, non di doverla cercare.
    //
    // Il richiamo sta su `visible` e non su `Component.onCompleted`: alzare
    // una finestra che il sistema non ha ancora mostrato non fa niente, e il
    // momento in cui viene mostrata è più tardi di quello in cui l'oggetto è
    // pronto.
    onVisibleChanged: {
        if (visible)
            bringForward()
    }

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

    // Quando non c'è niente da mostrare si dice, invece di lasciare una
    // finestra vuota che sembra rotta.
    Text {
        anchors.centerIn: parent
        visible: !root.contentRelevant
        text: qsTr("Nessun ricevitore collegato.")
        font.pixelSize: Theme.fontSmall
        color: Theme.textDisabled
    }

    // Il pannello riempie la finestra e cresce con lei: è tutto il senso di
    // averlo staccato. `PanelFrame` di suo si dimensiona sul contenuto —
    // giusto in colonna, sbagliato qui, dove il contenuto deve dimensionarsi
    // sulla finestra.
    Loader {
        id: loader

        anchors.fill: parent
        anchors.margins: Theme.spacing
        active: root.contentRelevant
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
