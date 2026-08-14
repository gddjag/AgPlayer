#include "equalizer_controller.hpp"

#include "graphic_equalizer.hpp"

#include <QSettings>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

constexpr std::array<double, AG_EQUALIZER_BAND_COUNT> kFrequencies{
    31.25, 62.5, 125.0, 250.0, 500.0,
    1'000.0, 2'000.0, 4'000.0, 8'000.0, 16'000.0};

QString frequencyLabel(const int row)
{
    if (row < 0 || row >= static_cast<int>(kFrequencies.size())) {
        return {};
    }
    const double frequency = kFrequencies[static_cast<std::size_t>(row)];
    if (frequency >= 1'000.0) {
        return QString::number(frequency / 1'000.0, 'g', 3)
               + QStringLiteral(" kHz");
    }
    return QString::number(frequency, 'g', 4) + QStringLiteral(" Hz");
}

QVariantList gainsToVariant(
    const std::array<double, AG_EQUALIZER_BAND_COUNT>& gains)
{
    QVariantList result;
    result.reserve(AG_EQUALIZER_BAND_COUNT);
    for (const double gain : gains) {
        result.push_back(gain);
    }
    return result;
}

std::array<double, AG_EQUALIZER_BAND_COUNT> gainsFromVariant(
    const QVariant& value)
{
    std::array<double, AG_EQUALIZER_BAND_COUNT> result{};
    const QVariantList values = value.toList();
    for (int index = 0;
         index < values.size() && index < AG_EQUALIZER_BAND_COUNT; ++index) {
        const double gain = values.at(index).toDouble();
        if (std::isfinite(gain) && gain >= -12.0 && gain <= 12.0) {
            result[static_cast<std::size_t>(index)] =
                qRound(gain * 10.0) / 10.0;
        }
    }
    return result;
}

} // namespace

EqualizerController::EqualizerController(ag_player* player, QObject* parent)
    : QAbstractListModel(parent)
    , player_(player)
{
    load();
    (void)submit();
}

int EqualizerController::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : AG_EQUALIZER_BAND_COUNT;
}

QVariant EqualizerController::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= AG_EQUALIZER_BAND_COUNT) {
        return {};
    }
    const std::size_t row = static_cast<std::size_t>(index.row());
    switch (role) {
    case FrequencyRole:
        return kFrequencies[row];
    case LabelRole:
        return frequencyLabel(index.row());
    case GainRole:
        return gains_[row];
    case MinimumRole:
        return -12.0;
    case MaximumRole:
        return 12.0;
    case StepRole:
        return 0.1;
    default:
        return {};
    }
}

QHash<int, QByteArray> EqualizerController::roleNames() const
{
    return {{FrequencyRole, "frequencyHz"},
            {LabelRole, "frequencyLabel"},
            {GainRole, "gainDb"},
            {MinimumRole, "minimumDb"},
            {MaximumRole, "maximumDb"},
            {StepRole, "stepDb"}};
}

bool EqualizerController::enabled() const noexcept { return enabled_; }

void EqualizerController::setEnabled(const bool enabled)
{
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    emit enabledChanged();
    (void)submit();
}

bool EqualizerController::bypassed() const noexcept { return bypassed_; }

void EqualizerController::setBypassed(const bool bypassed)
{
    if (bypassed_ == bypassed) {
        return;
    }
    bypassed_ = bypassed;
    emit bypassedChanged();
    (void)submit();
}

bool EqualizerController::autoClipProtection() const noexcept
{
    return autoClipProtection_;
}

void EqualizerController::setAutoClipProtection(const bool enabled)
{
    if (autoClipProtection_ == enabled) {
        return;
    }
    autoClipProtection_ = enabled;
    emit autoClipProtectionChanged();
    (void)submit();
}

double EqualizerController::preampDb() const noexcept { return preampDb_; }

void EqualizerController::setPreampDb(const double value)
{
    if (!std::isfinite(value) || value < -12.0 || value > 12.0) {
        return;
    }
    const double normalized = normalizedGain(value);
    if (qFuzzyCompare(preampDb_ + 13.0, normalized + 13.0)) {
        return;
    }
    preampDb_ = normalized;
    setCurrentPresetId(QStringLiteral("custom"));
    emit preampDbChanged();
    (void)submit();
}

double EqualizerController::protectionDb() const noexcept
{
    return protectionDb_;
}

QString EqualizerController::currentPresetId() const
{
    return currentPresetId_;
}

QStringList EqualizerController::presetIds() const
{
    QStringList result;
    for (const Preset& preset : builtInPresets()) {
        result.push_back(preset.id);
    }
    for (const Preset& preset : customPresets_) {
        result.push_back(preset.id);
    }
    return result;
}

QStringList EqualizerController::presetNames() const
{
    QStringList result;
    for (const Preset& preset : builtInPresets()) {
        result.push_back(preset.name);
    }
    for (const Preset& preset : customPresets_) {
        result.push_back(preset.name);
    }
    return result;
}

