// SPDX-License-Identifier: GPL-3.0-or-later
// L'ospite dei plugin: che l'audio passi, e soprattutto che la radio resti in
// piedi quando il plugin no.
//
// Il secondo è il motivo per cui tutto questo gira in un processo a parte, e
// non si può verificare rileggendo il codice: si verifica ammazzando l'ospite
// e guardando che cosa succede a chi gli stava mandando i campioni.
//
// Il plugin con cui si prova è il ritardo d'esempio dell'SDK, compilato
// insieme al test: sulla macchina di sviluppo non ce n'è nessuno installato, e
// senza un plugin vero l'unica cosa dimostrabile sarebbe che il protocollo
// parla — non che l'audio attraversi davvero una libreria di terze parti.
#include "plugins/PluginBridge.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <vector>

using namespace dsdr::plugins;

class TestPluginHost : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void lospiteSiPresenta();
    void unPluginCheNonEsisteLoDice();
    void ilSegnaleAttraversaUnPluginVero();
    void spentoNonAttraversaNiente();
    void seIlPluginMuoreLaRadioResta();

private:
    QString m_plugin{QStringLiteral(DSDR_TEST_PLUGIN)};
};

void TestPluginHost::initTestCase()
{
    QVERIFY2(PluginBridge::hostAvailable(),
             "l'ospite non è accanto al test: senza, non c'è niente da provare");
    QVERIFY2(QFile::exists(m_plugin),
             qPrintable(QStringLiteral("il plugin di prova non c'è: %1").arg(m_plugin)));
}

void TestPluginHost::lospiteSiPresenta()
{
    PluginBridge bridge;
    QVERIFY2(bridge.start(), qPrintable(bridge.lastError()));
    QVERIFY(bridge.isRunning());

    // La scansione non deve fallire nemmeno su una macchina senza plugin
    // installati: «nessuno» è una risposta, «non risponde» è un guasto.
    bridge.scan();
    QVERIFY(bridge.isRunning());

    bridge.stop();
    QVERIFY(!bridge.isRunning());
}

void TestPluginHost::unPluginCheNonEsisteLoDice()
{
    PluginBridge bridge;
    QVERIFY(bridge.start());

    QVERIFY(!bridge.load(QStringLiteral("C:/questo/non/esiste.vst3")));
    QVERIFY2(!bridge.lastError().isEmpty(),
             "un caricamento fallito senza spiegazione lascia l'operatore a "
             "guardare un blocco spento senza sapere perché");

    // E l'ospite è ancora vivo: un file sbagliato non è un motivo per
    // ricominciare da capo.
    QVERIFY(bridge.isRunning());
}

void TestPluginHost::ilSegnaleAttraversaUnPluginVero()
{
    PluginBridge bridge;
    QVERIFY(bridge.start());
    QVERIFY2(bridge.load(m_plugin), qPrintable(bridge.lastError()));
    QVERIFY(!bridge.loadedName().isEmpty());
    QVERIFY2(!bridge.parameters().isEmpty(),
             "un plugin senza parametri esposti non si può governare senza la "
             "sua finestra, e la sua finestra qui non c'è");

    bridge.prepare(48000.0, 512);
    bridge.setEnabled(true);

    // Il ritardo al massimo: quello che entra non deve uscire subito.
    bridge.setParameter(bridge.parameters().constFirst().index, 1.0);

    std::vector<float> block(512, 0.0f);
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = 0.5f;

    bridge.process(block.data(), block.size());

    // Con un ritardo lungo il primo blocco esce silenzioso: è la prova che il
    // segnale è passato **dentro** il plugin e non gli è girato attorno. Un
    // ponte che restituisse l'ingresso intatto sembrerebbe funzionare, e
    // sarebbe il modo più silenzioso di essere rotto.
    float peak = 0.0f;
    for (float sample : block)
        peak = std::max(peak, std::abs(sample));

    QVERIFY2(peak < 0.4f,
             qPrintable(QStringLiteral("il blocco esce a %1: il plugin non l'ha "
                                       "toccato").arg(peak)));
}

void TestPluginHost::spentoNonAttraversaNiente()
{
    PluginBridge bridge;
    QVERIFY(bridge.start());
    QVERIFY(bridge.load(m_plugin));
    bridge.prepare(48000.0, 256);
    bridge.setParameter(bridge.parameters().constFirst().index, 1.0);

    // Da spento il blocco non deve nemmeno attraversare il processo: uno
    // stadio in bypass deve costare zero, e qui «costare» vuol dire due
    // attraversamenti di pipe per blocco.
    bridge.setEnabled(false);

    std::vector<float> block(256, 0.5f);
    bridge.process(block.data(), block.size());
    for (float sample : block)
        QCOMPARE(sample, 0.5f);
}

void TestPluginHost::seIlPluginMuoreLaRadioResta()
{
    PluginBridge bridge;
    QVERIFY(bridge.start());
    QVERIFY(bridge.load(m_plugin));
    bridge.prepare(48000.0, 256);
    bridge.setEnabled(true);

    QSignalSpy died(&bridge, &PluginBridge::hostDied);

    // Si ammazza l'ospite come lo ammazzerebbe un plugin che sbaglia un
    // indice. Questa è la prova per cui esiste tutto il resto: da qui in poi
    // il ponte deve continuare a rispondere, e il segnale deve passare com'è.
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/IM"),
                       QStringLiteral("decodium-vst-host.exe"),
                       QStringLiteral("/F")});
    QVERIFY(died.wait(3000));
    QCOMPARE(bridge.crashCount(), quint64(1));

    std::vector<float> block(256, 0.5f);
    bridge.process(block.data(), block.size());

    // Il segnale esce identico: bypass, non silenzio. Un blocco che si zittisce
    // quando il suo plugin muore toglie l'aria a una stazione che sta
    // chiamando, ed è peggio del difetto che stava cercando di gestire.
    for (float sample : block)
        QCOMPARE(sample, 0.5f);

    QVERIFY2(!bridge.lastError().isEmpty(),
             "l'ospite è morto e nessuno lo dice all'operatore");
}

QTEST_MAIN(TestPluginHost)
#include "tst_plugin_host.moc"
