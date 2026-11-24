// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_constraint_worker.hpp"
namespace biglybtc::app {
// Existing Host worker only. Each step owns/releases its FS gate separately.
// Query the selected admission and the complete census at most once each.
// Consume them before a Host presentation change can invalidate the request.
// frame owns the bounded replies and reports the last sequence consumed.
std::expected<ConstraintWorkerUpdate,storage::Error> runConstraintPeerTurn(
 TagConstraintWorker&,storage::ManualTagStore&,const storage::ManualTagSnapshot&,
 ConstraintWorkerFrame&,const storage::ProfileLease&,engine::PersistentSession*,std::stop_token={},
 OwnedI2pService* i2p=nullptr);
}
