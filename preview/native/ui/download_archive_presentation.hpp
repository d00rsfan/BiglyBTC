// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "biglybtc/download_archive_values.hpp"
#include <QMetaType>
#include <QString>
#include <memory>

namespace biglybtc::ui {
struct DownloadArchivePacket {
  quint64 session = 0;
  std::vector<core::ArchivedDownload> rows;
  bool recovered = false;
  bool writable = false;
  QString error;
};
using DownloadArchivePresentation = std::shared_ptr<const DownloadArchivePacket>;
}

Q_DECLARE_METATYPE(biglybtc::ui::DownloadArchivePresentation)
