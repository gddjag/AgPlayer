#include "equalizer_controller.hpp"

#include "graphic_equalizer.hpp"

#include <QSettings>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

constexpr std::array<double, AG_EQUALIZER_BAND_COUNT> kFrequencies{
    20.0, 31.5, 50.0, 80.0, 125.0, 200.0, 315.0, 500.0, 800.0,
    1'250.0, 2'000.0, 3'150.0, 5'000.0, 8'000.0, 10'000.0,
    12'500.0, 16'000.0, 20'000.0};
constexpr std::array<double, 17> kSchemaTwoFrequencies{
    20.0, 31.5, 50.0, 80.0, 125.0, 200.0, 315.0, 500.0, 800.0,
    1'250.0, 2'000.0, 3'150.0, 5'000.0, 8'000.0, 12'500.0,
    16'000.0, 20'000.0};
constexpr std::array<double, 10> kLegacyFrequencies{
    31.25, 62.5, 125.0, 250.0, 500.0,
    1'000.0, 2'000.0, 4'000.0, 8'000.0, 16'000.0};
constexpr int kSettingsSchemaVersion = 3;

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

double legacyStoredGain(const double value)
{
    return std::isfinite(value) && value >= -12.0 && value <= 12.0
               ? qRound(value * 10.0) / 10.0
               : 0.0;
}

double storedDspValue(const double value)
{
    return std::isfinite(value) && value >= -18.0 && value <= 18.0
               ? value
               : 0.0;
}

std::array<double, AG_EQUALIZER_BAND_COUNT> insertTenKHzBand(
    const std::array<double, kSchemaTwoFrequencies.size()>& oldGains)
{
    std::array<double, AG_EQUALIZER_BAND_COUNT> result{};
    for (std::size_t index = 0; index < 14U; ++index) {
        result[index] = oldGains[index];
    }
    result[14] = 0.0;
    for (std::size_t index = 14U; index < oldGains.size(); ++index) {
        result[index + 1U] = oldGains[index];
    }
    return result;
}

std::array<double, AG_EQUALIZER_BAND_COUNT> migrateLegacyGains(
    const QVariantList& values)
{
    std::array<double, kSchemaTwoFrequencies.size()> result{};
    std::array<double, kLegacyFrequencies.size()> legacy{};
    for (std::size_t index = 0; index < legacy.size(); ++index) {
        legacy[index] = legacyStoredGain(
            values.at(static_cast<int>(index)).toDouble());
    }
    for (std::size_t target = 0; target < kSchemaTwoFrequencies.size(); ++target) {
        const double frequency = kSchemaTwoFrequencies[target];
        if (frequency <= kLegacyFrequencies.front()) {
            result[target] = legacy.front();
            continue;
        }
        if (frequency >= kLegacyFrequencies.back()) {
            result[target] = legacy.back();
            continue;
        }
        const auto upper = std::upper_bound(kLegacyFrequencies.cbegin(),
                                            kLegacyFrequencies.cend(), frequency);
        const std::size_t high = static_cast<std::size_t>(
            std::distance(kLegacyFrequencies.cbegin(), upper));
        const std::size_t low = high - 1U;
        const double fraction =
            std::log(frequency / kLegacyFrequencies[low])
            / std::log(kLegacyFrequencies[high] / kLegacyFrequencies[low]);
        result[target] = legacyStoredGain(
            legacy[low] + fraction * (legacy[high] - legacy[low]));
    }
    return insertTenKHzBand(result);
}

std::array<double, AG_EQUALIZER_BAND_COUNT> gainsFromVariant(
    const QVariant& value)
{
    std::array<double, AG_EQUALIZER_BAND_COUNT> result{};
    const QVariantList values = value.toList();
    if (values.size() == static_cast<int>(kLegacyFrequencies.size())) {
        return migrateLegacyGains(values);
    }
    if (values.size() == static_cast<int>(kSchemaTwoFrequencies.size())) {
        std::array<double, kSchemaTwoFrequencies.size()> oldGains{};
        for (int index = 0; index < values.size(); ++index) {
            oldGains[static_cast<std::size_t>(index)] =
                storedDspValue(values.at(index).toDouble());
        }
        return insertTenKHzBand(oldGains);
    }
    for (int index = 0;
         index < values.size() && index < AG_EQUALIZER_BAND_COUNT; ++index) {
        result[static_cast<std::size_t>(index)] =
            storedDspValue(values.at(index).toDouble());
    }
    return result;
}

} // namespace

EqualizerController::EqualizerController(
    ag_player* player, QObject* parent,
    const EqualizerSubmitFunction submitFunction)
    : QAbstractListModel(parent)
    , player_(player)
    , submitFunction_(submitFunction != nullptr ? submitFunction
                                                : &ag_player_set_equalizer)
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
        return -gainRangeDb_;
    case MaximumRole:
        return gainRangeDb_;
    case StepRole:
        return gainStepDb();
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
    if (!std::isfinite(value) || value < -gainRangeDb_
        || value > gainRangeDb_) {
        return;
    }
    const double normalized = quantizedGain(value);
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

double EqualizerController::outputPeakDb() const noexcept
{
    return outputPeakDb_;
}

int EqualizerController::sampleRate() const noexcept { return sampleRate_; }

bool EqualizerController::active() const noexcept { return active_; }

bool EqualizerController::sampleRateSupported() const noexcept
{
    return sampleRateSupported_;
}

double EqualizerController::gainRangeDb() const noexcept
{
    return gainRangeDb_;
}

bool EqualizerController::setGainRangeDb(const double value)
{
    if (value != 6.0 && value != 12.0 && value != 18.0) {
        return false;
    }
    if (qFuzzyCompare(gainRangeDb_ + 1.0, value + 1.0)) {
        return true;
    }

    gainRangeDb_ = value;
    bool gainsChanged = false;
    for (int index = 0; index < AG_EQUALIZER_BAND_COUNT; ++index) {
        double& gain = gains_[static_cast<std::size_t>(index)];
        const double clamped = std::clamp(gain, -gainRangeDb_, gainRangeDb_);
        if (!qFuzzyCompare(gain + 19.0, clamped + 19.0)) {
            gain = clamped;
            gainsChanged = true;
            emit bandGainChanged(index, gain);
        }
    }
    const double clampedPreamp =
        std::clamp(preampDb_, -gainRangeDb_, gainRangeDb_);
    const bool preampChanged =
        !qFuzzyCompare(preampDb_ + 19.0, clampedPreamp + 19.0);
    if (preampChanged) {
        preampDb_ = clampedPreamp;
        emit preampDbChanged();
    }

    emit gainRangeDbChanged();
    if (gainsChanged) {
        emit dataChanged(this->index(0, 0),
                         this->index(AG_EQUALIZER_BAND_COUNT - 1, 0),
                         {GainRole, MinimumRole, MaximumRole});
    } else {
        emit dataChanged(this->index(0, 0),
                         this->index(AG_EQUALIZER_BAND_COUNT - 1, 0),
                         {MinimumRole, MaximumRole});
    }
    if (gainsChanged || preampChanged) {
        setCurrentPresetId(QStringLiteral("custom"));
        return submit();
    }
    synchronizeSubmittedMetadata();
    persist();
    return true;
}

QString EqualizerController::precisionMode() const
{
    return precisionMode_;
}

bool EqualizerController::setPrecisionMode(const QString& mode)
{
    if (mode != QStringLiteral("high") && mode != QStringLiteral("medium")
        && mode != QStringLiteral("low")) {
        return false;
    }
    if (precisionMode_ == mode) {
        return true;
    }
    precisionMode_ = mode;
    emit precisionModeChanged();
    emit gainStepDbChanged();
    emit dataChanged(this->index(0, 0),
                     this->index(AG_EQUALIZER_BAND_COUNT - 1, 0),
                     {StepRole});
    persist();
    return true;
}

double EqualizerController::gainStepDb() const noexcept
{
    if (precisionMode_ == QStringLiteral("medium")) {
        return 0.5;
    }
    if (precisionMode_ == QStringLiteral("low")) {
        return 1.0;
    }
    return 0.1;
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
        || !std::isfinite(value) || value < -gainRangeDb_
        || value > gainRangeDb_) {
        return false;
    }
    const double normalized = quantizedGain(value);
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
    if (id != QStringLiteral("flat")) {
        if (!enabled_) {
            enabled_ = true;
            emit enabledChanged();
        }
        if (bypassed_) {
            bypassed_ = false;
            emit bypassedChanged();
        }
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
    synchronizeSubmittedMetadata();
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
    synchronizeSubmittedMetadata();
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
    const int responseRate = sampleRate_ > 0 ? sampleRate_ : 48'000;
    if (!agplayer::is_graphic_eq_sample_rate_supported(responseRate)) {
        return response;
    }
    const auto program = agplayer::prepare_graphic_eq(settings, responseRate, 1U);
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
        response.push_back(agplayer::graphic_eq_response_db(*program, frequency)
                           + program->protection_db);
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
    if (!qFuzzyCompare(outputPeakDb_ + 121.0,
                       status.output_peak_db + 121.0)) {
        outputPeakDb_ = status.output_peak_db;
        emit outputPeakDbChanged();
    }
    if (sampleRate_ != status.sample_rate) {
        sampleRate_ = status.sample_rate;
        emit sampleRateChanged();
    }
    const bool sampleRateSupported = status.sample_rate <= 0
        || agplayer::is_graphic_eq_sample_rate_supported(status.sample_rate);
    if (sampleRateSupported_ != sampleRateSupported) {
        sampleRateSupported_ = sampleRateSupported;
        emit sampleRateSupportedChanged();
    }
    if (active_ != bool(status.active)) {
        active_ = bool(status.active);
        emit activeChanged();
    }
}

double EqualizerController::normalizedGain(const double value) noexcept
{
    return static_cast<double>(qRound64(value * 10.0)) / 10.0;
}

double EqualizerController::quantizedGain(const double value) const noexcept
{
    if (precisionMode_ == QStringLiteral("medium")) {
        return static_cast<double>(qRound64(value * 2.0)) / 2.0;
    }
    if (precisionMode_ == QStringLiteral("low")) {
        return static_cast<double>(qRound64(value));
    }
    return normalizedGain(value);
}

QList<EqualizerController::Preset> EqualizerController::builtInPresets() const
{
    const auto preset = [](QString id, QString name, double preamp,
                           std::array<double, AG_EQUALIZER_BAND_COUNT> gains) {
        return Preset{std::move(id), std::move(name), preamp, gains, true};
    };
    return {
        preset(QStringLiteral("flat"), tr("Flat"), 0.0,
               {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        preset(QStringLiteral("bass"), tr("Bass"), -6.9,
               {5, 4.5, 3.5, 2.5, 1.5, 0.5, 0, 0, -0.5, -0.5,
                0, 0, 0, 0, 0, 0, 0, 0}),
        preset(QStringLiteral("classical"), tr("Classical"), -4.5,
               {2.5, 2, 1.5, 0.5, -0.5, -1, -1, -0.5, 0.5, 1.5,
                2, 2.5, 3, 2.5, 0, 1.5, 0.5, 0}),
        preset(QStringLiteral("pop"), tr("Pop"), -3.8,
               {-0.5, 0, 1, 2, 2.5, 1.5, 0, -1, -1, 0,
                1, 2, 2.5, 2, 0, 1, 0, -0.5}),
        preset(QStringLiteral("rock"), tr("Rock"), -5.4,
               {4, 3.5, 2, 0, -1.5, -2, -1, 0.5, 2, 3,
                3.5, 3, 2.5, 2, 0, 1.5, 1, 0}),
        preset(QStringLiteral("vocal"), tr("Vocal"), -4.9,
               {-3, -2.5, -2, -1, -0.5, 0.5, 1.5, 2.5, 3, 3,
                2.5, 2, 1, -0.5, 0, -1.5, -2, -2}),
        preset(QStringLiteral("edm"), tr("EDM"), -6.6,
               {4.5, 4, 3.5, 2, 0.5, -1, -1.5, -1, 0, 1.5,
                3, 4, 4.5, 4, 0, 3, 2, 1}),
        preset(QStringLiteral("jazz"), tr("Jazz"), -4.7,
               {2.5, 2, 1.5, 0.5, -0.5, -1, -0.5, 0.5, 1.5, 2.5,
                3, 2.5, 2, 1.5, 0, 1, 0.5, 0})};
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
    const bool hadStoredCurve = settings.contains(QStringLiteral("bandGains"))
                                || settings.contains(
                                    QStringLiteral("currentPresetId"));
    enabled_ = settings.value(QStringLiteral("enabled"), false).toBool();
    bypassed_ = settings.value(QStringLiteral("bypassed"), false).toBool();
    autoClipProtection_ =
        settings.value(QStringLiteral("autoClipProtection"), true).toBool();
    preampDb_ = storedDspValue(
        settings.value(QStringLiteral("preampDb"), 0.0).toDouble());
    const QVariant storedGains = settings.value(QStringLiteral("bandGains"));
    const int storedGainCount = storedGains.toList().size();
    const bool storedGainCountIsSupported =
        storedGainCount == static_cast<int>(kLegacyFrequencies.size())
        || storedGainCount == static_cast<int>(kSchemaTwoFrequencies.size())
        || storedGainCount == AG_EQUALIZER_BAND_COUNT;
    gains_ = gainsFromVariant(storedGains);
    const double storedRange =
        settings.value(QStringLiteral("gainRangeDb"), 12.0).toDouble();
    gainRangeDb_ = (storedRange == 6.0 || storedRange == 12.0
                    || storedRange == 18.0)
                       ? storedRange
                       : 12.0;
    const QString storedPrecision =
        settings.value(QStringLiteral("precisionMode"), QStringLiteral("high"))
            .toString();
    precisionMode_ = (storedPrecision == QStringLiteral("high")
                      || storedPrecision == QStringLiteral("medium")
                      || storedPrecision == QStringLiteral("low"))
                         ? storedPrecision
                         : QStringLiteral("high");
    currentPresetId_ =
        settings.value(QStringLiteral("currentPresetId"),
                       QStringLiteral("flat")).toString();
    const int count = settings.beginReadArray(QStringLiteral("customPresets"));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        Preset preset;
        preset.id = settings.value(QStringLiteral("id")).toString();
        preset.name = settings.value(QStringLiteral("name")).toString();
        preset.preampDb = storedDspValue(
            settings.value(QStringLiteral("preampDb"), 0.0).toDouble());
        preset.gains = gainsFromVariant(
            settings.value(QStringLiteral("bandGains")));
        if (!preset.id.isEmpty() && !preset.name.isEmpty()) {
            customPresets_.push_back(std::move(preset));
        }
    }
    settings.endArray();
    const int storedSchema =
        settings.value(QStringLiteral("schemaVersion"), 0).toInt();
    const int storedBandCount =
        settings.value(QStringLiteral("bandCount"), 0).toInt();
    const bool storedGainMetadataIsValid =
        storedGainCountIsSupported
        && (storedBandCount <= 0 || storedBandCount == storedGainCount)
        && (storedSchema != kSettingsSchemaVersion
            || storedGainCount == AG_EQUALIZER_BAND_COUNT)
        && (storedSchema != 2
            || storedGainCount == static_cast<int>(kSchemaTwoFrequencies.size()));
    const bool requiresMigration = storedSchema != kSettingsSchemaVersion
                                   || storedBandCount != AG_EQUALIZER_BAND_COUNT;
    const bool presetExists = findPreset(currentPresetId_).has_value();
    const bool selectedCustomPreset = std::any_of(
        customPresets_.cbegin(), customPresets_.cend(),
        [this](const Preset& preset) { return preset.id == currentPresetId_; });
    const bool migratedCurveIsFlat = currentPresetId_ == QStringLiteral("flat")
                                     && std::abs(preampDb_) <= 1.0e-9
                                     && std::all_of(
                                         gains_.cbegin(), gains_.cend(),
                                         [](const double gain) {
                                             return std::abs(gain) <= 1.0e-9;
                                         });
    // A retained identifier such as "rock" does not prove that the migrated
    // ten-band gains match the new reference curve. Preserve the interpolated
    // sound as a custom state instead of mislabelling it as a built-in preset.
    if ((!selectedCustomPreset && requiresMigration && hadStoredCurve
         && !migratedCurveIsFlat)
        || (!selectedCustomPreset && hadStoredCurve
            && !storedGainMetadataIsValid)
        || !presetExists) {
        currentPresetId_ = QStringLiteral("custom");
    }
    settings.endGroup();
    if (requiresMigration) {
        persist();
    }
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
    settings.setValue(QStringLiteral("gainRangeDb"), gainRangeDb_);
    settings.setValue(QStringLiteral("precisionMode"), precisionMode_);
    settings.setValue(QStringLiteral("currentPresetId"), currentPresetId_);
    settings.setValue(QStringLiteral("schemaVersion"), kSettingsSchemaVersion);
    settings.setValue(QStringLiteral("bandCount"), AG_EQUALIZER_BAND_COUNT);
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
        restoreSubmittedState();
        emit submissionFailed();
        return false;
    }
    ag_equalizer_settings settings{};
    const quint64 candidateRevision = revision_ + 1U;
    settings.revision = candidateRevision;
    settings.enabled = enabled_ ? 1 : 0;
    settings.bypassed = bypassed_ ? 1 : 0;
    settings.auto_clip_protection = autoClipProtection_ ? 1 : 0;
    settings.preamp_db = preampDb_;
    std::copy(gains_.begin(), gains_.end(), settings.band_gain_db);
    settings.q = agplayer::kGraphicEqDefaultQ;
    settings.transition_ms = 25.0;
    const ag_result result = submitFunction_(player_, &settings);
    if (result != AG_OK) {
        restoreSubmittedState();
        emit submissionFailed();
        return false;
    }
    revision_ = candidateRevision;
    rememberSubmittedState();
    refreshStatus();
    persist();
    emit responseCurveChanged();
    return true;
}

void EqualizerController::rememberSubmittedState()
{
    submittedState_.valid = true;
    submittedState_.enabled = enabled_;
    submittedState_.bypassed = bypassed_;
    submittedState_.autoClipProtection = autoClipProtection_;
    submittedState_.preampDb = preampDb_;
    submittedState_.gainRangeDb = gainRangeDb_;
    submittedState_.gains = gains_;
    submittedState_.currentPresetId = currentPresetId_;
}

void EqualizerController::synchronizeSubmittedMetadata()
{
    if (!submittedState_.valid) {
        return;
    }
    submittedState_.gainRangeDb = gainRangeDb_;
    submittedState_.currentPresetId = currentPresetId_;
}

void EqualizerController::restoreSubmittedState()
{
    if (!submittedState_.valid) {
        return;
    }

    const bool enabledChanged = enabled_ != submittedState_.enabled;
    const bool bypassedChanged = bypassed_ != submittedState_.bypassed;
    const bool protectionChanged =
        autoClipProtection_ != submittedState_.autoClipProtection;
    const bool preampChanged = !qFuzzyCompare(
        preampDb_ + 19.0, submittedState_.preampDb + 19.0);
    const bool rangeChanged = !qFuzzyCompare(
        gainRangeDb_ + 19.0, submittedState_.gainRangeDb + 19.0);
    const bool presetChanged =
        currentPresetId_ != submittedState_.currentPresetId;
    std::array<bool, AG_EQUALIZER_BAND_COUNT> changedBands{};
    bool gainsChanged = false;
    for (int index = 0; index < AG_EQUALIZER_BAND_COUNT; ++index) {
        const std::size_t slot = static_cast<std::size_t>(index);
        changedBands[slot] = !qFuzzyCompare(
            gains_[slot] + 19.0, submittedState_.gains[slot] + 19.0);
        gainsChanged = gainsChanged || changedBands[slot];
    }

    if (gainsChanged || rangeChanged) {
        beginResetModel();
    }
    enabled_ = submittedState_.enabled;
    bypassed_ = submittedState_.bypassed;
    autoClipProtection_ = submittedState_.autoClipProtection;
    preampDb_ = submittedState_.preampDb;
    gainRangeDb_ = submittedState_.gainRangeDb;
    gains_ = submittedState_.gains;
    currentPresetId_ = submittedState_.currentPresetId;
    if (gainsChanged || rangeChanged) {
        endResetModel();
    }

    if (enabledChanged) {
        emit this->enabledChanged();
    }
    if (bypassedChanged) {
        emit this->bypassedChanged();
    }
    if (protectionChanged) {
        emit autoClipProtectionChanged();
    }
    if (preampChanged) {
        emit preampDbChanged();
    }
    if (rangeChanged) {
        emit gainRangeDbChanged();
    }
    for (int index = 0; index < AG_EQUALIZER_BAND_COUNT; ++index) {
        const std::size_t slot = static_cast<std::size_t>(index);
        if (changedBands[slot]) {
            emit bandGainChanged(index, gains_[slot]);
        }
    }
    if (presetChanged) {
        emit currentPresetChanged();
    }
    if (enabledChanged || bypassedChanged || protectionChanged || preampChanged
        || gainsChanged) {
        emit responseCurveChanged();
    }
}

void EqualizerController::setCurrentPresetId(const QString& id)
{
    if (currentPresetId_ == id) {
        return;
    }
    currentPresetId_ = id;
    emit currentPresetChanged();
}
