// SPDX-License-Identifier: GPL-3.0-or-later
// SmartSDR: le righe del canale di comando.
//
// Un protocollo di testo sembra impossibile da sbagliare, e ha invece due
// trappole precise: il codice d'errore è esadecimale, e le coppie
// `chiave=valore` arrivano in ordine libero. Chi legge il primo in decimale
// vede riuscire i comandi falliti; chi legge le seconde per posizione
// funziona finché il firmware non ne aggiunge una in mezzo.

#include "hal/backends/flex/FlexProtocol.h"
#include "hal/backends/flex/FlexVita.h"

#include <QtEndian>

#include <cstring>

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

    // ── VITA-49 ─────────────────────────────────────────────────────────
    void theHeaderTellsWhereTheDataBegins();
    void aPacketFromSomeoneElseIsNotOurs();
    void theClassCodeDeclaresTheFormat();
    void samplesComeOutInNetworkOrder();
    void aFormatWeCannotReadIsSkipped();
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

namespace {

/// Un pacchetto VITA-49 come lo manda un FlexRadio: intestazione, flusso,
/// classe, marcatori temporali, e coppie di float in ordine di rete.
QByteArray vitaPacket(quint16 packetClass, const QVector<float> &samples,
                      bool timestamps = true, quint32 oui = 0x001C2D)
{
    const int words = 4 + (timestamps ? 3 : 0) + samples.size();

    QByteArray packet(words * 4, '\0');
    auto *p = reinterpret_cast<uchar *>(packet.data());

    quint32 header = 0x10000000        // dati con identificativo di flusso
                   | 0x08000000        // classe presente
                   | quint32(words);   // misura totale, in parole
    if (timestamps)
        header |= 0x00400000 | 0x00100000;   // TSI UTC, TSF conteggio campioni

    qToBigEndian<quint32>(header, p);
    qToBigEndian<quint32>(0x40000001u, p + 4);            // identificativo di flusso
    qToBigEndian<quint32>(oui, p + 8);                    // OUI
    qToBigEndian<quint32>((0x534Cu << 16) | packetClass, p + 12);

    const int payload = (4 + (timestamps ? 3 : 0)) * 4;
    for (int i = 0; i < samples.size(); ++i) {
        quint32 bits = 0;
        const float value = samples.at(i);
        std::memcpy(&bits, &value, sizeof(bits));
        qToBigEndian<quint32>(bits, p + payload + i * 4);
    }
    return packet;
}

/// Il codice di classe di un flusso IQ: 32 bit per campione, due canali,
/// virgola mobile IEEE-754.
constexpr quint16 kIqClass = (0x3 << 5) | (0x3 << 7) | (0x1 << 9);

} // namespace

void TestFlexProtocol::theHeaderTellsWhereTheDataBegins()
{
    // Con i marcatori temporali l'intestazione è di sette parole, senza di
    // cinque. Darla per fissa funziona finché una radio non ne manda uno
    // senza — e allora tutti i campioni scivolano di due parole, restando
    // plausibili.
    const auto withTs = parseVita(vitaPacket(kIqClass, {1.0f, 0.0f}, true));
    QVERIFY(withTs.valid);
    QCOMPARE(withTs.payloadOffset, 28);
    QCOMPARE(withTs.payloadBytes, 8);

    const auto withoutTs = parseVita(vitaPacket(kIqClass, {1.0f, 0.0f}, false));
    QVERIFY(withoutTs.valid);
    QCOMPARE(withoutTs.payloadOffset, 16);
    QCOMPARE(withoutTs.payloadBytes, 8);
}

void TestFlexProtocol::aPacketFromSomeoneElseIsNotOurs()
{
    // Sulla stessa porta può arrivare di tutto. L'OUI del costruttore è il
    // filtro: senza, un pacchetto VITA di un altro apparato diventerebbe
    // rumore spacciato per banda.
    QVERIFY(!parseVita(vitaPacket(kIqClass, {1.0f, 0.0f}, true, 0x00ABCDEF)).valid);
    QVERIFY(!parseVita(QByteArray()).valid);
    QVERIFY(!parseVita(QByteArray(12, '\0')).valid);
}

void TestFlexProtocol::theClassCodeDeclaresTheFormat()
{
    // Non è un identificativo opaco da confrontare con una tabella: descrive
    // il carico. È quello che permette di verificare invece di indovinare.
    const auto iq = parseVita(vitaPacket(kIqClass, {1.0f, 0.0f}));
    QVERIFY(iq.valid);
    QVERIFY(iq.isFloatPair());

    // Interi invece di virgola mobile: stesso numero di bit, formato diverso.
    const quint16 fixedPoint = (0x3 << 5) | (0x3 << 7);
    QVERIFY(!parseVita(vitaPacket(fixedPoint, {1.0f, 0.0f})).isFloatPair());

    // Un canale solo: è audio, non IQ.
    const quint16 mono = (0x3 << 5) | (0x1 << 9);
    QVERIFY(!parseVita(vitaPacket(mono, {1.0f, 0.0f})).isFloatPair());
}

void TestFlexProtocol::samplesComeOutInNetworkOrder()
{
    const QVector<float> samples = {1.0f, -1.0f, 0.25f, -0.5f};
    const QByteArray packet = vitaPacket(kIqClass, samples);
    const auto parsed = parseVita(packet);

    std::vector<float> out(samples.size(), 0.0f);
    QCOMPARE(decodeIq(packet, parsed, out.data()), std::size_t(2));

    // Letti com'è in memoria, questi valori uscirebbero come numeri enormi o
    // denormali: l'ordine di rete non è un dettaglio.
    for (int i = 0; i < samples.size(); ++i)
        QVERIFY(std::abs(out[i] - samples.at(i)) < 1e-6f);
}

void TestFlexProtocol::aFormatWeCannotReadIsSkipped()
{
    // Un formato che non sappiamo leggere si salta. Interpretarlo lo stesso
    // darebbe campioni plausibili e sbagliati — che è il modo peggiore di
    // essere rotti, perché sembra funzionare.
    const quint16 fixedPoint = (0x3 << 5) | (0x3 << 7);
    const QByteArray packet = vitaPacket(fixedPoint, {1.0f, 0.0f});
    std::vector<float> out(2, 0.0f);
    QCOMPARE(decodeIq(packet, parseVita(packet), out.data()), std::size_t(0));
}

QTEST_MAIN(TestFlexProtocol)
#include "tst_flex_protocol.moc"
