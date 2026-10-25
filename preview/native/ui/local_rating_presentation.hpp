#pragma once
#include "library_model.hpp"
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <memory>

namespace biglybtc::ui {
struct LocalRatingRequest {
  quint64 session=0, request=0, revision=0;
  Target target;
  bool operator==(const LocalRatingRequest&) const = default;
};
struct LocalRatingValues {
  qint32 score=0;
  QString comment;
  QByteArray attributesRevision;
  bool invalidScoreText=false, recoveredBackup=false;
};
struct LocalRatingPacket {
  LocalRatingRequest identity;
  std::shared_ptr<const LocalRatingValues> snapshot;
  QString failure;
};
using LocalRatingPresentation=std::shared_ptr<const LocalRatingPacket>;
struct LocalRatingSubmission {
  LocalRatingRequest identity;
  QByteArray attributesRevision;
  qint32 score=0;
  QString comment;
};
struct LocalRatingSavePacket {
  LocalRatingRequest identity;
  bool saved=false;
  QString failure;
};
using LocalRatingSavePresentation=std::shared_ptr<const LocalRatingSavePacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::LocalRatingRequest)
Q_DECLARE_METATYPE(biglybtc::ui::LocalRatingSubmission)
Q_DECLARE_METATYPE(biglybtc::ui::LocalRatingPresentation)
Q_DECLARE_METATYPE(biglybtc::ui::LocalRatingSavePresentation)
