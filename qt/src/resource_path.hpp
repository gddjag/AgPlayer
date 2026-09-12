#pragma once

#include <QString>
#include <Qt>

namespace agplayer::qt {

Qt::CaseSensitivity resourcePathCaseSensitivity() noexcept;
QString resourcePathIdentity(const QString& path);
bool resourcePathsEqual(const QString& left, const QString& right);
bool resourcePathIsWithin(const QString& candidate, const QString& root);
bool resourcePathIdentityIsWithin(const QString& candidateIdentity,
                                  const QString& rootIdentity);

} // namespace agplayer::qt
