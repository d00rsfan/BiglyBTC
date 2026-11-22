// SPDX-License-Identifier: BSD-3-Clause
#include "libtorrent/biglybtc_shared_quota.hpp"
#include "biglybtc_tag_gate.hpp"
#include "libtorrent/aux_/biglybtc_shared_binding.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/aux_/bandwidth_manager.hpp"
#include "libtorrent/peer_class.hpp"
#include <limits>
namespace libtorrent::aux {bool owns_shared_quota(biglybtc_shared_quota_v1 const&) noexcept;}
extern "C" int biglybtc_tag_shared_bind_v1(void* raw,std::uint32_t group,biglybtc_shared_quota_v1 const* operations) noexcept {
 if(!raw||group==1||!operations||!libtorrent::aux::valid_shared_quota(*operations))return -1;
 if(!libtorrent::aux::owns_shared_quota(*operations))return -3;
 auto* session=static_cast<libtorrent::aux::session_interface*>(raw);
 if(!session->get_context().get_executor().running_in_this_thread())return -2;
 try{
  const auto& o=*operations;if(!o.epoch(o.budget))return -3;
  for(int d=0;d<2;++d){const int rate=o.limit(o.budget,d);if(rate<-1||rate==std::numeric_limits<int>::max())return -1;}
  auto* peer_class=session->peer_classes().at(libtorrent::peer_class_t{group});if(!peer_class)return -1;
  auto* up=session->get_bandwidth_manager(0);auto* down=session->get_bandwidth_manager(1);
  if(!up||!down||!up->can_bind_shared_scope(o)||!down->can_bind_shared_scope(o))return -3;
  bool both=true;
  for(auto& channel:peer_class->channel){const auto* old=channel.shared_operations();if(old&&(old->scope!=o.scope||old->budget!=o.budget))return -3;both=both&&old;}
  if(both)return 0;
  // Prepare the quota references before publishing either channel. Scope-only
  // binding on allocation failure grants no quota and remains profile-affine.
  auto upload=std::make_shared<libtorrent::aux::shared_channel_binding>(o,0);
  auto download=std::make_shared<libtorrent::aux::shared_channel_binding>(o,1);
  if(!libtorrent::biglybtc_tag_gate::register_shared_class(session,group))return -5;
  if(!up->bind_shared_scope(o)||!down->bind_shared_scope(o))return -3;
  peer_class->channel[0].bind_shared_prepared(std::move(upload));peer_class->channel[1].bind_shared_prepared(std::move(download));
  return 0;
 }catch(...){return -5;}
}
