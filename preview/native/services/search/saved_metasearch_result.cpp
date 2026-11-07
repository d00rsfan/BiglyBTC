// SPDX-License-Identifier: GPL-2.0-or-later
#include "saved_metasearch_result.hpp"
#include <new>
namespace biglybtc::search {
struct SavedMetasearchResultHandle::Impl {
  std::shared_ptr<const SavedMetasearchSourceSnapshot> source;
  std::shared_ptr<std::stop_source> lifetime;
  SavedMetasearchResultDescription description;
};
SavedMetasearchResultHandle::SavedMetasearchResultHandle(std::shared_ptr<const Impl> value)noexcept:impl_(std::move(value)){}
SavedMetasearchResultHandle::SavedMetasearchResultHandle(SavedMetasearchResultHandle&&)noexcept=default;
SavedMetasearchResultHandle& SavedMetasearchResultHandle::operator=(SavedMetasearchResultHandle&&)noexcept=default;
SavedMetasearchResultHandle::~SavedMetasearchResultHandle()=default;
const SavedMetasearchResultDescription& SavedMetasearchResultHandle::description()const noexcept{return impl_->description;}
std::expected<std::uint64_t,SavedRssSourceError> SavedMetasearchResultHandle::current(const storage::ProfileLease& lease,std::uint64_t session)const{
  if(!impl_||!impl_->lifetime||impl_->lifetime->stop_requested()){return std::unexpected(SavedRssSourceError::SourceChanged);}
  auto actual=impl_->source->current(lease,session);if(!actual){return actual;}if(impl_->lifetime->stop_requested()){return std::unexpected(SavedRssSourceError::SourceChanged);}return actual;
}
std::expected<SavedMetasearchResultHandle,SavedRssSourceError> SavedMetasearchResultHandle::mint(
    const storage::ProfileLease& lease,std::uint64_t session,std::shared_ptr<const SavedMetasearchSourceSnapshot> source,
    const MetasearchRowValues& row,MetasearchResultKey key,std::shared_ptr<std::stop_source> lifetime){try{
  using E=SavedRssSourceError;if(!source||!lifetime||lifetime->stop_requested()||!key.queryNonce||!key.rowId||row.incomplete||!row.rawTorrentLink||row.rawTorrentLink->empty()||!row.torrentLink||row.torrentLink->empty()){return std::unexpected(E::Unsupported);}
  auto observed=source->current(lease,session);if(!observed){return std::unexpected(observed.error());}auto request=source->resultDescription(lease,session,*row.torrentLink);if(!request){return std::unexpected(request.error());}
  auto out=std::make_shared<Impl>();out->source=std::move(source);out->lifetime=std::move(lifetime);static_cast<RssGetDescription&>(out->description)=std::move(*request);out->description.key=key;out->description.engine=out->source->description().selected;out->description.query=out->source->description().query;
  const auto referer=out->source->description().context.rootPage.toUtf8().toStdString();if(!referer.empty()){if(referer.size()>2048){return std::unexpected(E::Limit);}out->description.referer=referer;}
  if(row.hash&&!row.hash->empty()){
    if(row.hash->size()!=32){return std::unexpected(E::Unsupported);}std::array<std::byte,20> decoded{};std::uint32_t pending=0;unsigned bits=0;std::size_t at=0;
    for(unsigned char c:*row.hash){if(c>='a'&&c<='z'){c=static_cast<unsigned char>(c-'a'+'A');}const int digit=c>='A'&&c<='Z'?c-'A':c>='2'&&c<='7'?c-'2'+26:-1;if(digit<0){return std::unexpected(E::Unsupported);}pending=(pending<<5)|static_cast<unsigned>(digit);bits+=5;if(bits>=8){bits-=8;decoded[at++]=std::byte((pending>>bits)&255);}}
    if(at!=20||bits){return std::unexpected(E::Unsupported);}out->description.expectedV1Hash=decoded;
  }
  SavedMetasearchResultHandle handle(std::move(out));auto current=handle.current(lease,session);if(!current){return std::unexpected(current.error());}return handle;
}catch(const std::bad_alloc&){return std::unexpected(SavedRssSourceError::Memory);}}
std::expected<SavedMetasearchResultHandle,SavedRssSourceError> SavedMetasearchResultHandle::retainedCopy(const storage::ProfileLease& lease,std::uint64_t session)const{
  auto actual=current(lease,session);if(!actual){return std::unexpected(actual.error());}return SavedMetasearchResultHandle(impl_);
}
bool SavedMetasearchResultHandle::sameIdentity(const SavedMetasearchResultHandle& other)const noexcept{return impl_&&impl_==other.impl_;}
struct SavedMetasearchMagnetHandle::Impl {
  std::shared_ptr<const SavedMetasearchSourceSnapshot> source;
  std::shared_ptr<std::stop_source> lifetime;
  std::stop_token receiver;
  std::string uri;
  SavedMetasearchMagnetDescription description;
  std::optional<std::array<std::byte,20>> expectedV1;
};
SavedMetasearchMagnetHandle::SavedMetasearchMagnetHandle(std::shared_ptr<const Impl> value)noexcept:impl_(std::move(value)){}
SavedMetasearchMagnetHandle::SavedMetasearchMagnetHandle(SavedMetasearchMagnetHandle&&)noexcept=default;
SavedMetasearchMagnetHandle& SavedMetasearchMagnetHandle::operator=(SavedMetasearchMagnetHandle&&)noexcept=default;
SavedMetasearchMagnetHandle::~SavedMetasearchMagnetHandle()=default;
const SavedMetasearchMagnetDescription& SavedMetasearchMagnetHandle::description()const noexcept{return impl_->description;}
std::string_view SavedMetasearchMagnetHandle::uri()const noexcept{return impl_->uri;}
std::stop_token SavedMetasearchMagnetHandle::queryToken()const noexcept{return impl_->lifetime->get_token();}
std::stop_token SavedMetasearchMagnetHandle::receiverToken()const noexcept{return impl_->receiver;}
const std::optional<std::array<std::byte,20>>& SavedMetasearchMagnetHandle::expectedV1Hash()const noexcept{return impl_->expectedV1;}
std::expected<std::uint64_t,SavedRssSourceError> SavedMetasearchMagnetHandle::current(const storage::ProfileLease& lease,std::uint64_t session)const{
  if(!impl_||!impl_->lifetime||impl_->lifetime->stop_requested()||impl_->receiver.stop_requested()){return std::unexpected(SavedRssSourceError::SourceChanged);}
  auto actual=impl_->source->current(lease,session);if(!actual){return actual;}
  if(impl_->lifetime->stop_requested()||impl_->receiver.stop_requested()){return std::unexpected(SavedRssSourceError::SourceChanged);}return actual;
}
std::expected<SavedMetasearchMagnetHandle,SavedRssSourceError> SavedMetasearchMagnetHandle::mint(
    const storage::ProfileLease& lease,std::uint64_t session,std::shared_ptr<const SavedMetasearchSourceSnapshot> source,
    const MetasearchRowValues& row,MetasearchResultKey key,std::shared_ptr<std::stop_source> lifetime,std::stop_token receiver){try{
  using E=SavedRssSourceError;
  if(!source||!lifetime||lifetime->stop_requested()||!receiver.stop_possible()||receiver.stop_requested()||!key.queryNonce||!key.rowId||row.incomplete
      ||!row.rawTorrentLink||!row.torrentLink||*row.rawTorrentLink!=*row.torrentLink||!row.torrentLink->starts_with("magnet:?")||row.torrentLink->size()>65536){return std::unexpected(E::Unsupported);}
  auto observed=source->current(lease,session);if(!observed){return std::unexpected(observed.error());}
  auto value=std::make_shared<Impl>();value->source=std::move(source);value->lifetime=std::move(lifetime);value->receiver=receiver;value->uri=*row.torrentLink;
  value->description={key,value->source->description().selected,value->source->description().query,row.title.value_or(std::string{})};
  if(row.hash&&!row.hash->empty()){
    if(row.hash->size()!=32){return std::unexpected(E::Unsupported);}std::array<std::byte,20> decoded{};std::uint32_t pending=0;unsigned bits=0;std::size_t at=0;
    for(unsigned char c:*row.hash){if(c>='a'&&c<='z'){c=static_cast<unsigned char>(c-'a'+'A');}const int digit=c>='A'&&c<='Z'?c-'A':c>='2'&&c<='7'?c-'2'+26:-1;if(digit<0){return std::unexpected(E::Unsupported);}pending=(pending<<5)|static_cast<unsigned>(digit);bits+=5;if(bits>=8){bits-=8;decoded[at++]=std::byte((pending>>bits)&255);}}
    if(at!=20||bits){return std::unexpected(E::Unsupported);}value->expectedV1=decoded;
  }
  SavedMetasearchMagnetHandle handle(std::move(value));auto current=handle.current(lease,session);if(!current){return std::unexpected(current.error());}return handle;
}catch(const std::bad_alloc&){return std::unexpected(SavedRssSourceError::Memory);}}
std::expected<SavedMetasearchMagnetHandle,SavedRssSourceError> SavedMetasearchMagnetHandle::retainedCopy(const storage::ProfileLease& lease,std::uint64_t session)const{
  auto actual=current(lease,session);if(!actual){return std::unexpected(actual.error());}return SavedMetasearchMagnetHandle(impl_);
}
} // namespace biglybtc::search
