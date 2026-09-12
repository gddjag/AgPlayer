#include "filename_processor.hpp"

#include "audio_file_discovery.hpp"
#include "filename_transform_engine.hpp"
#include "filename_validator.hpp"
#include "library_model.hpp"
#include "rename_plan.hpp"
#include "rename_transaction.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
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

agplayer::qt::FilenameRuleSet typedRules(const QVariantMap& rules)
{
    agplayer::qt::FilenameRuleSet typed;
    typed.prefix = rules.value(QStringLiteral("prefix")).toString();
    typed.suffix = rules.value(QStringLiteral("suffix")).toString();
    typed.removePrefix = rules.value(QStringLiteral("removePrefix")).toString();
    typed.removeSuffix = rules.value(QStringLiteral("removeSuffix")).toString();
    typed.replaceSpaces = rules.value(QStringLiteral("replaceSpaces"), false).toBool();
    typed.spaceReplacement = rules.value(QStringLiteral("spaceReplacement"), QStringLiteral("_")).toString();
    typed.autoNumber = rules.value(QStringLiteral("autoNumber"), false).toBool();
    typed.numberStart = rules.value(QStringLiteral("numberStart"), 1).toInt();
    typed.numberDigits = rules.value(QStringLiteral("numberDigits"), 2).toInt();
    typed.numberSeparator = rules.value(QStringLiteral("numberSeparator"), QStringLiteral("_")).toString();
    typed.preserveExtension = rules.value(QStringLiteral("preserveExtension"), true).toBool();
    typed.removePrefixWhenEmpty = rules.value(QStringLiteral("removePrefixWhenEmpty"), true).toBool();
    typed.removeSuffixWhenEmpty = rules.value(QStringLiteral("removeSuffixWhenEmpty"), true).toBool();
    typed.removeSequenceWhenEmpty = rules.value(QStringLiteral("removeSequenceWhenEmpty"), false).toBool();
    typed.removeSequenceAtStart = rules.value(QStringLiteral("removeSequenceAtStart"), false).toBool();
    typed.removeSequenceAtEnd = rules.value(QStringLiteral("removeSequenceAtEnd"), false).toBool();
    // Adding a sequence and deleting one are contradictory operations.  Keep
    // the explicitly requested new number and normalize removal away before
    // preview and execution share the same rule snapshot.
    if (typed.autoNumber) {
        typed.removeSequenceWhenEmpty = false;
        typed.removeSequenceAtStart = false;
        typed.removeSequenceAtEnd = false;
    }
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
    return typed;
}

agplayer::qt::ConflictPolicy typedConflictPolicy(const QString& policy)
{
    if (policy == QLatin1String("skip")) return agplayer::qt::ConflictPolicy::Skip;
    if (policy == QLatin1String("overwrite")) return agplayer::qt::ConflictPolicy::Overwrite;
    if (policy == QLatin1String("stop")) return agplayer::qt::ConflictPolicy::StopBatch;
    return agplayer::qt::ConflictPolicy::AutoNumber;
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
        const RenameResult result = operationWatcher_->future().result();
        if (result.transaction.committed) {
            agplayer::qt::RenameTransaction::discardUndo(
                result.transaction.undoRecord);
        }
    }
    if (undoWatcher_ != nullptr) {
        undoWatcher_->future().waitForFinished();
    }
    agplayer::qt::RenameTransaction::discardUndo(lastUndoRecord_);
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
    return !lastUndoRecord_.plan.items.isEmpty();
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

bool FilenameProcessor::discardLastUndo()
{
    if (!canUndo()) {
        return true;
    }
    if (!agplayer::qt::RenameTransaction::discardUndo(lastUndoRecord_)) {
        emit errorOccurred(tr("上次覆盖备份已变更或无法删除，已保留撤销记录"));
        return false;
    }
    lastUndoRecord_ = {};
    emit canUndoChanged();
    return true;
}

