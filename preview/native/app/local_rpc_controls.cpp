// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_rpc_controls.hpp"

#include "library_window.hpp"
#include "session_bridge.hpp"

namespace biglybtc::app {

void bindLocalRpcControls(ui::LibraryWindow& window, SessionBridge& bridge) {
  using Window = ui::LibraryWindow;
  QObject::connect(&bridge, &SessionBridge::localRpcReady, &window, &Window::setLocalRpc);
  QObject::connect(&window, &Window::localRpcRequested, &bridge,
      [owner = QPointer(&bridge), view = QPointer(&window)](bool running) {
    if (!owner || !view) { return; }
    const bool accepted = running ? owner->startSavedLocalRpc("xmwebui") : owner->stopLocalRpc();
    if (!accepted && view) { view->setMessage(Window::tr("Wait for the current operation before changing local RPC.")); }
  });
}

} // namespace biglybtc::app
