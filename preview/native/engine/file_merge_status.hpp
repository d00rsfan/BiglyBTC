// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "biglybtc/swarm_file_merge.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace biglybtc::engine {
struct FileMergeRow {std::string source,target;int sourceFile=0,targetFile=0;std::int64_t submitted=0;unsigned verified=0,failures=0;std::string state;};
struct FileMergeStatus {core::FileMergeSettings settings;std::size_t candidates=0,pending=0,reservedBytes=0;bool truncated=false;std::int64_t submittedBytes=0;std::uint64_t verifiedPieces=0,rejectedPieces=0;std::string state;std::vector<FileMergeRow> rows;};
}
