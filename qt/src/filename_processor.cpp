#include "filename_processor.hpp"

#include "audio_file_discovery.hpp"
#include "filename_transform_engine.hpp"
#include "filename_validator.hpp"
#include "library_model.hpp"
#include "rename_plan.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <functional>
#include <memory>

namespace {

QString pathKey(const QString& path)
{
    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return normalized.toCaseFolded();
#else
    return normalized;
#endif
}

QString uniqueStagingPath(const QString& source)
{
    const QFileInfo info(source);
    for (;;) {
        const QString candidate = info.dir().filePath(
            QStringLiteral(".%1.agplayer-%2.tmp")
                .arg(info.fileName(),
                     QUuid::createUuid().toString(QUuid::Id128)));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

QString contentHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString sanitizeComponent(QString value)
{
    const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character.unicode() < 0x20 || invalid.contains(character)) {
            value[index] = QLatin1Char('_');
        }
    }
    while (value.endsWith(QLatin1Char(' '))
           || value.endsWith(QLatin1Char('.'))) {
        value.chop(1);
    }
    static const QRegularExpression reservedDeviceName(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedDeviceName.match(value).hasMatch()) {
        value.prepend(QLatin1Char('_'));
    }
    return value.isEmpty() ? QStringLiteral("_") : value;
}

QString applyCase(QString value, const QString& mode)
{
    if (mode == QLatin1String("lower")) {
        return value.toLower();
    }
    if (mode == QLatin1String("upper")) {
        return value.toUpper();
    }
    if (mode == QLatin1String("title")) {
        bool capitalize = true;
        for (qsizetype index = 0; index < value.size(); ++index) {
            if (value.at(index).isLetter()) {
                value[index] = capitalize ? value.at(index).toUpper()
                                          : value.at(index).toLower();
                capitalize = false;
            } else {
                capitalize = value.at(index).isSpace()
                    || value.at(index) == QLatin1Char('_')
                    || value.at(index) == QLatin1Char('-');
            }
        }
    }
    return value;
}

QString collisionFreePath(const QString& desired,
                          const QSet<QString>& reserved)
{
    if (!QFileInfo::exists(desired) && !reserved.contains(pathKey(desired))) {
        return desired;
    }
    const QFileInfo info(desired);
    const QString suffix = info.suffix().isEmpty()
        ? QString() : QStringLiteral(".") + info.suffix();
    for (int number = 2; number <= 9999; ++number) {
        const QString candidate = info.dir().filePath(
            info.completeBaseName() + QStringLiteral("_")
            + QString::number(number) + suffix);
        if (!QFileInfo::exists(candidate)
            && !reserved.contains(pathKey(candidate))) {
            return candidate;
        }
    }
    return {};
}

} // namespace

FilenameProcessor::FilenameProcessor(QObject* parent)
    : QObject(parent)
{
}

FilenameProcessor::~FilenameProcessor()
{
    cancel();
    if (discoveryWatcher_ != nullptr) {
        discoveryWatcher_->future().waitForFinished();
    }
    if (loadWatcher_ != nullptr) {
        loadWatcher_->future().waitForFinished();
    }
    if (operationWatcher_ != nullptr) {
        operationWatcher_->future().waitForFinished();
    }
}

void FilenameProcessor::setLibraryModel(LibraryModel* library)
{
    library_ = library;
}

double FilenameProcessor::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool FilenameProcessor::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

int FilenameProcessor::fileCount() const noexcept
{
    return static_cast<int>(entries_.size());
}

bool FilenameProcessor::canUndo() const noexcept
{
    return !lastTransaction_.isEmpty();
}

QVariantList FilenameProcessor::files() const
{
    QVariantList result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        const QFileInfo info(entry.path);
        result.append(QVariantMap{{QStringLiteral("path"), entry.path},
                                  {QStringLiteral("fileName"), entry.fileName},
                                  {QStringLiteral("directory"), info.absolutePath()},
                                  {QStringLiteral("extension"), info.suffix().toLower()},
                                  {QStringLiteral("fileSize"), info.size()},
                                  {QStringLiteral("sha256"), entry.sha256}});
    }
    return result;
}

