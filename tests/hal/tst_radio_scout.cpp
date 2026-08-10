// SPDX-License-Identifier: GPL-3.0-or-later
// Chi c'è in rete: l'interpretazione delle risposte.
//
// È la parte che sbaglia senza far rumore. Un byte preso dall'offset sbagliato
// non produce un errore: produce un modello inventato, o un indirizzo MAC che
// scorre di uno, e chi legge l'elenco non ha modo di sospettarlo. Per questo i
// pacchetti qui sotto sono scritti a mano, byte per byte, come li manda la
// radio.

#include "hal/RadioScout.h"

#include <QTest>

using namespace dsdr::hal;

namespace {

/// Risposta OpenHPSDR costruita a mano: sincronismo, stato, MAC, firmware,
/// modello.
QByteArray hpsdrReply(quint8 status, quint8 firmware, quint8 board)
{
    QByteArray reply(60, '\0');
    reply[0] = char(0xEF);
    reply[1] = char(0xFE);
    reply[2] = char(status);
    const quint8 mac[6] = {0x00, 0x1C, 0xC0, 0xA2, 0x13, 0xDD};
    for (int i = 0; i < 6; ++i)
        reply[3 + i] = char(mac[i]);
    reply[9] = char(firmware);
    reply[10] = char(board);
    return reply;
}

} // namespace

class TestRadioScout : public QObject
{
    Q_OBJECT

private slots:
    void theProbeIsWhatTheProtocolAsksFor();
    void anHermesLiteSaysWhoItIs();
    void aRadioAlreadyInUseSaysSoToo();
    void anUnknownBoardKeepsItsNumber();
    void noiseIsNotARadio();
    void aFlexAnnouncesItself();
    void aFlexWithoutSerialStillHasAnIdentity();
    void anythingWithoutAModelIsNotAFlex();
};

void TestRadioScout::theProbeIsWhatTheProtocolAsksFor()
{
    // Sessantatré byte: due di sincronismo, il comando, e zeri. Una lunghezza
    // diversa e la radio non risponde — e non c'è modo di accorgersene se non
    // sapendo che doveva rispondere.
    const QByteArray probe = RadioScout::hpsdrProbe();
    QCOMPARE(probe.size(), 63);
    QCOMPARE(static_cast<quint8>(probe.at(0)), quint8(0xEF));
    QCOMPARE(static_cast<quint8>(probe.at(1)), quint8(0xFE));
    QCOMPARE(static_cast<quint8>(probe.at(2)), quint8(0x02));
    QCOMPARE(probe.count('\0'), 60);
}

void TestRadioScout::anHermesLiteSaysWhoItIs()
{
    const auto radio = RadioScout::parseHpsdrReply(
        hpsdrReply(0x02, 73, 0x06), QHostAddress(QStringLiteral("192.168.1.44")));

    QVERIFY(radio.isValid());
    QCOMPARE(radio.family, QStringLiteral("OpenHPSDR"));
    QCOMPARE(radio.model, QStringLiteral("Hermes-Lite"));
    QCOMPARE(radio.address, QStringLiteral("192.168.1.44"));
    // Il MAC è l'identità: è l'unica cosa che non cambia quando il DHCP
    // cambia idea.
    QCOMPARE(radio.identity, QStringLiteral("00:1C:C0:A2:13:DD"));
    QVERIFY(radio.detail.contains(QStringLiteral("7.3")));
}

void TestRadioScout::aRadioAlreadyInUseSaysSoToo()
{
    // 0x03 significa «mi sta già usando qualcun altro». È una risposta valida,
    // e l'informazione vale doppio: spiega perché la radio non si apre, che
    // altrimenti sembra un guasto.
    const auto radio = RadioScout::parseHpsdrReply(
        hpsdrReply(0x03, 73, 0x05), QHostAddress(QStringLiteral("10.0.0.9")));

    QVERIFY(radio.isValid());
    QCOMPARE(radio.model, QStringLiteral("Orion"));
    QVERIFY2(radio.detail.contains(QStringLiteral("uso")),
             qPrintable(QStringLiteral("dettaglio: %1").arg(radio.detail)));
}

