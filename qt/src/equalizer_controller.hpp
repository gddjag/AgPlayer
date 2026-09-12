#pragma once

#include <agplayer/c_api.h>

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>

#include <array>
#include <optional>

using EqualizerSubmitFunction = ag_result (*)(
    ag_player*, const ag_equalizer_settings*);

class EqualizerController final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool bypassed READ bypassed WRITE setBypassed NOTIFY bypassedChanged)
    Q_PROPERTY(bool autoClipProtection READ autoClipProtection
                   WRITE setAutoClipProtection NOTIFY autoClipProtectionChanged)
    Q_PROPERTY(double preampDb READ preampDb WRITE setPreampDb
                   NOTIFY preampDbChanged)
    Q_PROPERTY(double protectionDb READ protectionDb NOTIFY protectionDbChanged)
    Q_PROPERTY(double outputPeakDb READ outputPeakDb NOTIFY outputPeakDbChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool sampleRateSupported READ sampleRateSupported
                   NOTIFY sampleRateSupportedChanged)
    Q_PROPERTY(double gainRangeDb READ gainRangeDb WRITE setGainRangeDb
                   NOTIFY gainRangeDbChanged)
    Q_PROPERTY(QString precisionMode READ precisionMode WRITE setPrecisionMode
                   NOTIFY precisionModeChanged)
    Q_PROPERTY(double gainStepDb READ gainStepDb NOTIFY gainStepDbChanged)
    Q_PROPERTY(QString currentPresetId READ currentPresetId
                   NOTIFY currentPresetChanged)
    Q_PROPERTY(QStringList presetIds READ presetIds NOTIFY presetsChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)

public:
    enum Role {
        FrequencyRole = Qt::UserRole + 1,
        LabelRole,
        GainRole,
        MinimumRole,
        MaximumRole,
        StepRole
    };
    Q_ENUM(Role)

    explicit EqualizerController(
        ag_player* player, QObject* parent = nullptr,
        EqualizerSubmitFunction submitFunction = &ag_player_set_equalizer);

    [[nodiscard]] int rowCount(
        const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool enabled() const noexcept;
    void setEnabled(bool enabled);
    [[nodiscard]] bool bypassed() const noexcept;
    void setBypassed(bool bypassed);
    [[nodiscard]] bool autoClipProtection() const noexcept;
    void setAutoClipProtection(bool enabled);
    [[nodiscard]] double preampDb() const noexcept;
    void setPreampDb(double value);
    [[nodiscard]] double protectionDb() const noexcept;
    [[nodiscard]] double outputPeakDb() const noexcept;
    [[nodiscard]] int sampleRate() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool sampleRateSupported() const noexcept;
    [[nodiscard]] double gainRangeDb() const noexcept;
    Q_INVOKABLE bool setGainRangeDb(double value);
    [[nodiscard]] QString precisionMode() const;
    Q_INVOKABLE bool setPrecisionMode(const QString& mode);
    [[nodiscard]] double gainStepDb() const noexcept;
    [[nodiscard]] QString currentPresetId() const;
    [[nodiscard]] QStringList presetIds() const;
    [[nodiscard]] QStringList presetNames() const;

    Q_INVOKABLE [[nodiscard]] double bandGain(int index) const noexcept;
    Q_INVOKABLE [[nodiscard]] bool setBandGain(int index, double value);
    Q_INVOKABLE void resetBand(int index);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE [[nodiscard]] bool applyPreset(const QString& id);
    Q_INVOKABLE [[nodiscard]] QString saveCustomPreset(const QString& name);
    Q_INVOKABLE [[nodiscard]] bool renameCustomPreset(const QString& id,
                                                      const QString& name);
    Q_INVOKABLE [[nodiscard]] bool deleteCustomPreset(const QString& id);
    Q_INVOKABLE [[nodiscard]] QVariantList responseCurve(int pointCount) const;
    Q_INVOKABLE void refreshStatus();

signals:
    void enabledChanged();
    void bypassedChanged();
    void autoClipProtectionChanged();
    void preampDbChanged();
    void protectionDbChanged();
    void outputPeakDbChanged();
    void sampleRateChanged();
    void activeChanged();
    void sampleRateSupportedChanged();
    void gainRangeDbChanged();
    void precisionModeChanged();
    void gainStepDbChanged();
    void bandGainChanged(int index, double gainDb);
    void currentPresetChanged();
    void presetsChanged();
    void responseCurveChanged();
    void submissionFailed();

private:
    struct Preset {
        QString id;
        QString name;
        double preampDb = 0.0;
        std::array<double, AG_EQUALIZER_BAND_COUNT> gains{};
        bool builtIn = false;
    };

    struct SubmittedState {
        // AudioEngine starts with this same EQ default before the controller's
        // first submission, so it is a valid rollback baseline.
        bool valid = true;
        bool enabled = true;
        bool bypassed = false;
        bool autoClipProtection = true;
        double preampDb = 0.0;
        double gainRangeDb = 12.0;
        std::array<double, AG_EQUALIZER_BAND_COUNT> gains{};
        QString currentPresetId = QStringLiteral("flat");
    };

    static double normalizedGain(double value) noexcept;
    [[nodiscard]] double quantizedGain(double value) const noexcept;
    [[nodiscard]] QList<Preset> builtInPresets() const;
    [[nodiscard]] std::optional<Preset> findPreset(const QString& id) const;
    void load();
    void persist() const;
    bool submit() override;
    void rememberSubmittedState();
    void restoreSubmittedState();
    void synchronizeSubmittedMetadata();
    void setCurrentPresetId(const QString& id);

    ag_player* player_ = nullptr;
    EqualizerSubmitFunction submitFunction_ = &ag_player_set_equalizer;
    bool enabled_ = false;
    bool bypassed_ = false;
    bool autoClipProtection_ = true;
    double preampDb_ = 0.0;
    double protectionDb_ = 0.0;
    double outputPeakDb_ = -120.0;
    int sampleRate_ = 0;
    bool active_ = false;
    bool sampleRateSupported_ = true;
    double gainRangeDb_ = 12.0;
    QString precisionMode_ = QStringLiteral("high");
    std::array<double, AG_EQUALIZER_BAND_COUNT> gains_{};
    QString currentPresetId_ = QStringLiteral("flat");
    QList<Preset> customPresets_;
    quint64 revision_ = 0;
    SubmittedState submittedState_;
};
