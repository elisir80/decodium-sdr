// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/LanguageManager.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QSettings>
#include <QTranslator>

Q_DECLARE_LOGGING_CATEGORY(dsdrCore)

namespace dsdr::core {

namespace {

/// Lingua in cui sono scritte le stringhe nel codice: per essa non esiste (né
/// serve) un file di traduzione.
const QString kSourceLanguage = QStringLiteral("it");

constexpr const char *kSettingsKey = "ui/language";

struct LanguageEntry
{
    const char *code;
    const char *nativeName;
};

/// Le quattordici lingue previste da RF-18. L'ordine è quello mostrato in UI:
/// prima la lingua sorgente e l'inglese, poi le altre in ordine alfabetico di
/// codice, che è arbitrario ma stabile.
constexpr LanguageEntry kLanguages[] = {
    {"it", "Italiano"},
    {"en", "English"},
    {"cs", "Čeština"},
    {"de", "Deutsch"},
    {"es", "Español"},
    {"fr", "Français"},
    {"ja", "日本語"},
    {"lv", "Latviešu"},
    {"nl", "Nederlands"},
    {"pl", "Polski"},
    {"pt", "Português"},
    {"ru", "Русский"},
    {"uk", "Українська"},
    {"zh_CN", "简体中文"},
};

} // namespace

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
    , m_current(kSourceLanguage)
{
}

LanguageManager::~LanguageManager() = default;

QStringList LanguageManager::supportedLanguages()
{
    QStringList codes;
    codes.reserve(static_cast<int>(std::size(kLanguages)));
    for (const LanguageEntry &entry : kLanguages)
        codes.append(QString::fromLatin1(entry.code));
    return codes;
}

QString LanguageManager::nativeName(const QString &code)
{
    for (const LanguageEntry &entry : kLanguages) {
        if (code == QLatin1String(entry.code))
            return QString::fromUtf8(entry.nativeName);
    }
    return QLocale(code).nativeLanguageName();
}

QVariantList LanguageManager::availableLanguages() const
{
    QVariantList list;
    for (const LanguageEntry &entry : kLanguages) {
        const QString code = QString::fromLatin1(entry.code);

        QVariantMap item;
        item.insert(QStringLiteral("code"), code);
        item.insert(QStringLiteral("name"), QString::fromUtf8(entry.nativeName));
        item.insert(QStringLiteral("isSource"), code == kSourceLanguage);

        // Una lingua compare come disponibile solo se il suo .qm è stato
        // compilato: elencare lingue senza traduzione porterebbe l'utente a
        // sceglierne una e non vedere cambiare nulla.
        QTranslator probe;
        item.insert(QStringLiteral("available"),
                    code == kSourceLanguage
                        || probe.load(QStringLiteral(":/i18n/decodium_sdr_%1.qm").arg(code)));

        list.append(item);
    }
    return list;
}

void LanguageManager::attachEngine(QQmlEngine *engine)
{
    m_engine = engine;
}

QString LanguageManager::systemLanguage() const
{
    const QStringList supported = supportedLanguages();
    const QLocale system = QLocale::system();

    // Prima la corrispondenza piena (zh_CN), poi quella sulla sola lingua.
    const QString full = system.name();
    if (supported.contains(full))
        return full;

    const QString language = full.section(QLatin1Char('_'), 0, 0);
    if (supported.contains(language))
        return language;

    return kSourceLanguage;
}

bool LanguageManager::installTranslator(const QString &code)
{
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }

    if (code == kSourceLanguage)
        return true; // le stringhe sorgente sono già in questa lingua

    auto *translator = new QTranslator(this);
    if (!translator->load(QStringLiteral(":/i18n/decodium_sdr_%1.qm").arg(code))) {
        delete translator;
        return false;
    }
    QCoreApplication::installTranslator(translator);
    m_appTranslator = translator;

    // Anche i testi dei dialoghi standard di Qt vanno tradotti, altrimenti si
    // ottiene una finestra metà in una lingua e metà in un'altra.
    auto *qtTranslator = new QTranslator(this);
    if (qtTranslator->load(QStringLiteral("qt_%1").arg(code),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qtTranslator);
        m_qtTranslator = qtTranslator;
    } else {
        delete qtTranslator;
    }

    return true;
}

bool LanguageManager::setLanguage(const QString &code)
{
    if (!supportedLanguages().contains(code)) {
        qCWarning(dsdrCore) << "lingua non prevista:" << code;
        return false;
    }
    if (code == m_current)
        return true;

    if (!installTranslator(code)) {
        qCWarning(dsdrCore) << "traduzione non disponibile per" << code;
        return false;
    }

    m_current = code;
    QSettings().setValue(QLatin1String(kSettingsKey), code);

    // Rivaluta tutti i binding qsTr() senza riavviare l'applicazione.
    if (m_engine)
        m_engine->retranslate();

    emit languageChanged();
    qCInfo(dsdrCore) << "lingua impostata:" << code;
    return true;
}

void LanguageManager::restoreSavedLanguage()
{
    const QString saved = QSettings().value(QLatin1String(kSettingsKey)).toString();
    const QString wanted = saved.isEmpty() ? systemLanguage() : saved;

    if (wanted == m_current)
        return;

    // Se la lingua salvata non è più disponibile si resta alla sorgente,
    // invece di avviarsi con una interfaccia vuota.
    if (!installTranslator(wanted)) {
        qCWarning(dsdrCore) << "traduzione mancante per" << wanted << "— si resta in"
                            << kSourceLanguage;
        return;
    }

    m_current = wanted;
    emit languageChanged();
}

} // namespace dsdr::core
