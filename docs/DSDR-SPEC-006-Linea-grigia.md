# DSDR-SPEC-006 — La linea grigia e il puntamento

> Stato: **costruita.**

## 1. A che cosa serve

Sulle bande basse le aperture buone non durano tutto il giorno: durano i minuti
attorno all'alba e al tramonto, quando il percorso fra due stazioni corre lungo
la fascia in cui il Sole sta appena sotto l'orizzonte. Lì la banda D — quella
che di giorno assorbe gli 80 e i 160 metri — si è già dissolta, mentre la F è
ancora ionizzata.

Sapere dove passa quella fascia **adesso** è la differenza fra chiamare e
chiamare al momento giusto.

## 2. Quello che calcola, e quello che no

C'è la geometria: posizione del Sole, terminatore, distanza angolare, rotta
ortodromica, locatori Maidenhead.

**Non c'è nessuna previsione di propagazione.** Quella dipende dal flusso
solare, dall'indice K, dall'antenna e dalla potenza, e un numero inventato su
quelle basi sarebbe peggio di nessun numero.

## 3. Le decisioni

### 3.1 Nessuna dipendenza da Qt Positioning

Il modulo di partenza usava `QGeoCoordinate`, che trascina `Qt6::Positioning`
per portare due `double` e una distanza sferica. La distanza sferica sono
quattro righe di trigonometria. Il motore si compila e si prova con Qt Core e
basta.

Di conseguenza niente `QtLocation`, niente MapLibre, niente tile, niente rete:
**un client SDR non deve telefonare a un server di mappe per dire dov'è il
Sole**, e su una stazione senza rete la mappa deve funzionare uguale.

### 3.2 Equirettangolare, non Mercatore

Mercatore serve a una cosa: mantenere gli angoli, così una rotta costante è una
retta. Qui non si naviga. In cambio Mercatore gonfia le alte latitudini fino a
rendere illeggibile la calotta polare, che sulle bande alte è la zona che
conta.

In equirettangolare **la longitudine è il tempo**: il terminatore è una
sinusoide che si legge come un orologio.

### 3.3 Le bande sono dischi, non anelli

Un punto in cui il Sole sta a −h di altitudine dista (90 − h) gradi dal punto
**antisolare**. Le quattro fasce di crepuscolo sono quindi dischi concentrici
attorno a quel punto — 90°, 84°, 78°, 72° — e non anelli da ritagliare.
Disegnandoli dal più grande al più piccolo con la stessa trasparenza si
sommano, e la sfumatura viene da sé senza gradienti.

Ritagliarli come anelli darebbe una sfumatura al contrario, e nessuno se ne
accorgerebbe se non confrontando due mappe.

### 3.4 Il fondo è il giorno

Le bande **scuriscono**. Al primo tentativo il fondo era già quasi nero e la
mappa usciva tutta uguale: il terminatore c'era, ed era una riga senza
significato. Il fondo deve essere il giorno perché ci sia qualcosa da scurire.

### 3.5 Il quadrante del rotore

L'azimut è un angolo, e un numero da solo — «265 gradi» — va convertito
mentalmente in una direzione ogni volta che lo si legge. Sul quadrante la
direzione **è** la posizione dell'ago.

**Gli assi cartesiani sono la parte che la maggior parte dei quadranti software
non ha**, e sono quelli che rendono leggibile un angolo a colpo d'occhio: con
la croce nord-sud / est-ovest si vede subito da che parte del nord si sta.
Senza, un ago in mezzo a un cerchio vuoto si legge come un orologio senza
lancette delle ore.

Le tacche stanno ogni dieci gradi, lunghe ogni trenta: è la spaziatura dei
controller meccanici, e non è arbitraria — dieci gradi è la precisione con cui
si riesce a fermare un rotore, e più fitto sarebbe una scala che promette
quello che la meccanica non mantiene.

**Due aghi.** La via breve e la via lunga sono la stessa direzione dai due
lati, e sono centottanta gradi di rotore. Quello inattivo resta disegnato e
spento: vederlo lì dice che esiste, e sulle bande alte — quando la via breve
passa sopra una calotta polare disturbata — la via lunga è spesso l'unica che
porta il segnale.

Il terzo indice, tratteggiato, mostra dove sta puntando **davvero** l'antenna.
Compare solo se un rotore lo sa dire: finché non c'è, non si disegna un ago che
finge di sapere. Sotto al quadrante c'è lo scarto — «161° da girare» — che è
l'unica cosa che si guarda mentre il rotore gira.

### 3.6 Il numero che conta

Non l'azimut e non la distanza — quelli li dà qualunque programma — ma **quanti
chilometri del percorso stanno adesso nella fascia grigia**.

Non si legge a occhio da una mappa: un percorso può attraversare il terminatore
di sbieco per migliaia di chilometri o tagliarlo di netto in duecento, e le due
cose sulla mappa si assomigliano.

### 3.7 La fascia è larga, e si regola

Resta un errore **fisico** che nessun calcolo toglie: il terminatore radio non
coincide con quello ottico. Gli strati ionizzati stanno a centinaia di
chilometri di quota e restano illuminati quando la superficie è già al buio.

Per questo la fascia ha una semilarghezza regolabile — sei gradi di partenza —
invece di essere una riga.

## 3.8 Il rotore

Via **`rotctld`**, il demone rotori di Hamlib, che parla con una quarantina di
modelli — Yaesu GS-232, Prosistel, SPID, Green Heron, M2 — e permette di
riusare quel lavoro senza linkare hamlib e senza copiarne una riga:

