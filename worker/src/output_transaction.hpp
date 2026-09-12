#pragma once

#include "cancellation_token.hpp"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QLockFile;

namespace agplayer::separation {

struct OutputPlan {
    QString outputDirectory;
    QString baseName;
    QString extension;
    QStringList stems;
    QString modelName;
    QStringList stemLabels;
    QString directoryName;
};

struct TransactionResult {
    bool ok = false;
    QString code;
    QString message;
    QStringList outputs;
    QString causeCode;
    QString causeMessage;
};

class NativeOutputFileOps {
public:
    virtual ~NativeOutputFileOps() = default;
    virtual bool renameDirectory(const QString& source,
                                 const QString& destination);
    virtual bool removeFile(const QString& path);
    virtual bool removeEmptyDirectory(const QString& path);
};

class OutputTransaction final {
public:
    explicit OutputTransaction(
        OutputPlan plan,
        std::shared_ptr<NativeOutputFileOps> operations =
            std::make_shared<NativeOutputFileOps>());
    ~OutputTransaction();

    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;

    [[nodiscard]] TransactionResult begin();
    [[nodiscard]] QString temporaryPath(const QString& stem) const;
    [[nodiscard]] QString temporaryDirectory() const;
    [[nodiscard]] TransactionResult
    commit(const std::function<bool(const QString&)>& verifier,
           const CancellationToken& cancellation);
    [[nodiscard]] TransactionResult cancel();

private:
    [[nodiscard]] TransactionResult reject(const QString& code,
                                           const QString& message);
    [[nodiscard]] TransactionResult rollback();
    [[nodiscard]] TransactionResult recoverReservedJobs();
    [[nodiscard]] TransactionResult cleanupReservedJob(
        const QString& reservationPath);

    OutputPlan plan_;
    std::shared_ptr<NativeOutputFileOps> operations_;
    std::unique_ptr<QLockFile> lock_;
    QString reservationPath_;
    QString temporaryDirectory_;
    QHash<QString, QString> temporaryPaths_;
    bool active_ = false;
};

} // namespace agplayer::separation