void FilenameProcessor::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void FilenameProcessor::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void FilenameProcessor::loadFiles(const QList<QUrl>& urls)
{
    if (busy()) {
        return;
    }
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);
    auto* discovery = new QFutureWatcher<QList<QUrl>>(this);
    discoveryWatcher_ = discovery;
    connect(discovery, &QFutureWatcher<QList<QUrl>>::finished, this,
            [this, discovery] {
        if (discoveryWatcher_ == discovery) {
            discoveryWatcher_.clear();
        }
        const QList<QUrl> expanded = discovery->result();
        discovery->deleteLater();
        if (expanded.isEmpty()) {
            entries_.clear();
            setBusy(false);
            setProgress(1.0);
            emit fileCountChanged();
            emit entriesChanged();
            emit entriesLoaded();
            emit errorOccurred(tr("未找到支持的音频文件"));
            return;
        }
        startLoad(expanded);
    });
    discovery->setFuture(agplayer::qt::expandAudioUrlsAsync(urls));
}

void FilenameProcessor::startLoad(QList<QUrl> expanded)
{
    auto* watcher = new QFutureWatcher<QList<Entry>>(this);
    loadWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<QList<Entry>>::finished, this,
            [this, watcher] {
        loadWatcher_.clear();
        entries_ = watcher->result();
        const bool undoChanged = !lastTransaction_.isEmpty();
        lastTransaction_.clear();
        setBusy(false);
        setProgress(1.0);
        emit fileCountChanged();
        emit entriesChanged();
        if (undoChanged) {
            emit canUndoChanged();
        }
        emit entriesLoaded();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([expanded = std::move(expanded), this] {
        QList<Entry> result;
        result.reserve(expanded.size());
        for (int index = 0; index < expanded.size(); ++index) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                break;
            }
            const QString path = expanded.at(index).toLocalFile();
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                result.append({path, QFileInfo(path).fileName(), contentHash(path)});
            }
            progress_.store(expanded.isEmpty() ? 1.0
                                               : static_cast<double>(index + 1)
                                                     / expanded.size(),
                            std::memory_order_release);
        }
        return result;
    }));
}

QVariantMap FilenameProcessor::entryAt(int index) const
{
    if (index < 0 || index >= entries_.size()) {
        return {};
    }
    const Entry& entry = entries_.at(index);
    const QFileInfo info(entry.path);
    return {{QStringLiteral("path"), entry.path},
            {QStringLiteral("fileName"), entry.fileName},
            {QStringLiteral("directory"), info.absolutePath()},
            {QStringLiteral("extension"), info.suffix().toLower()},
            {QStringLiteral("fileSize"), info.size()},
            {QStringLiteral("sha256"), entry.sha256}};
}

