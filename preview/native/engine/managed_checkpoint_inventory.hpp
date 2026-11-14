#pragma once
#include "profile.hpp"
#include "biglybtc/library.hpp"
#include <map>

namespace biglybtc::engine::detail {
// Owner-thread shutdown transaction, not piece-map authority. Keep the original
// inventory immutable while up to32 independently durable states are prepared.
// The caller must report all participants failed if commit fails. No destructor
// publication/retry. An exact source CAS and the ordinary fsync/rename protocol
// remain at the single final publication. Completion renames are excluded.
class ManagedCheckpointInventory final {
public:
  std::expected<core::SavedDownload,storage::Error> select(const storage::ProfileLease&,const core::TorrentHash&);
  const core::Document& document()const{return inventory_->document();}
  std::expected<void,storage::Error> stage(const core::SavedDownload&,std::string row);
  std::expected<void,storage::Error> commit(const storage::ProfileLease&);
  std::size_t pending()const noexcept{return rows_.size();}
  void clear()noexcept{rows_.clear();index_.clear();inventory_.reset();access_.reset();}
private:
  std::optional<storage::ProfileSourceAccess> access_;
  std::optional<storage::ConfigFile> inventory_;
  std::map<std::string,core::SavedDownload> index_;
  std::map<std::size_t,std::string> rows_;
};
}
