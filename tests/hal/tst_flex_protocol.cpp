// SPDX-License-Identifier: GPL-3.0-or-later
// SmartSDR: le righe del canale di comando.
//
// Un protocollo di testo sembra impossibile da sbagliare, e ha invece due
// trappole precise: il codice d'errore è esadecimale, e le coppie
// `chiave=valore` arrivano in ordine libero. Chi legge il primo in decimale
// vede riuscire i comandi falliti; chi legge le seconde per posizione
// funziona finché il firmware non ne aggiunge una in mezzo.

#include "hal/backends/flex/FlexProtocol.h"

#include <QTest>

using namespace dsdr::hal::flex;

class TestFlexProtocol : public QObject
{
    Q_OBJECT

private slots:
    void theRadioIntroducesItself();
    void aResponseCarriesItsSequence();
    void theErrorCodeIsHexadecimal();
    void aStatusBelongsToAHandle();
    void fieldsAreReadByNameNotByPosition();
    void aCommandCarriesItsSequence();
    void whatIsNotALineIsNotMistakenForOne();
    void theRadioIsDescribedByWhatTheOperatorWrote();
};

void TestFlexProtocol::theRadioIntroducesItself()
{
    const Line version = parseLine(QStringLiteral("V1.4.0.0"));
    QCOMPARE(version.kind, LineKind::Version);
    QCOMPARE(version.payload, QStringLiteral("1.4.0.0"));

    const Line handle = parseLine(QStringLiteral("H1A2B3C4D"));
    QCOMPARE(handle.kind, LineKind::Handle);
    QCOMPARE(handle.handle, QStringLiteral("1A2B3C4D"));
}

void TestFlexProtocol::aResponseCarriesItsSequence()
{
    // Su un canale dove le risposte si mescolano agli aggiornamenti di stato,
    // il numero d'ordine è l'unico modo di sapere a quale domanda si sta
    // rispondendo.
    const Line line = parseLine(QStringLiteral("R42|0|slice 3"));
    QCOMPARE(line.kind, LineKind::Response);
    QCOMPARE(line.sequence, quint32(42));
    QCOMPARE(line.code, quint32(0));
    QCOMPARE(line.payload, QStringLiteral("slice 3"));
    QVERIFY(!line.isError());
}

void TestFlexProtocol::theErrorCodeIsHexadecimal()
{
    // `50000015` in decimale è un numero plausibile, e un comando fallito
    // sembrerebbe riuscito. In esadecimale è quello che è: un errore.
    const Line line = parseLine(QStringLiteral("R7|50000015|"));
    QCOMPARE(line.sequence, quint32(7));
    QCOMPARE(line.code, quint32(0x50000015));
    QVERIFY(line.isError());
}

void TestFlexProtocol::aStatusBelongsToAHandle()
{
    const Line line = parseLine(
        QStringLiteral("S1A2B3C4D|slice 0 RF_frequency=14.074000 mode=USB"));
    QCOMPARE(line.kind, LineKind::Status);
    QCOMPARE(line.handle, QStringLiteral("1A2B3C4D"));
    QVERIFY(line.payload.startsWith(QStringLiteral("slice 0")));
}

void TestFlexProtocol::fieldsAreReadByNameNotByPosition()
{
    // L'ordine non è garantito, e il firmware ne aggiunge di nuovi. Leggerle
    // per posizione funzionerebbe fino al primo aggiornamento della radio.
    const auto fields = parseFields(
        QStringLiteral("mode=USB RF_frequency=14.074000 rxant=ANT1 wide=0"));

    QCOMPARE(fields.value(QStringLiteral("RF_frequency")), QStringLiteral("14.074000"));
    QCOMPARE(fields.value(QStringLiteral("mode")), QStringLiteral("USB"));
    QCOMPARE(fields.value(QStringLiteral("rxant")), QStringLiteral("ANT1"));

    // Quello che non c'è non deve diventare una stringa vuota travestita da
    // valore: chi chiede una chiave assente deve ricevere niente.
    QVERIFY(fields.value(QStringLiteral("inesistente")).isEmpty());

    // Un pezzo senza uguale non è un campo, e non deve entrare.
    const auto messy = parseFields(QStringLiteral("slice 0 mode=CW"));
    QCOMPARE(messy.size(), 1);
    QCOMPARE(messy.value(QStringLiteral("mode")), QStringLiteral("CW"));
}

void TestFlexProtocol::aCommandCarriesItsSequence()
{
    QCOMPARE(buildCommand(3, QStringLiteral("sub slice all")),
             QStringLiteral("C3|sub slice all\n"));
    // Il ritorno a capo non è decorazione: senza, la radio aspetta il resto
    // del comando e non risponde mai.
    QVERIFY(buildCommand(1, QStringLiteral("ping")).endsWith(QLatin1Char('\n')));
}

void TestFlexProtocol::whatIsNotALineIsNotMistakenForOne()
{
    QCOMPARE(parseLine(QString()).kind, LineKind::Unknown);
    QCOMPARE(parseLine(QStringLiteral("   ")).kind, LineKind::Unknown);
    QCOMPARE(parseLine(QStringLiteral("qualcosa d'altro")).kind, LineKind::Unknown);
    // Il tag giusto ma senza i campi: una risposta a metà non è una risposta,
    // e prenderla per buona darebbe un codice d'errore zero — cioè «fatto».
    QCOMPARE(parseLine(QStringLiteral("R42")).kind, LineKind::Unknown);
}

void TestFlexProtocol::theRadioIsDescribedByWhatTheOperatorWrote()
{
    QHash<QString, QString> fields;
    fields.insert(QStringLiteral("model"), QStringLiteral("FLEX-6400"));
    fields.insert(QStringLiteral("nickname"), QStringLiteral("Shack"));
    fields.insert(QStringLiteral("callsign"), QStringLiteral("IU8LMC"));

    // In una stazione con due Flex, soprannome e nominativo sono l'unico modo
    // di sapere quale delle due si sta guardando.
    QCOMPARE(describeRadio(fields),
             QStringLiteral("FLEX-6400 · Shack · IU8LMC"));

    // Un soprannome uguale al modello non si ripete.
    fields.insert(QStringLiteral("nickname"), QStringLiteral("FLEX-6400"));
    QCOMPARE(describeRadio(fields), QStringLiteral("FLEX-6400 · IU8LMC"));

    QVERIFY(describeRadio(QHash<QString, QString>()).isEmpty());
}

QTEST_MAIN(TestFlexProtocol)
#include "tst_flex_protocol.moc"
