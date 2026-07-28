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
        QStringLiteral("th"),
        QStringLiteral("vi"),
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
    if (normalized == language_) {
        return true;
    }

    QCoreApplication::removeTranslator(&translator_);
    if (normalized != QStringLiteral("zh")) {
        const QString resource = QStringLiteral(":/i18n/agplayer_%1.qm").arg(normalized);
        if (!translator_.load(resource)) {
            language_ = QStringLiteral("zh");
            emit languageChanged();
            return false;
        }
        QCoreApplication::installTranslator(&translator_);
    }

    language_ = normalized;
    emit languageChanged();
    return true;
}
