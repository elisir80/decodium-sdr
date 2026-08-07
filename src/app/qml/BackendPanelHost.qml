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
    visible: repeater.count > 0

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
