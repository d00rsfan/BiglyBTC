#pragma once
#include "checkpoint_batch.hpp"
#include "biglybtc/library.hpp"
#include <map>

namespace biglybtc::engine::detail {
// Owner-thread shutdown transaction, not piece-map authority. Keep the original
// inventory immutable while ordinary transfers retire. Each directory transaction
// still bounds the number and encoded size of its state files.
// The caller must report all participants failed if commit fails. No destructor
// publication/retry. An exact source CAS and the ordinary fsync/rename protocol
// remain at the single final publication. Completion renames are excluded.
class ManagedCheckpointInventory final {
public:
  std::expected<core::SavedDownload,storage::Error> select(const storage::ProfileLease&,const core::TorrentHash&);
  const core::Document& document()const{return inventory_->document();}
  std::expected<void,storage::Error> stage(const core::SavedDownload&,std::string row,storage::CheckpointState);
  std::expected<void,storage::Error> commit(const storage::ProfileLease&);
  std::size_t pending()const noexcept{return rows_.size();}
  void releaseSource()noexcept{access_.reset();}
  void clear()noexcept{states_.clear();rows_.clear();rowBytes_=0;index_.clear();inventory_.reset();access_.reset();}
private:
  std::size_t rowBytes_=0;
  std::vector<storage::CheckpointState> states_;
  std::optional<storage::ProfileSourceAccess> access_;
  std::optional<storage::ConfigFile> inventory_;
  std::map<std::string,core::SavedDownload> index_;
  std::map<std::size_t,std::string> rows_;
};
}
