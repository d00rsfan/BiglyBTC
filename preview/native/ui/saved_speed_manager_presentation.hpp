#pragma once
#include "biglybtc/saved_speed_manager.hpp"
#include <QMetaType>
#include <QString>
#include <cmath>
namespace biglybtc::ui {
enum class SavedSpeedManagerState { Ready,Missing,Failed };
enum class SavedSpeedManagerSource { Primary,Backup,Saving };
enum class AutoSpeedPolicy { Inactive,Unavailable };
struct SavedSpeedManagerPacket {
  quint64 session=0,sequence=0;
  SavedSpeedManagerState state=SavedSpeedManagerState::Failed;
  AutoSpeedPolicy livePolicy=AutoSpeedPolicy::Unavailable;
  std::shared_ptr<const core::SavedSpeedSettings> settings;
  std::shared_ptr<const core::SavedSpeedHistory> history;
  std::optional<SavedSpeedManagerSource> source;
  QString failure;
};
using SavedSpeedManagerPresentation=std::shared_ptr<const SavedSpeedManagerPacket>;
inline bool acceptSavedSpeedManager(const SavedSpeedManagerPresentation& previous,const SavedSpeedManagerPresentation& incoming,quint64 session)noexcept{
  if(!incoming||!session||incoming->session!=session||!incoming->sequence||incoming->failure.size()>8192
      ||(previous&&(previous->session!=session||incoming->sequence<=previous->sequence))){return false;}
  if(incoming->livePolicy!=AutoSpeedPolicy::Inactive&&incoming->livePolicy!=AutoSpeedPolicy::Unavailable){return false;}
  if(incoming->source&&*incoming->source!=SavedSpeedManagerSource::Primary&&*incoming->source!=SavedSpeedManagerSource::Backup&&*incoming->source!=SavedSpeedManagerSource::Saving){return false;}
  if(incoming->settings){const auto& s=*incoming->settings;if(!s.source){return false;}for(const auto* value:{&s.asNumber,&s.asName,&s.bgpPrefix}){if(*value&&(*value)->size()>4096){return false;}}if(s.historyLeaf&&s.historyLeaf->size()>29){return false;}}
  if(incoming->history){if(!incoming->history->source){return false;}for(const auto* e:{&incoming->history->upload,&incoming->history->download}){if((e->metric&&!std::isfinite(*e->metric))||(e->type&&!std::isfinite(*e->type))){return false;}if((e->rawMetric&&e->rawMetric->size()>128)||(e->rawType&&e->rawType->size()>128)){return false;}}}
  switch(incoming->state){
    case SavedSpeedManagerState::Ready:return incoming->settings&&incoming->history&&incoming->source&&incoming->failure.isEmpty();
    case SavedSpeedManagerState::Missing:return incoming->settings&&!incoming->history&&!incoming->source&&incoming->failure.isEmpty();
    case SavedSpeedManagerState::Failed:return !incoming->history&&!incoming->source;
  }return false;
}
} // namespace biglybtc::ui
Q_DECLARE_METATYPE(biglybtc::ui::SavedSpeedManagerPresentation)