void FilenameProcessor::loadFiles(const QList<QUrl>& urls)
{
    if (busy()) {
        return;
    }
    if (!discardLastUndo()) {
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
        setBusy(false);
        setProgress(1.0);
        emit fileCountChanged();
        emit entriesChanged();
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
    return agplayer::qt::FilenameTransformEngine::transform(
        original, typedRules(rules), ordinal);
}

QVariantList FilenameProcessor::preview(const QVariantMap& rules,
                                        const QList<int>& indices,
                                        const QString& conflictPolicy) const
{
    QVariantList result;
    const QList<int> targets = normalizedTargets(entries_.size(), indices);
    QList<agplayer::qt::RenameSource> sources;
    sources.reserve(targets.size());
    for (const int index : targets) {
        const Entry& entry = entries_.at(index);
        sources.append({index, entry.path, entry.sha256, {}, QFileInfo(entry.path).size()});
    }
    const agplayer::qt::RenamePlan plan = agplayer::qt::RenamePlanner::build(
        sources, typedRules(rules), typedConflictPolicy(conflictPolicy));
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
    if (!discardLastUndo()) {
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
        if (result.transaction.committed) {
            lastUndoRecord_ = result.transaction.undoRecord;
        }
        if (result.transaction.committed && !library_.isNull()) {
            QHash<QString, QString> paths;
            for (const auto& item : result.transaction.undoRecord.plan.items) {
                if (item.action != agplayer::qt::RenameAction::Rename
                    && item.action != agplayer::qt::RenameAction::Overwrite) {
                    continue;
                }
                for (const TrackRecord& track : library_->tracks()) {
                    if (agplayer::qt::FilenameValidator::collisionKey(track.path)
                        == agplayer::qt::FilenameValidator::collisionKey(item.sourcePath)) {
                        paths.insert(track.trackId, item.targetPath);
                        break;
                    }
                }
            }
            bool libraryUpdateOk = true;
            if (!paths.isEmpty()) {
                for (auto it = paths.cbegin(); it != paths.cend(); ++it) {
                    if (!library_->updateTrackPath(it.key(), it.value())) {
                        libraryUpdateOk = false;
                        break;
                    }
                }
            }
            if (!paths.isEmpty() && !libraryUpdateOk) {
                // The filesystem transaction is already committed. Surface the
                // recovery action without falsifying its disk outcome.
                emit errorOccurred(tr("文件已重命名，但播放器资料库路径同步失败；请重新扫描资料库"));
            }
        }
        setBusy(false);
        setProgress(1.0);
        emit entriesChanged();
        if (canUndo()) {
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
        QList<agplayer::qt::RenameSource> sources;
        sources.reserve(targets.size());
        for (const int index : targets) {
            const Entry& entry = snapshot.at(index);
            sources.append({index, entry.path, entry.sha256, {},
                            QFileInfo(entry.path).size()});
        }
        const agplayer::qt::RenamePlan plan = agplayer::qt::RenamePlanner::build(
            sources, typedRules(rules), typedConflictPolicy(conflictPolicy));
        for (const auto& item : plan.items) {
            if (item.action == agplayer::qt::RenameAction::NoOp
                || item.action == agplayer::qt::RenameAction::Skip) {
                ++result.skipped;
                if (item.severity == agplayer::qt::RenameSeverity::Error) {
                    ++result.failure;
                }
            }
        }
        if (!plan.executable) {
            result.error = tr("当前重命名计划包含错误，未执行任何文件操作");
            return result;
        }
        result.transaction = agplayer::qt::RenameTransaction().execute(
            plan, &cancelFlag_);
        if (!result.transaction.committed) {
            result.failure = qMax(1, targets.size() - result.skipped);
            result.error = result.transaction.errorText;
            return result;
        }
        for (const auto& item : plan.items) {
            if (item.action != agplayer::qt::RenameAction::Rename
                && item.action != agplayer::qt::RenameAction::Overwrite) {
                continue;
            }
            result.entries[item.itemId].path = item.targetPath;
            result.entries[item.itemId].fileName = item.proposedFileName;
            ++result.success;
        }
        return result;
    }));
}

void FilenameProcessor::undoLast()
{
    if (busy() || !canUndo()) {
        return;
    }
    const agplayer::qt::RenameUndoRecord undoRecord = lastUndoRecord_;
    setBusy(true);
    setProgress(0.0);
    auto* watcher =
        new QFutureWatcher<agplayer::qt::RenameTransactionResult>(this);
    undoWatcher_ = watcher;
    connect(watcher,
            &QFutureWatcher<agplayer::qt::RenameTransactionResult>::finished,
            this,
            [this, watcher, undoRecord] {
        if (undoWatcher_ == watcher) {
            undoWatcher_.clear();
        }
        const agplayer::qt::RenameTransactionResult result = watcher->result();
        if (!result.committed) {
            setBusy(false);
            setProgress(1.0);
            if (!result.errorText.isEmpty()) {
                emit errorOccurred(result.errorText);
            }
            emit undoCompleted(0, 1);
            watcher->deleteLater();
            return;
        }

        int restoredCount = 0;
        for (const auto& item : undoRecord.plan.items) {
            if (item.action != agplayer::qt::RenameAction::Rename
                && item.action != agplayer::qt::RenameAction::Overwrite) {
                continue;
            }
            if (item.itemId >= 0 && item.itemId < entries_.size()) {
                entries_[item.itemId].path = item.sourcePath;
                entries_[item.itemId].fileName = QFileInfo(item.sourcePath).fileName();
            }
            ++restoredCount;
        }
        if (!library_.isNull()) {
            QHash<QString, QString> restoredPaths;
            const QList<TrackRecord> tracks = library_->tracks();
            for (const auto& item : undoRecord.plan.items) {
                if (item.action != agplayer::qt::RenameAction::Rename
                    && item.action != agplayer::qt::RenameAction::Overwrite) {
                    continue;
                }
                for (const TrackRecord& track : tracks) {
                    if (pathKey(track.path) == pathKey(item.targetPath)) {
                        restoredPaths.insert(track.trackId, item.sourcePath);
                        break;
                    }
                }
            }
            bool libraryUpdateOk = true;
            if (!restoredPaths.isEmpty()) {
                for (auto it = restoredPaths.cbegin(); it != restoredPaths.cend(); ++it) {
                    if (!library_->updateTrackPath(it.key(), it.value())) {
                        libraryUpdateOk = false;
                        break;
                    }
                }
            }
            if (!restoredPaths.isEmpty() && !libraryUpdateOk) {
                emit errorOccurred(tr("文件已恢复，但曲库路径同步失败。"));
            }
        }
        lastUndoRecord_ = {};
        setBusy(false);
        setProgress(1.0);
        emit entriesChanged();
        emit canUndoChanged();
        emit undoCompleted(restoredCount, 0);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([undoRecord] {
        return agplayer::qt::RenameTransaction().undo(undoRecord);
    }));
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
    if (!discardLastUndo()) {
        return;
    }
    entries_.clear();
    emit fileCountChanged();
    emit entriesChanged();
}
