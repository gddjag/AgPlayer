#pragma once

#include <QObject>
#include <QStringList>
#include <QTranslator>

class TranslationManager final : public QObject {
    Q_OBJECT

public:
    explicit TranslationManager(QObject* parent = nullptr);

    static QStringList supportedLanguages();
    static QString normalizedLanguage(const QString& language);

    QString language() const;
    bool setLanguage(const QString& language);

signals:
    void languageChanged();

private:
    QTranslator translator_;
    QString language_;
    bool translatorInstalled_ = false;
};
