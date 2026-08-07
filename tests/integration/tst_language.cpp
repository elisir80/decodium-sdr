// SPDX-License-Identifier: GPL-3.0-or-later
// Pipeline di traduzione (RF-18).
//
// Verifica che i .qm siano davvero incorporati e che cambiare lingua cambi
// davvero i testi: una traduzione che non si carica è indistinguibile da una
// traduzione assente, e ce ne si accorge solo guardando l'applicazione.

#include "core/LanguageManager.h"

#include <QCoreApplication>
#include <QTest>

using namespace dsdr;
using namespace dsdr::core;

class TestLanguage : public QObject
{
    Q_OBJECT

private slots:
    void declaresFourteenLanguages();
    void englishTranslationIsBundledAndComplete();
    void switchingLanguageChangesTranslatedText();
    void unknownLanguageIsRefused();
    void nativeNamesAreInTheirOwnLanguage();
};

void TestLanguage::declaresFourteenLanguages()
{
    // RF-18: quattordici lingue.
    QCOMPARE(LanguageManager::supportedLanguages().size(), 14);
    QVERIFY(LanguageManager::supportedLanguages().contains(QStringLiteral("it")));
    QVERIFY(LanguageManager::supportedLanguages().contains(QStringLiteral("en")));
}

void TestLanguage::englishTranslationIsBundledAndComplete()
{
    LanguageManager manager;

    const QVariantList languages = manager.availableLanguages();
    QCOMPARE(languages.size(), 14);

    bool englishAvailable = false;
    for (const QVariant &entry : languages) {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("code")).toString() == QLatin1String("en"))
            englishAvailable = item.value(QStringLiteral("available")).toBool();
    }

    QVERIFY2(englishAvailable,
             "l'inglese non risulta disponibile: il .qm non è stato incorporato "
             "nelle risorse");
}

void TestLanguage::switchingLanguageChangesTranslatedText()
{
    LanguageManager manager;
    QCOMPARE(manager.currentLanguage(), QStringLiteral("it"));

    // Stringa presa dal contesto del SessionManager, tradotta nel file en.
    const char *context = "dsdr::core::SessionManager";
    const char *source = "Nessun device trovato.";

    QCOMPARE(QCoreApplication::translate(context, source), QString::fromUtf8(source));

    QVERIFY2(manager.setLanguage(QStringLiteral("en")), "passaggio all'inglese fallito");
    QCOMPARE(manager.currentLanguage(), QStringLiteral("en"));

    const QString translated = QCoreApplication::translate(context, source);
    QVERIFY2(translated != QString::fromUtf8(source),
             qPrintable(QStringLiteral("testo non tradotto: '%1'").arg(translated)));
    QCOMPARE(translated, QStringLiteral("No devices found."));

    // Tornare alla lingua sorgente deve rimuovere il traduttore.
    QVERIFY(manager.setLanguage(QStringLiteral("it")));
    QCOMPARE(QCoreApplication::translate(context, source), QString::fromUtf8(source));
}

void TestLanguage::unknownLanguageIsRefused()
{
    LanguageManager manager;
    QVERIFY2(!manager.setLanguage(QStringLiteral("xx")), "lingua inesistente accettata");
    QCOMPARE(manager.currentLanguage(), QStringLiteral("it"));
}

void TestLanguage::nativeNamesAreInTheirOwnLanguage()
{
    // In un elenco di lingue il nome nella lingua stessa è l'unica forma che
    // chi la parla riconosce a colpo d'occhio.
    QCOMPARE(LanguageManager::nativeName(QStringLiteral("de")), QStringLiteral("Deutsch"));
    QCOMPARE(LanguageManager::nativeName(QStringLiteral("fr")), QStringLiteral("Français"));
    QCOMPARE(LanguageManager::nativeName(QStringLiteral("ja")), QString::fromUtf8("日本語"));
}

QTEST_MAIN(TestLanguage)

#include "tst_language.moc"