```
rotctld -m 603 -r COM7 -s 9600 -t 4533
```

È lo stesso mestiere che fa già `RigctldDriver` per il CAT, e con lo stesso
riconoscimento dei **due dialetti**: `rotctld` capisce il prefisso `+` e chiude
con `RPRT`, i server minimi rispondono i soli valori. La differenza è che qui il
silenzio *è* una risposta — un server minimo al `+` non dice niente — e la si
aspetta 1,2 secondi invece di quattro, altrimenti ogni collegamento con mezzo
ecosistema costerebbe quattro secondi di attesa muta.

### Un rotore è una massa su un palo

Girarlo non è come cambiare un filtro: c'è un motore, ci sono relè che chiudono,
e c'è un'antenna in cima a una torre. Da qui discendono tutte le scelte.

**Non insegue niente da solo.** Puntare è un comando esplicito. Un rotore che
parte perché qualcuno ha toccato una mappa è una sorpresa su un palo, e sui pali
le sorprese costano. C'è un test che lascia girare tutto e verifica che non
parta un solo comando di movimento.

**Non si martella.** Un comando di puntamento ogni secondo e mezzo, non di più:
ogni comando chiude dei relè, e trascinare un cursore ne manderebbe cento in tre
secondi. Il rifiuto si dice, non si tace.

**FERMA passa davanti a tutto.** Se qualcuno lo preme mentre un puntamento è in
coda, il puntamento non parte — sarebbe l'esatto contrario di quello che è stato
chiesto, e il rotore ripartirebbe subito dopo essersi fermato.

**Se il collegamento cade mentre gira, il rotore continua.** Non lo si può
fermare e non si finge di poterlo fare: lo si dice, perché chi opera possa
scendere in torre o staccare l'alimentazione.

**Un server che tace non blocca la coda.** Chi smette di rispondere non chiude
il socket: resta lì. Senza un guinzaglio l'indicatore resterebbe sul valore di
mezz'ora prima, sembrando giusto.

### 4532 non è 4533

Sono due porte vicine e due demoni che si somigliano, e su una stazione vera
capita di avere un server **rigctl** dove ci si aspettava rotctld. Chiedere una
posizione a una radio non dà una posizione: dà `RPRT -11`. Lì il controller si
stacca e lo dice, invece di restare collegato a un indicatore che non si
muoverà mai.

Verificato dal vivo su questa macchina, dove sulla 4533 c'è il rigctl di
DECODIUM 4.

### L'elevazione, se c'è

`hasElevation` è falso finché `dump_caps` non dichiara un intervallo di
elevazione non nullo. Non vuol dire «zero gradi»: vuol dire che non c'è, e un
indicatore d'elevazione fermo sullo zero sarebbe una bugia che nessuno può
smentire guardandola. Con il dialetto corto non si può chiedere, e non si
inventa.

## 4. Precisione, e come si controlla

La declinazione sta entro il centesimo di grado fra il 1950 e il 2050:
algoritmo NOAA in forma ridotta.

Una mappa di propagazione sbagliata non dà un errore: **dà una mappa**. Compare,
si muove, sembra plausibile, e manda a chiamare mezz'ora prima o dopo il
momento buono. Tutto quello che si può verificare contro un valore indipendente
è verificato in `tst_greyline`:

| Prova | Che cosa prende |
|---|---|
| solstizi a ±23,44° | segno invertito nell'obliquità |
| equinozi sull'equatore | fase dell'anno sbagliata |
| equazione del tempo a novembre | il termine che sposta il terminatore di 400 km |
| Sole a 0,000° sul terminatore | le due metà del calcolo che non si parlano |
| Roma–New York, gli antipodi | latitudine e longitudine invertite |
| Roma–Tokyo a nord-est | una rotta calcolata sulla mappa piatta |
| locatore andata e ritorno | mezzo quadrato di scarto, che non si vede |
| locatore non valido | zero-zero, che è nel golfo di Guinea |
| ortodromia spezzata | la riga che attraversa tutta la mappa |

E per il rotore, in `tst_rotor`, con un finto `rotctld` che parla i due dialetti:

| Prova | Che cosa prende |
|---|---|
| dialetto esteso e corto | mezzo ecosistema che ignora il `+` |
| non si muove da solo | un'antenna che parte all'avvio del programma |
| dieci puntamenti, uno parte | i relè di un controller consumati in tre secondi |
| FERMA cancella la coda | il rotore che riparte subito dopo essersi fermato |
| il server tace | una coda bloccata per sempre |
| il collegamento cade | un rotore che gira e nessuno lo dice |
| un rigctl sulla 4533 | una radio scambiata per un rotore |

## 5. I dati

Coste da **Natural Earth** `ne_110m_coastline`, di pubblico dominio,
semplificate a un centesimo di grado — circa un chilometro, un decimo di pixel
su una mappa da mille punti — e convertite in polilinee binarie: diciannove
kilobyte in `data/coste.bin`, rigenerabili con `data/coste.py`.

## 6. Dove guardare nel codice

```
src/core/Greyline.h/.cpp      il motore: Sole, rotte, locatori. Solo Qt Core
src/app/GreylineMap.h/.cpp    la mappa dipinta, con la basemap in cache
src/core/RotorController.*    il rotore via rotctld
src/app/qml/RotorDial.qml     il quadrante
src/app/qml/GreylinePanel.qml il pannello che li tiene insieme
tests/core/tst_greyline.cpp
tests/core/tst_rotor.cpp      con il suo finto rotctld
data/coste.py                 come si rigenerano le coste
```
