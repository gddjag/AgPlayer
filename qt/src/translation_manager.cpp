#include "translation_manager.hpp"

#include <QCoreApplication>

TranslationManager::TranslationManager(QObject* parent)
    : QObject(parent)
{
}

QStringList TranslationManager::supportedLanguages()
{
    return {
        QStringLiteral("zh"),
        QStringLiteral("en"),
    };
}

QString TranslationManager::normalizedLanguage(const QString& language)
{
    const QString normalized = language.trimmed().toLower();
    return supportedLanguages().contains(normalized)
        ? normalized
        : QStringLiteral("zh");
}

QString TranslationManager::language() const
{
    return language_;
}

bool TranslationManager::setLanguage(const QString& language)
{
    const QString normalized = normalizedLanguage(language);
    if (normalized == language_ && translatorInstalled_) {
        return true;
    }

    const QString previousLanguage = language_;
    if (translatorInstalled_) {
        QCoreApplication::removeTranslator(&translator_);
        translatorInstalled_ = false;
    }

    const QString resource = QStringLiteral(":/i18n/agplayer_%1.qm").arg(normalized);
    if (!translator_.load(resource)) {
        const QString fallback = previousLanguage.isEmpty()
            ? QStringLiteral("zh") : previousLanguage;
        const QString fallbackResource =
            QStringLiteral(":/i18n/agplayer_%1.qm").arg(fallback);
        if (translator_.load(fallbackResource)) {
            QCoreApplication::installTranslator(&translator_);
            translatorInstalled_ = true;
            language_ = fallback;
        } else {
            language_.clear();
        }
        if (language_ != previousLanguage) {
            emit languageChanged();
        }
        return false;
    }
    QCoreApplication::installTranslator(&translator_);
    translatorInstalled_ = true;

    language_ = normalized;
    emit languageChanged();
    return true;
}
