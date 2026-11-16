// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_constraint_worker.hpp"
namespace biglybtc::app {
// Existing Host worker only. Each step owns/releases its FS gate separately.
// Query <=1 current SDK admission between steps,then consume immediately before
// another Host presentation refresh can invalidate its target generation.
// frame owns the bounded replies and reports the last sequence consumed.
std::expected<ConstraintWorkerUpdate,storage::Error> runConstraintPeerTurn(
 TagConstraintWorker&,storage::ManualTagStore&,const storage::ManualTagSnapshot&,
 ConstraintWorkerFrame&,engine::PersistentSession*,std::stop_token={});
}
