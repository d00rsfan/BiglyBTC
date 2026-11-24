// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace biglybtc::ui { class LibraryWindow; }

namespace biglybtc::app {
class SessionBridge;
void bindLocalRpcControls(ui::LibraryWindow&, SessionBridge&);
} // namespace biglybtc::app
