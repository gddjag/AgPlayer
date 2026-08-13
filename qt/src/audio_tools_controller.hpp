#pragma once

#include <QObject>
#include <QString>

class AudioToolsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentTool READ currentTool WRITE setCurrentTool NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolId READ currentToolId NOTIFY currentToolChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)

public:
    explicit AudioToolsController(QObject* parent = nullptr);

    int currentTool() const noexcept;
    QString currentToolId() const;
    bool visible() const noexcept;
    void setCurrentTool(int tool);

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void selectTool(int tool);
    Q_INVOKABLE void selectToolById(const QString& toolId);
    Q_INVOKABLE QString toolIdForIndex(int tool) const;
    Q_INVOKABLE int toolIndexForId(const QString& toolId) const;

signals:
    void currentToolChanged();
    void visibleChanged();
    void showRequested();
    void hideRequested();

private:
    int currentTool_ = 0;  // Default: Light Editor
    bool visible_ = false;
};
