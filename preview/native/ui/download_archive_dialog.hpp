// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "download_archive_presentation.hpp"
#include <QDialog>
#include <QStringList>

namespace biglybtc::ui {
class DownloadArchiveDialog final : public QDialog {
  Q_OBJECT
public:
  explicit DownloadArchiveDialog(DownloadArchivePresentation, QWidget* parent = nullptr);
signals:
  void restoreRequested(quint64 session, QStringList hashes, bool start);
  void removeRequested(quint64 session, QStringList hashes, bool deleteMetainfo, bool deletePayload, ArchiveRowRevisions);
  void openLocationsRequested(QStringList locations, bool reveal);
};
}
