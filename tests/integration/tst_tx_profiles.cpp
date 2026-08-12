// SPDX-License-Identifier: GPL-3.0-or-later
// I profili della catena di trasmissione (SPEC-005 §4.4).
//
// Un profilo che cambia da sé è comodo finché non cancella mezz'ora di lavoro
// senza dirlo. Questi test presidiano le due cose che lo rendono una memoria
// invece di una tassa: che ogni modo trovi il profilo giusto, e che sui dati
// la catena sia spenta — perché comprimere davanti a un modulatore FT8 allarga
// il segnale e non aggiunge un decibel a chi decodifica, ed è l'errore più
// diffuso che ci sia.
#include "core/TxProfiles.h"

#include <QTest>

using namespace dsdr;
using namespace dsdr::core;

class TestTxProfiles : public QObject
{
    Q_OBJECT

private slots:
    void ogniModoTrovaIlSuoMestiere();
    void suiDatiLaCatenaEspenta();
    void ilContestNonEUnaChiacchierataPiuForte();
    void unProfiloSiRicordaEsiRimette();
    void laVoceDiFabbricaSiRecupera();
};

void TestTxProfiles::ogniModoTrovaIlSuoMestiere()
{
    TxProfiles profiles;

    // In fonia comanda la scelta dell'operatore.
    QCOMPARE(profiles.profileForMode(DemodMode::Usb), TxProfiles::Ragchew);
    profiles.setSsbProfile(TxProfiles::Contest);
    QCOMPARE(profiles.profileForMode(DemodMode::Usb), TxProfiles::Contest);
    QCOMPARE(profiles.profileForMode(DemodMode::Lsb), TxProfiles::Contest);

    // Fuori dalla fonia non c'è scelta da fare, e la scelta dell'operatore non
    // deve poterla scavalcare: è il punto in cui un profilo «comodo» farebbe
    // trasmettere dati compressi senza che nessuno se ne accorga.
    QCOMPARE(profiles.profileForMode(DemodMode::DigU), TxProfiles::Data);
    QCOMPARE(profiles.profileForMode(DemodMode::DigL), TxProfiles::Data);
    QCOMPARE(profiles.profileForMode(DemodMode::Iq), TxProfiles::Data);
    QCOMPARE(profiles.profileForMode(DemodMode::Cw), TxProfiles::Data);
    QCOMPARE(profiles.profileForMode(DemodMode::Cwr), TxProfiles::Data);

    // E non si può nemmeno *chiedere* di usare la catena spenta in fonia: non
    // è una voce dell'elenco, è quello che succede quando non si trasmette
    // voce.
    profiles.setSsbProfile(TxProfiles::Data);
    QCOMPARE(profiles.ssbProfile(), TxProfiles::Contest);
}

void TestTxProfiles::suiDatiLaCatenaEspenta()
{
    const TxProfileState s = TxProfiles::factory(TxProfiles::Data);

    QVERIFY(!s.gateEnabled);
    QVERIFY(!s.levellerEnabled);
    QVERIFY(!s.eqEnabled);
    QVERIFY(!s.cfcEnabled);
    QVERIFY(!s.limiterEnabled);
    QCOMPARE(s.compressionDb, 0.0);

    // E il livello parte più basso: sui dati si trasmette a ciclo pieno, e un
    // finale tenuto al massimo per quindici secondi scalda come non fa mai in
    // fonia.
    QVERIFY(s.drive < TxProfiles::factory(TxProfiles::Ragchew).drive);
}

void TestTxProfiles::ilContestNonEUnaChiacchierataPiuForte()
{
    const TxProfileState chat = TxProfiles::factory(TxProfiles::Ragchew);
    const TxProfileState dx = TxProfiles::factory(TxProfiles::Contest);

    // Quello che fa capire in un pile-up non è il volume: è la banda delle
    // consonanti. Se i due profili differissero solo per la compressione,
    // sarebbero lo stesso profilo con una manopola diversa — e tanto varrebbe
    // non averne due.
    QVERIFY2(dx.eq[3].gainDb > chat.eq[3].gainDb + 2.0,
             "il profilo DX non alza le consonanti");
    QVERIFY2(dx.eq[0].gainDb < chat.eq[0].gainDb - 4.0,
             "il profilo DX non toglie il rimbombo");
    QVERIFY(dx.cfcEnabled && !chat.cfcEnabled);
    QVERIFY(dx.compressionDb > chat.compressionDb);
}

void TestTxProfiles::unProfiloSiRicordaEsiRimette()
{
    TxProfiles profiles;

    // Si regola la propria voce in chiacchierata…
    TxProfileState mine = profiles.state(TxProfiles::Ragchew);
    mine.eq[2].gainDb = 5.5;
    mine.compressionDb = 7.0;
    profiles.setState(TxProfiles::Ragchew, mine);

    // …si passa ai dati, e quando si torna la si ritrova. È tutta la
    // differenza fra un profilo che è una memoria e uno che è una tassa da
    // pagare ogni volta che si cambia modo.
    QCOMPARE(profiles.state(TxProfiles::Data).compressionDb, 0.0);
    QCOMPARE(profiles.state(TxProfiles::Ragchew).eq[2].gainDb, 5.5);
    QCOMPARE(profiles.state(TxProfiles::Ragchew).compressionDb, 7.0);

    // E non si è sporcato l'altro profilo di fonia: sono due mestieri, non due
    // sfumature dello stesso.
    QCOMPARE(profiles.state(TxProfiles::Contest).compressionDb,
             TxProfiles::factory(TxProfiles::Contest).compressionDb);
}

void TestTxProfiles::laVoceDiFabbricaSiRecupera()
{
    TxProfiles profiles;

    TxProfileState wrecked = profiles.state(TxProfiles::Ragchew);
    wrecked.eq[0].gainDb = 12.0;
    wrecked.compressionDb = 20.0;
    profiles.setState(TxProfiles::Ragchew, wrecked);

    // La via di ritorno esiste sempre. Un profilo che si può solo peggiorare
    // è un profilo che nessuno osa toccare.
    profiles.restoreDefaults(TxProfiles::Ragchew);
    QCOMPARE(profiles.state(TxProfiles::Ragchew).compressionDb,
             TxProfiles::factory(TxProfiles::Ragchew).compressionDb);
    QCOMPARE(profiles.state(TxProfiles::Ragchew).eq[0].gainDb,
             TxProfiles::factory(TxProfiles::Ragchew).eq[0].gainDb);
}

QTEST_MAIN(TestTxProfiles)
#include "tst_tx_profiles.moc"
