#pragma once
#include "owned_dht_statistics.hpp"
#include "router.hpp"
#include "../services/azureus_control/lookup.hpp"
#include <QMetaType>
#include <QString>
#include <memory>

namespace biglybtc::ui {
enum class AzureusStatisticsState { Disabled, Observed, Unavailable };
struct AzureusStatisticsPacket {
  quint64 session=0,sequence=0;
  AzureusStatisticsState state=AzureusStatisticsState::Unavailable;
  std::shared_ptr<const services::azureus_udp::OwnedDhtStatisticsSnapshot> snapshot;
  QString failure;
  bool current=true; // False retains actual snapshots without presenting them as live.
  std::shared_ptr<const services::azureus_router::RouterStatisticsSnapshot> routerSnapshot;
  std::shared_ptr<const services::azureus_control::LookupSnapshot> lookupSnapshot;
};
using AzureusStatisticsPresentation=std::shared_ptr<const AzureusStatisticsPacket>;
inline bool acceptAzureusStatistics(const AzureusStatisticsPresentation& previous,
    const AzureusStatisticsPresentation& incoming,quint64 session)noexcept{
  if(!incoming || !session || incoming->session!=session || !incoming->sequence || incoming->failure.size()>8192
      ||(previous &&(previous->session!=session ||incoming->sequence<=previous->sequence))){return false;}
  switch(incoming->state){
    case AzureusStatisticsState::Disabled:return !incoming->snapshot && !incoming->routerSnapshot && !incoming->lookupSnapshot && incoming->failure.isEmpty();
    case AzureusStatisticsState::Unavailable:return !incoming->snapshot && !incoming->routerSnapshot && !incoming->lookupSnapshot;
    case AzureusStatisticsState::Observed:{
      if(!incoming->snapshot || !incoming->failure.isEmpty()){return false;}
      const auto& now=incoming->snapshot->transport();if(!now.generation()||!now.publication()){return false;}
      const auto& router=incoming->routerSnapshot;
      if(router && (!router->generation() || !router->revision())){return false;}
      const auto& lookup=incoming->lookupSnapshot;
      if(lookup&&(!services::azureus_control::validLookupSnapshot(*lookup)||lookup->generation!=now.generation())){return false;}
      bool lookupChanged=previous&&static_cast<bool>(previous->lookupSnapshot)!=static_cast<bool>(lookup);
      if(previous&&previous->lookupSnapshot&&lookup){
        const auto& old=*previous->lookupSnapshot;
        if(lookup->generation<old.generation||(lookup->generation==old.generation
            &&(lookup->lookup<old.lookup||(lookup->lookup==old.lookup&&lookup->revision<old.revision)))){return false;}
        lookupChanged=lookup->generation!=old.generation||lookup->lookup!=old.lookup||lookup->revision!=old.revision;
      }
      bool routerChanged=previous && static_cast<bool>(previous->routerSnapshot)!=static_cast<bool>(router);
      if(previous && previous->routerSnapshot && router){
        const auto& old=*previous->routerSnapshot;
        if(router->generation()<old.generation() ||(router->generation()==old.generation() && router->revision()<old.revision())){return false;}
        routerChanged=router->generation()!=old.generation() || router->revision()!=old.revision();
      }
      if(previous && previous->snapshot){const auto& old=previous->snapshot->transport();
        if(now.generation()<old.generation()){return false;}
        if(now.generation()==old.generation()){
          if(now.publication()<old.publication()){return false;}
          // Actor facts can stay fixed while availability or the router changes.
          if(now.publication()==old.publication() && previous->current==incoming->current && !routerChanged && !lookupChanged){return false;}
        }}
      return true;
    }
  }
  return false;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::AzureusStatisticsPresentation)
