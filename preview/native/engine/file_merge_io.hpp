// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
namespace biglybtc::engine::detail {
struct MergeIoOwner {std::string hash;std::uint64_t admission=0;bool operator==(const MergeIoOwner&)const=default;};
struct MergeIoFailure {enum Code {Read,Hash,Cancelled} code;std::string message;};
using MergeIoData=std::expected<std::vector<char>,MergeIoFailure>;
struct MergeIoJob {std::uint64_t id=0;std::vector<MergeIoOwner> owners;std::size_t reservedBytes=0;std::function<MergeIoData(std::stop_token)> execute;};
struct MergeIoResult {std::uint64_t id=0;MergeIoData data;};
// The sole auxiliary worker performs payload reads/hashing only. It never owns
// an SDK handle, pumps SDK alerts, writes a file or publishes profile metadata.
class FileMergeIo final {
public:
 FileMergeIo();~FileMergeIo();FileMergeIo(const FileMergeIo&)=delete;FileMergeIo& operator=(const FileMergeIo&)=delete;
 bool submit(MergeIoJob);std::vector<MergeIoResult> take();
 void setWake(std::function<void()>);void cancelOwner(const MergeIoOwner&);
 bool ownerBusy(const MergeIoOwner&)const;std::size_t reservedBytes()const;
 void stopAndJoin()noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