double EqualizerController::bandGain(const int index) const noexcept
{
    return index >= 0 && index < AG_EQUALIZER_BAND_COUNT
               ? gains_[static_cast<std::size_t>(index)]
               : 0.0;
}

bool EqualizerController::setBandGain(const int index, const double value)
{
    if (index < 0 || index >= AG_EQUALIZER_BAND_COUNT
        || !std::isfinite(value) || value < -12.0 || value > 12.0) {
        return false;
    }
    const double normalized = normalizedGain(value);
    double& gain = gains_[static_cast<std::size_t>(index)];
    if (qFuzzyCompare(gain + 13.0, normalized + 13.0)) {
        return true;
    }
    gain = normalized;
    emit dataChanged(this->index(index, 0), this->index(index, 0),
                     {GainRole});
    emit bandGainChanged(index, normalized);
    setCurrentPresetId(QStringLiteral("custom"));
    return submit();
}

void EqualizerController::resetBand(const int index)
{
    (void)setBandGain(index, 0.0);
}

void EqualizerController::resetAll()
{
    beginResetModel();
    gains_.fill(0.0);
    endResetModel();
    if (preampDb_ != 0.0) {
        preampDb_ = 0.0;
        emit preampDbChanged();
    }
    setCurrentPresetId(QStringLiteral("flat"));
    (void)submit();
}

bool EqualizerController::applyPreset(const QString& id)
{
    const std::optional<Preset> preset = findPreset(id);
    if (!preset.has_value()) {
        return false;
    }
    beginResetModel();
    gains_ = preset->gains;
    endResetModel();
    if (preampDb_ != preset->preampDb) {
        preampDb_ = preset->preampDb;
        emit preampDbChanged();
    }
    setCurrentPresetId(id);
    return submit();
}

QString EqualizerController::saveCustomPreset(const QString& name)
{
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty()) {
        return {};
    }
    Preset preset;
    preset.id = QStringLiteral("custom-")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = normalizedName;
    preset.preampDb = preampDb_;
    preset.gains = gains_;
    customPresets_.push_back(preset);
    setCurrentPresetId(preset.id);
    persist();
    emit presetsChanged();
    return preset.id;
}

bool EqualizerController::renameCustomPreset(const QString& id,
                                             const QString& name)
{
    const QString normalizedName = name.trimmed();
    if (normalizedName.isEmpty()) {
        return false;
    }
    for (Preset& preset : customPresets_) {
        if (preset.id == id) {
            preset.name = normalizedName;
            persist();
            emit presetsChanged();
            return true;
        }
    }
    return false;
}

bool EqualizerController::deleteCustomPreset(const QString& id)
{
    const auto iterator = std::find_if(
        customPresets_.begin(), customPresets_.end(),
        [&id](const Preset& preset) { return preset.id == id; });
    if (iterator == customPresets_.end()) {
        return false;
    }
    customPresets_.erase(iterator);
    if (currentPresetId_ == id) {
        setCurrentPresetId(QStringLiteral("custom"));
    }
    persist();
    emit presetsChanged();
    return true;
}

