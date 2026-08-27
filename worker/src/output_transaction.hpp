#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <atomic>

namespace agplayer::separation {

struct OutputPlan {
    QString outputDirectory;
    QString baseName;
    QString extension;
    QStringList stems;
};

struct TransactionResult {
    bool ok = false;
    QString code;
    QString message;
    QStringList outputs;
};

class NativeOutputFileOps {
public:
    virtual ~NativeOutputFileOps() = default;
    virtual bool renameFile(const QString& source, const QString& destination);
    virtual bool removeFile(const QString& path);
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
           const std::atomic_bool& cancelled);
    void cancel();

private:
    [[nodiscard]] TransactionResult reject(const QString& code,
                                           const QString& message);
    [[nodiscard]] bool rollback();

    OutputPlan plan_;
    std::shared_ptr<NativeOutputFileOps> operations_;
    QString temporaryDirectory_;
    QHash<QString, QString> temporaryPaths_;
    QHash<QString, QString> finalPaths_;
    QStringList committedPaths_;
    bool active_ = false;
};

} // namespace agplayer::separation
