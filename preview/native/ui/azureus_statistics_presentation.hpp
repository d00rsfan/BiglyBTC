#pragma once
#include "owned_dht_statistics.hpp"
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
};
using AzureusStatisticsPresentation=std::shared_ptr<const AzureusStatisticsPacket>;
inline bool acceptAzureusStatistics(const AzureusStatisticsPresentation& previous,
    const AzureusStatisticsPresentation& incoming,quint64 session)noexcept{
  if(!incoming || !session || incoming->session!=session || !incoming->sequence || incoming->failure.size()>8192
      ||(previous &&(previous->session!=session ||incoming->sequence<=previous->sequence))){return false;}
  switch(incoming->state){
    case AzureusStatisticsState::Disabled:return !incoming->snapshot && incoming->failure.isEmpty();
    case AzureusStatisticsState::Unavailable:return !incoming->snapshot;
    case AzureusStatisticsState::Observed:{
      if(!incoming->snapshot || !incoming->failure.isEmpty()){return false;}
      const auto& now=incoming->snapshot->transport();if(!now.generation()||!now.publication()){return false;}
      if(previous && previous->snapshot){const auto& old=previous->snapshot->transport();
        if(now.generation()<old.generation() ||(now.generation()==old.generation() &&now.publication()<=old.publication())){return false;}}
      return true;
    }
  }
  return false;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::AzureusStatisticsPresentation)