QList<int> FilenameProcessor::normalizedTargets(int count,
                                                const QList<int>& indices)
{
    QList<int> result = indices;
    if (result.isEmpty()) {
        result.reserve(count);
        for (int index = 0; index < count; ++index) {
            result.append(index);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    result.erase(std::remove_if(result.begin(), result.end(),
                                [count](int index) {
        return index < 0 || index >= count;
    }), result.end());
    return result;
}

void FilenameProcessor::removeFiles(const QList<int>& indices)
{
    if (busy() || indices.isEmpty()) {
        return;
    }
    QList<int> targets = normalizedTargets(entries_.size(), indices);
    std::sort(targets.begin(), targets.end(), std::greater<int>());
    for (const int index : targets) {
        entries_.removeAt(index);
    }
    if (!targets.isEmpty()) {
        emit fileCountChanged();
        emit entriesChanged();
    }
}

QString FilenameProcessor::proposedName(const QString& original,
                                        const QVariantMap& rules,
                                        int ordinal)
{
    agplayer::qt::FilenameRuleSet typed;
    typed.prefix = rules.value(QStringLiteral("prefix")).toString();
    typed.suffix = rules.value(QStringLiteral("suffix")).toString();
    typed.replaceSpaces = rules.value(QStringLiteral("replaceSpaces"), false).toBool();
    typed.spaceReplacement = rules.value(QStringLiteral("spaceReplacement"), QStringLiteral("_")).toString();
    typed.autoNumber = rules.value(QStringLiteral("autoNumber"), false).toBool();
    typed.numberStart = rules.value(QStringLiteral("numberStart"), 1).toInt();
    typed.numberDigits = rules.value(QStringLiteral("numberDigits"), 2).toInt();
    typed.numberSeparator = rules.value(QStringLiteral("numberSeparator"), QStringLiteral("_")).toString();
    typed.preserveExtension = rules.value(QStringLiteral("preserveExtension"), true).toBool();
    const QString mode = rules.value(QStringLiteral("caseMode"), QStringLiteral("keep")).toString();
    typed.caseRule = mode == QLatin1String("lower") ? agplayer::qt::CaseRule::Lower
        : mode == QLatin1String("upper") ? agplayer::qt::CaseRule::Upper
        : mode == QLatin1String("title") ? agplayer::qt::CaseRule::Title
        : agplayer::qt::CaseRule::Keep;
    const QString position = rules.value(QStringLiteral("numberPosition"), QStringLiteral("afterSuffix")).toString();
    typed.numberPosition = position == QLatin1String("beginning") ? agplayer::qt::NumberPosition::Beginning
        : position == QLatin1String("beforeSuffix") ? agplayer::qt::NumberPosition::BeforeSuffix
        : position == QLatin1String("afterSuffix") ? agplayer::qt::NumberPosition::AfterSuffix
        : agplayer::qt::NumberPosition::AfterPrefix;
    return agplayer::qt::FilenameTransformEngine::transform(original, typed, ordinal);
}

QVariantList FilenameProcessor::preview(const QVariantMap& rules,
                                        const QList<int>& indices) const
{
    QVariantList result;
    const QList<int> targets = normalizedTargets(entries_.size(), indices);
    QList<agplayer::qt::RenameSource> sources;
    sources.reserve(targets.size());
    for (const int index : targets) {
        const Entry& entry = entries_.at(index);
        sources.append({index, entry.path, entry.sha256, {}, QFileInfo(entry.path).size()});
    }
    agplayer::qt::FilenameRuleSet typed;
    typed.prefix = rules.value(QStringLiteral("prefix")).toString();
    typed.suffix = rules.value(QStringLiteral("suffix")).toString();
    typed.replaceSpaces = rules.value(QStringLiteral("replaceSpaces"), false).toBool();
    typed.spaceReplacement = rules.value(QStringLiteral("spaceReplacement"), QStringLiteral("_")).toString();
    typed.autoNumber = rules.value(QStringLiteral("autoNumber"), false).toBool();
    typed.numberStart = rules.value(QStringLiteral("numberStart"), 1).toInt();
    typed.numberDigits = rules.value(QStringLiteral("numberDigits"), 2).toInt();
    typed.numberSeparator = rules.value(QStringLiteral("numberSeparator"), QStringLiteral("_")).toString();
    const QString mode = rules.value(QStringLiteral("caseMode"), QStringLiteral("keep")).toString();
    typed.caseRule = mode == QLatin1String("lower") ? agplayer::qt::CaseRule::Lower
        : mode == QLatin1String("upper") ? agplayer::qt::CaseRule::Upper
        : mode == QLatin1String("title") ? agplayer::qt::CaseRule::Title
        : agplayer::qt::CaseRule::Keep;
    const QString position = rules.value(QStringLiteral("numberPosition"), QStringLiteral("afterSuffix")).toString();
    typed.numberPosition = position == QLatin1String("beginning") ? agplayer::qt::NumberPosition::Beginning
        : position == QLatin1String("afterPrefix") ? agplayer::qt::NumberPosition::AfterPrefix
        : position == QLatin1String("beforeSuffix") ? agplayer::qt::NumberPosition::BeforeSuffix
        : agplayer::qt::NumberPosition::AfterSuffix;
    const agplayer::qt::RenamePlan plan = agplayer::qt::RenamePlanner::build(
        sources, typed, agplayer::qt::ConflictPolicy::AutoNumber);
    result.reserve(plan.items.size());
    for (const auto& item : plan.items) {
        const bool conflict = item.severity != agplayer::qt::RenameSeverity::Ready;
        result.append(QVariantMap{{QStringLiteral("index"), item.itemId},
                                  {QStringLiteral("original"), QFileInfo(item.sourcePath).fileName()},
                                  {QStringLiteral("preview"), item.proposedFileName},
                                  {QStringLiteral("conflict"), conflict},
                                  {QStringLiteral("severity"), static_cast<int>(item.severity)},
                                  {QStringLiteral("reason"), item.reasonText}});
    }
    return result;
}

void FilenameProcessor::apply(const QVariantMap& rules,
                              const QList<int>& indices,
                              const QString& conflictPolicy)
{
    if (busy()) {
        return;
    }
    const QList<int> targets = normalizedTargets(entries_.size(), indices);
    if (targets.isEmpty()) {
        emit renameApplied(0, 0, 0);
        return;
    }
    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);
    const QList<Entry> snapshot = entries_;
    auto* watcher = new QFutureWatcher<RenameResult>(this);
    operationWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<RenameResult>::finished, this,
            [this, watcher] {
        operationWatcher_.clear();
        const RenameResult result = watcher->result();
        entries_ = result.entries;
        const bool undoChanged = lastTransaction_.isEmpty()
            != result.committed.isEmpty();
        lastTransaction_ = result.committed;
        if (!result.committed.isEmpty() && !library_.isNull()) {
            QHash<QString, QString> paths;
            for (const RenameStep& step : result.committed) {
                for (const TrackRecord& track : library_->tracks()) {
                    if (agplayer::qt::FilenameValidator::collisionKey(track.path)
                        == agplayer::qt::FilenameValidator::collisionKey(step.source)) {
                        paths.insert(track.trackId, step.target);
                        break;
                    }
                }
            }
            if (!paths.isEmpty() && !library_->updateTrackPaths(paths)) {
                // The filesystem transaction is already committed. Surface the
                // recovery action without falsifying its disk outcome.
                emit errorOccurred(tr("文件已重命名，但播放器资料库路径同步失败；请重新扫描资料库"));
            }
        }
        setBusy(false);
        setProgress(1.0);
        emit entriesChanged();
        if (undoChanged) {
            emit canUndoChanged();
        }
        if (!result.error.isEmpty()) {
            emit errorOccurred(result.error);
        }
        emit renameApplied(result.success, result.skipped, result.failure);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(
        [snapshot, targets, rules, conflictPolicy, this] {
        RenameResult result;
        result.entries = snapshot;
        QList<RenameStep> plan;
        QSet<QString> reserved;
        QSet<QString> sourcePaths;
        for (const int index : targets) {
            sourcePaths.insert(pathKey(snapshot.at(index).path));
        }
        for (int ordinal = 0; ordinal < targets.size(); ++ordinal) {
            const int index = targets.at(ordinal);
            const Entry& entry = snapshot.at(index);
            QString target = QFileInfo(entry.path).dir().filePath(
                proposedName(entry.fileName, rules, ordinal));
            if (QDir::cleanPath(target) == QDir::cleanPath(entry.path)) {
                ++result.skipped;
                continue;
            }
            const QString targetKey = pathKey(target);
            const bool conflict =
                (QFileInfo::exists(target) && !sourcePaths.contains(targetKey))
                || reserved.contains(targetKey);
            if (conflict && conflictPolicy == QLatin1String("stop")) {
                result.failure = targets.size() - result.skipped;
                result.error = tr("文件名冲突：%1")
                    .arg(QFileInfo(target).fileName());
                return result;
            }
            if (conflict && conflictPolicy == QLatin1String("skip")) {
                ++result.skipped;
                continue;
            }
            if (conflict) {
                target = collisionFreePath(target, reserved);
            }
            if (target.isEmpty()) {
                ++result.failure;
                continue;
            }
            reserved.insert(pathKey(target));
            plan.append({index, entry.path, target, {}, entry.sha256});
        }

        int stagedCount = 0;
        int finalizedCount = 0;
        const auto rollbackPlan = [&plan, &stagedCount, &finalizedCount]() {
            bool complete = true;
            for (int index = finalizedCount - 1; index >= 0; --index) {
                const RenameStep& step = plan.at(index);
                if (!QFile::rename(step.target, step.staging)) {
                    complete = false;
                }
            }
            for (int index = stagedCount - 1; index >= 0; --index) {
                const RenameStep& step = plan.at(index);
                if (QFileInfo::exists(step.staging)
                    && !QFile::rename(step.staging, step.source)) {
                    complete = false;
                }
            }
            return complete;
        };

        for (int stepIndex = 0; stepIndex < plan.size(); ++stepIndex) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("重命名已取消")
                    : tr("重命名已取消，但回滚未完整完成");
                return result;
            }
            RenameStep& step = plan[stepIndex];
            if (step.sha256.isEmpty() || contentHash(step.source) != step.sha256) {
                ++result.failure;
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("文件内容已变更：%1").arg(QFileInfo(step.source).fileName())
                    : tr("文件内容已变更且回滚未完整完成：%1")
                          .arg(QFileInfo(step.source).fileName());
                return result;
            }
            step.staging = uniqueStagingPath(step.source);
            if (!QFile::rename(step.source, step.staging)) {
                ++result.failure;
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("重命名失败：%1")
                          .arg(QFileInfo(step.source).fileName())
                    : tr("重命名失败：%1；回滚未完整完成")
                          .arg(QFileInfo(step.source).fileName());
                return result;
            }
            ++stagedCount;
            progress_.store(static_cast<double>(stagedCount)
                                / qMax(1, plan.size() * 2),
                            std::memory_order_release);
        }

        for (int stepIndex = 0; stepIndex < plan.size(); ++stepIndex) {
            if (cancelFlag_.load(std::memory_order_acquire)) {
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("重命名已取消")
                    : tr("重命名已取消，但回滚未完整完成");
                return result;
            }
            const RenameStep& step = plan.at(stepIndex);
            if (!QFile::rename(step.staging, step.target)) {
                ++result.failure;
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("重命名失败：%1")
                          .arg(QFileInfo(step.source).fileName())
                    : tr("重命名失败：%1；回滚未完整完成")
                          .arg(QFileInfo(step.source).fileName());
                return result;
            }
            ++finalizedCount;
            if (contentHash(step.target) != step.sha256) {
                ++result.failure;
                const bool rolledBack = rollbackPlan();
                result.error = rolledBack
                    ? tr("重命名后内容校验失败：%1")
                          .arg(QFileInfo(step.target).fileName())
                    : tr("重命名后内容校验失败且回滚未完整完成：%1")
                          .arg(QFileInfo(step.target).fileName());
                return result;
            }
            result.entries[step.index].path = step.target;
            result.entries[step.index].fileName =
                QFileInfo(step.target).fileName();
            progress_.store(static_cast<double>(plan.size() + finalizedCount)
                                / qMax(1, plan.size() * 2),
                            std::memory_order_release);
        }
        result.committed = plan;
        result.success = plan.size();
        return result;
    }));
}