QVariantList EqualizerController::responseCurve(const int pointCount) const
{
    QVariantList response;
    if (pointCount < 2) {
        return response;
    }
    response.reserve(pointCount);
    if (!enabled_ || bypassed_) {
        for (int index = 0; index < pointCount; ++index) {
            response.push_back(0.0);
        }
        return response;
    }

    agplayer::GraphicEqSettings settings;
    settings.enabled = enabled_;
    settings.bypassed = bypassed_;
    settings.auto_clip_protection = autoClipProtection_;
    settings.preamp_db = preampDb_;
    settings.band_gain_db = gains_;
    const auto program = agplayer::prepare_graphic_eq(settings, 48'000, 1U);
    if (!program.has_value()) {
        return response;
    }
    constexpr double minimumFrequency = 20.0;
    constexpr double maximumFrequency = 20'000.0;
    const double ratio = maximumFrequency / minimumFrequency;
    for (int index = 0; index < pointCount; ++index) {
        const double fraction = static_cast<double>(index)
                                / static_cast<double>(pointCount - 1);
        const double frequency = minimumFrequency * std::pow(ratio, fraction);
        response.push_back(
            agplayer::graphic_eq_response_db(*program, frequency));
    }
    return response;
}

void EqualizerController::refreshStatus()
{
    if (player_ == nullptr) {
        return;
    }
    ag_equalizer_status status{};
    if (ag_player_equalizer_status(player_, &status) != AG_OK) {
        return;
    }
    if (!qFuzzyCompare(protectionDb_ + 24.0,
                       status.protection_db + 24.0)) {
        protectionDb_ = status.protection_db;
        emit protectionDbChanged();
    }
}

double EqualizerController::normalizedGain(const double value) noexcept
{
    return qRound(value * 10.0) / 10.0;
}

QList<EqualizerController::Preset> EqualizerController::builtInPresets() const
{
    const auto preset = [](QString id, QString name, double preamp,
                           std::array<double, AG_EQUALIZER_BAND_COUNT> gains) {
        return Preset{std::move(id), std::move(name), preamp, gains, true};
    };
    return {
        preset(QStringLiteral("flat"), tr("Flat"), 0.0,
               {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        preset(QStringLiteral("bass-boost"), tr("Bass Boost"), -4.5,
               {4.5, 4, 3, 1.5, 0, 0, 0, 0, 0, 0}),
        preset(QStringLiteral("bass-cut"), tr("Bass Cut"), 0.0,
               {-4.5, -4, -3, -1.5, 0, 0, 0, 0, 0, 0}),
        preset(QStringLiteral("vocal"), tr("Vocal Clarity"), -2.5,
               {-2, -1.5, -0.5, 0.5, 1.5, 2.5, 2, 1, 0, -1}),
        preset(QStringLiteral("treble-boost"), tr("Treble Boost"), -4.5,
               {0, 0, 0, 0, 0, 0, 1, 2, 3.5, 4.5}),
        preset(QStringLiteral("treble-cut"), tr("Treble Cut"), 0.0,
               {0, 0, 0, 0, 0, 0, -1, -2, -3.5, -4.5}),
        preset(QStringLiteral("rock"), tr("Rock"), -3.5,
               {3.5, 3, 1, -1.5, -2, 1, 2.5, 3, 3.5, 2})};
}

std::optional<EqualizerController::Preset> EqualizerController::findPreset(
    const QString& id) const
{
    for (const Preset& preset : builtInPresets()) {
        if (preset.id == id) {
            return preset;
        }
    }
    for (const Preset& preset : customPresets_) {
        if (preset.id == id) {
            return preset;
        }
    }
    return std::nullopt;
}

void EqualizerController::load()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    enabled_ = settings.value(QStringLiteral("enabled"), false).toBool();
    bypassed_ = settings.value(QStringLiteral("bypassed"), false).toBool();
    autoClipProtection_ =
        settings.value(QStringLiteral("autoClipProtection"), true).toBool();
    preampDb_ = normalizedGain(
        settings.value(QStringLiteral("preampDb"), 0.0).toDouble());
    gains_ = gainsFromVariant(settings.value(QStringLiteral("bandGains")));
    currentPresetId_ =
        settings.value(QStringLiteral("currentPresetId"),
                       QStringLiteral("flat")).toString();
    const int count = settings.beginReadArray(QStringLiteral("customPresets"));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        Preset preset;
        preset.id = settings.value(QStringLiteral("id")).toString();
        preset.name = settings.value(QStringLiteral("name")).toString();
        preset.preampDb = normalizedGain(
            settings.value(QStringLiteral("preampDb"), 0.0).toDouble());
        preset.gains = gainsFromVariant(
            settings.value(QStringLiteral("bandGains")));
        if (!preset.id.isEmpty() && !preset.name.isEmpty()) {
            customPresets_.push_back(std::move(preset));
        }
    }
    settings.endArray();
    settings.endGroup();
}

void EqualizerController::persist() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("equalizer"));
    settings.setValue(QStringLiteral("enabled"), enabled_);
    settings.setValue(QStringLiteral("bypassed"), bypassed_);
    settings.setValue(QStringLiteral("autoClipProtection"),
                      autoClipProtection_);
    settings.setValue(QStringLiteral("preampDb"), preampDb_);
    settings.setValue(QStringLiteral("bandGains"), gainsToVariant(gains_));
    settings.setValue(QStringLiteral("currentPresetId"), currentPresetId_);
    settings.beginWriteArray(QStringLiteral("customPresets"),
                             customPresets_.size());
    for (int index = 0; index < customPresets_.size(); ++index) {
        settings.setArrayIndex(index);
        const Preset& preset = customPresets_.at(index);
        settings.setValue(QStringLiteral("id"), preset.id);
        settings.setValue(QStringLiteral("name"), preset.name);
        settings.setValue(QStringLiteral("preampDb"), preset.preampDb);
        settings.setValue(QStringLiteral("bandGains"),
                          gainsToVariant(preset.gains));
    }
    settings.endArray();
    settings.endGroup();
    settings.sync();
}

bool EqualizerController::submit()
{
    if (player_ == nullptr) {
        emit submissionFailed();
        return false;
    }
    ag_equalizer_settings settings{};
    settings.revision = ++revision_;
    settings.enabled = enabled_ ? 1 : 0;
    settings.bypassed = bypassed_ ? 1 : 0;
    settings.auto_clip_protection = autoClipProtection_ ? 1 : 0;
    settings.preamp_db = preampDb_;
    std::copy(gains_.begin(), gains_.end(), settings.band_gain_db);
    settings.q = 1.414;
    settings.transition_ms = 25.0;
    const ag_result result = ag_player_set_equalizer(player_, &settings);
    if (result != AG_OK) {
        emit submissionFailed();
        return false;
    }
    refreshStatus();
    persist();
    emit responseCurveChanged();
    return true;
}

void EqualizerController::setCurrentPresetId(const QString& id)
{
    if (currentPresetId_ == id) {
        return;
    }
    currentPresetId_ = id;
    emit currentPresetChanged();
}
