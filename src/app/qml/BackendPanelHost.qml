// SPDX-License-Identifier: GPL-3.0-or-later
// Ospita i pannelli specifici del backend attivo.
//
// I nomi dei componenti arrivano da `capabilities().nativePanels`: il core non
// sa quali pannelli esistano e la UI generale non sa quale backend sia
// collegato. È così che un backend nuovo porta con sé i propri controlli senza
// che questo file cambi (CONSTITUTION §7).
//
// I pannelli caricati qui sono l'unico posto in cui è lecito chiamare
// `Session.nativeCommand()` (§4.1).
import QtQuick
import QtQuick.Layouts
import DecodiumSdr

Column {
    id: root

    spacing: Theme.spacing
    // Serve un device aperto, non solo un backend scelto.
    //
    // Le capability — e con esse l'elenco dei pannelli nativi — le dichiara il
    // backend appena viene selezionato, prima ancora che si connetta qualcosa.
    // Da quando il ColibriNANO è il backend di partenza, questo significava
    // mostrare all'avvio il suo pannello di guadagno: comandi con l'aria di
    // funzionare e nessun ricevitore su cui agire.
    visible: repeater.count > 0 && Session.connected

    Repeater {
        id: repeater
        model: Session.capabilities.nativePanels

        delegate: Loader {
            required property string modelData

            width: root.width
            asynchronous: true

            // Il nome è un identificatore di componente, non un percorso
            // scelto dall'utente: resta dentro il modulo.
            source: "qrc:/qt/qml/DecodiumSdr/" + modelData + ".qml"

            onStatusChanged: {
                if (status === Loader.Error) {
                    // Un backend che dichiara un pannello inesistente è un
                    // difetto del backend, non della UI: si segnala e si
                    // prosegue, invece di lasciare un buco silenzioso.
                    console.warn("pannello backend non trovato:", modelData)
                }
            }
        }
    }
}