void FilenameProcessor::undoLast()
{
    if (busy() || lastTransaction_.isEmpty()) {
        return;
    }
    QList<RenameStep> undoPlan = lastTransaction_;
    QSet<QString> targetPaths;
    for (const RenameStep& step : undoPlan) {
        targetPaths.insert(pathKey(step.target));
    }
    for (const RenameStep& step : undoPlan) {
        if (QFileInfo::exists(step.source)
            && !targetPaths.contains(pathKey(step.source))) {
            emit undoCompleted(0, 1);
            return;
        }
    }

    int stagedCount = 0;
    int restoredCount = 0;
    const auto rollbackUndo = [&undoPlan, &stagedCount, &restoredCount]() {
        bool complete = true;
        for (int index = restoredCount - 1; index >= 0; --index) {
            const RenameStep& step = undoPlan.at(index);
            if (!QFile::rename(step.source, step.staging)) {
                complete = false;
            }
        }
        for (int index = stagedCount - 1; index >= 0; --index) {
            const RenameStep& step = undoPlan.at(index);
            if (QFileInfo::exists(step.staging)
                && !QFile::rename(step.staging, step.target)) {
                complete = false;
            }
        }
        return complete;
    };

    for (int index = 0; index < undoPlan.size(); ++index) {
        RenameStep& step = undoPlan[index];
        step.staging = uniqueStagingPath(step.target);
        if (!QFile::rename(step.target, step.staging)) {
            rollbackUndo();
            emit undoCompleted(0, 1);
            return;
        }
        ++stagedCount;
    }
    for (int index = 0; index < undoPlan.size(); ++index) {
        const RenameStep& step = undoPlan.at(index);
        if (!QFile::rename(step.staging, step.source)) {
            rollbackUndo();
            emit undoCompleted(0, 1);
            return;
        }
        ++restoredCount;
        entries_[step.index].path = step.source;
        entries_[step.index].fileName = QFileInfo(step.source).fileName();
    }
    lastTransaction_.clear();
    emit entriesChanged();
    emit canUndoChanged();
    emit undoCompleted(undoPlan.size(), 0);
}

void FilenameProcessor::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
}

void FilenameProcessor::clear()
{
    if (busy()) {
        return;
    }
    const bool hadUndo = !lastTransaction_.isEmpty();
    entries_.clear();
    lastTransaction_.clear();
    emit fileCountChanged();
    emit entriesChanged();
    if (hadUndo) {
        emit canUndoChanged();
    }
}
