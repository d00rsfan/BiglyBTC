// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace biglybtc::ui { class LibraryWindow; }

namespace biglybtc::app {
class SessionBridge;
struct LocalMediaInterface {
  QString name;
  QString address;
};

struct LocalMediaFfmpeg {
  QString executable;
  QByteArray sha256;
};

void bindLocalMediaControls(ui::LibraryWindow&, SessionBridge&, std::optional<LocalMediaInterface> = {},
    std::optional<LocalMediaFfmpeg> = {});
} // namespace biglybtc::app
