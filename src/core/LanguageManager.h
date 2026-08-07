// SPDX-License-Identifier: GPL-3.0-or-later
// DECODIUM SDR — selezione della lingua a runtime (RF-18).
//
// Le stringhe sorgente sono oggi in italiano; l'inglese è la prima traduzione.
// Migrare la lingua sorgente all'inglese resta una decisione aperta — tracciata
// in ROADMAP.md — e non cambierebbe nulla di questa classe.
//
// Il cambio di lingua a caccia calda richiede che QML ricarichi i binding: si
// usa `retranslate()` sull'engine QML, che rivaluta ogni qsTr() senza riavvio.
#pragma once

#include <QLocale>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;
class QTranslator;

namespace dsdr::core {

class LanguageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)

public:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    /// Collega l'engine QML, così un cambio di lingua rivaluta i binding.
    void attachEngine(QQmlEngine *engine);

    /// Codice della lingua attiva ("it", "en", "de", …).
    QString currentLanguage() const { return m_current; }

    /// Elenco delle lingue disponibili: `code`, `name` (nella lingua stessa) e
    /// `translated` (percentuale di copertura, 0 per la lingua sorgente).
    QVariantList availableLanguages() const;

    /// Applica una lingua. Restituisce false se non esiste una traduzione.
    Q_INVOKABLE bool setLanguage(const QString &code);

    /// Lingua preferita dal sistema, se abbiamo una traduzione; altrimenti la
    /// lingua sorgente.
    Q_INVOKABLE QString systemLanguage() const;

    /// Carica la lingua salvata, o quella di sistema al primo avvio.
    void restoreSavedLanguage();

    /// Codici delle lingue previste dal progetto (RF-18).
    static QStringList supportedLanguages();

    /// Nome della lingua nella lingua stessa: in un elenco di lingue è l'unica
    /// forma che chi la parla riconosce a colpo d'occhio.
    static QString nativeName(const QString &code);

signals:
    void languageChanged();

private:
    bool installTranslator(const QString &code);

    QQmlEngine *m_engine = nullptr;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
    QString m_current;
};

} // namespace dsdr::core