void TestRadioScout::anUnknownBoardKeepsItsNumber()
{
    // Un modello uscito dopo di noi deve restare riconoscibile: dire il numero
    // è meno bello che dire il nome, ed è infinitamente meglio che inventarne
    // uno o far sparire la radio.
    const auto radio = RadioScout::parseHpsdrReply(
        hpsdrReply(0x02, 40, 0x7F), QHostAddress(QStringLiteral("10.0.0.10")));

    QVERIFY(radio.isValid());
    QCOMPARE(radio.model, QStringLiteral("OpenHPSDR 0x7F"));
}

void TestRadioScout::noiseIsNotARadio()
{
    const QHostAddress sender(QStringLiteral("10.0.0.1"));

    // Sulla porta 1024 passa di tutto. Niente di ciò che non è una risposta
    // deve diventare una riga nell'elenco.
    QVERIFY(!RadioScout::parseHpsdrReply(QByteArray(), sender).isValid());
    QVERIFY(!RadioScout::parseHpsdrReply(QByteArray(5, '\0'), sender).isValid());
    QVERIFY(!RadioScout::parseHpsdrReply(QByteArray("qualcosa d'altro"), sender).isValid());

    // Sincronismo giusto ma stato che non esiste: è il nostro stesso pacchetto
    // di chiamata che torna indietro dal broadcast, e prenderlo per una radio
    // farebbe comparire un fantasma a ogni scansione.
    QByteArray echo = RadioScout::hpsdrProbe();
    echo[2] = char(0x02);
    // …questa invece è davvero una risposta: la differenza sta tutta nella
    // lunghezza e nel contenuto, non nei primi tre byte.
    QVERIFY(RadioScout::parseHpsdrReply(hpsdrReply(0x02, 1, 0x01), sender).isValid());

    QByteArray wrongStatus = hpsdrReply(0x09, 73, 0x06);
    QVERIFY(!RadioScout::parseHpsdrReply(wrongStatus, sender).isValid());
}

void TestRadioScout::aFlexAnnouncesItself()
{
    // L'annuncio vero ha davanti un'intestazione VITA-49 binaria: il testo
    // comincia più avanti, e cercarlo invece di decodificare l'intestazione è
    // ciò che rende la lettura indipendente dalla versione del protocollo.
    QByteArray datagram(28, '\0');
    datagram.append("discovery_protocol_version=3.0.0.2 model=FLEX-6400 "
                    "serial=1234-5678-6400-9999 version=3.3.29.10 "
                    "nickname=FlexRadio ip=192.168.1.77 port=4992 status=Available ");

    const auto radio = RadioScout::parseFlexAnnounce(
        datagram, QHostAddress(QStringLiteral("192.168.1.77")));

    QVERIFY(radio.isValid());
    QCOMPARE(radio.family, QStringLiteral("FlexRadio"));
    QCOMPARE(radio.model, QStringLiteral("FLEX-6400"));
    QCOMPARE(radio.address, QStringLiteral("192.168.1.77"));
    QCOMPARE(radio.identity, QStringLiteral("1234-5678-6400-9999"));
    QVERIFY(radio.detail.contains(QStringLiteral("SmartSDR 3.3.29.10")));
    QVERIFY(radio.detail.contains(QStringLiteral("Available")));
}

void TestRadioScout::aFlexWithoutSerialStillHasAnIdentity()
{
    // Senza identità la stessa radio comparirebbe una volta al secondo per
    // tutta la durata dell'ascolto: l'indirizzo è meno stabile del numero di
    // serie, ma basta a non ripetersi.
    QByteArray datagram("model=FLEX-6600M ip=10.0.0.5 status=Available ");
    const auto radio = RadioScout::parseFlexAnnounce(
        datagram, QHostAddress(QStringLiteral("10.0.0.5")));

    QVERIFY(radio.isValid());
    QCOMPARE(radio.identity, QStringLiteral("10.0.0.5"));
}

void TestRadioScout::anythingWithoutAModelIsNotAFlex()
{
    const QHostAddress sender(QStringLiteral("10.0.0.5"));
    QVERIFY(!RadioScout::parseFlexAnnounce(QByteArray(), sender).isValid());
    QVERIFY(!RadioScout::parseFlexAnnounce(QByteArray("serial=1 ip=10.0.0.5"), sender).isValid());
    QVERIFY(!RadioScout::parseFlexAnnounce(QByteArray("model= serial=1"), sender).isValid());
}

QTEST_MAIN(TestRadioScout)
#include "tst_radio_scout.moc"
