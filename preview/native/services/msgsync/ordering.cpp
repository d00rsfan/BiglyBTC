// SPDX-License-Identifier: GPL-2.0-or-later
// BuddyPluginBeta.sortMessagesSupport/flattenTree/merge, pinned cb8c6cb9.
// Original policy: Copyright (C) Azureus Software, Inc.
#include "ordering.hpp"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <functional>
namespace biglybtc::services::msgsync {
Result<std::vector<std::size_t>> displayMessageOrder(std::span<const DisplayMessage> messages){
 if(messages.size()>128){return std::unexpected(Error::Limit);}const auto n=messages.size();using List=std::vector<std::size_t>;
 const auto uid=[&](auto a,auto b){return messages[a].ordinal<messages[b].ordinal;};
 std::set<std::uint64_t> unique;for(const auto& m:messages){if(!m.ordinal||!unique.insert(m.ordinal).second){return std::unexpected(Error::Input);}}
 std::map<MessageId,std::size_t> ids;for(std::size_t i=0;i<n;++i){ids[messages[i].id]=i;}
 std::vector<std::optional<std::size_t>> previous(n);std::vector<List> next(n);
 for(std::size_t i=0;i<n;++i){if(messages[i].previous){const auto found=ids.find(*messages[i].previous);if(found!=ids.end()){previous[i]=found->second;next[found->second].push_back(i);}}}
 std::set<std::size_t,decltype(uid)> linked(uid),heads(uid);for(std::size_t i=0;i<n;++i){if(previous[i]){linked.insert(i);}}
 while(!linked.empty()){const auto start=*linked.begin();linked.erase(start);auto current=start;for(std::size_t loops=0;loops<n;++loops){if(!previous[current]){break;}const auto parent=*previous[current];linked.erase(parent);if(parent==start){previous[current].reset();next[parent].clear();break;}current=parent;}}
 for(std::size_t i=0;i<n;++i){if(previous[i]){auto parent=*previous[i];for(std::size_t loops=0;loops<n;++loops){if(!previous[parent]){heads.insert(parent);break;}parent=*previous[parent];}}}
 const auto merge=[&](const List& a,const List& b){List result;result.reserve(a.size()+b.size());std::size_t i=0,j=0;while(i<a.size()&&j<b.size()){const auto& x=messages[a[i]];const auto& y=messages[b[j]];auto xx=x.sequence,yy=y.sequence;if(xx==yy||(xx==0&&!x.previous)||(yy==0&&!y.previous)){xx=x.timestamp;yy=y.timestamp;}if(xx<yy||(xx==yy&&x.ordinal<y.ordinal)){result.push_back(a[i++]);}else{result.push_back(b[j++]);}}result.insert(result.end(),a.begin()+static_cast<std::ptrdiff_t>(i),a.end());result.insert(result.end(),b.begin()+static_cast<std::ptrdiff_t>(j),b.end());return result;};
 std::function<List(std::size_t,std::size_t)> flatten;
 flatten=[&](std::size_t head,std::size_t budget){List chain;if(!budget){return chain;}auto current=head;for(std::size_t guard=0;guard<n&&budget;++guard){chain.push_back(current);--budget;const auto& children=next[current];if(children.size()==1){current=children[0];}else if(children.size()>1){List branches;for(const auto child:children){auto branch=flatten(child,budget);budget-=std::min(budget,branch.size());branches=merge(branches,branch);}chain.insert(chain.end(),branches.begin(),branches.end());break;}else{break;}}return chain;};
 List result;std::set<std::size_t> remainder;for(std::size_t i=0;i<n;++i){remainder.insert(i);}for(const auto head:heads){auto chain=flatten(head,n);for(const auto row:chain){remainder.erase(row);}result=merge(result,chain);}
 List withSequence,withoutSequence;for(const auto row:remainder){const auto& m=messages[row];(m.sequence==0&&!m.previous?withoutSequence:withSequence).push_back(row);}
 std::ranges::sort(withSequence,[&](auto a,auto b){const auto& x=messages[a];const auto& y=messages[b];if(x.sequence!=y.sequence){return x.sequence<y.sequence;}if(x.timestamp!=y.timestamp){return x.timestamp<y.timestamp;}return x.ordinal<y.ordinal;});
 std::ranges::sort(withoutSequence,[&](auto a,auto b){const auto& x=messages[a];const auto& y=messages[b];return x.timestamp!=y.timestamp?x.timestamp<y.timestamp:x.ordinal<y.ordinal;});result=merge(result,withSequence);result=merge(result,withoutSequence);
 if(result.size()!=n){return std::unexpected(Error::Input);}std::set<std::size_t> checked(result.begin(),result.end());if(checked.size()!=n){return std::unexpected(Error::Input);}return result;
}
}
