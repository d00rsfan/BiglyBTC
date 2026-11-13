// SPDX-License-Identifier: GPL-2.0-or-later
// Execute qualified original publication receipts on the shared owner's clock.
bool Owner::ratingEnqueue(std::string_view hash,bool priority){
 if(!ratingPolicy_){return false;}auto at=ratingRegistrations_.find(std::string(hash));if(at==ratingRegistrations_.end()){return false;}auto bound=ratingPolicy_->insertionBound(at->second);if(!bound){return false;}RatingInputs inputs;const std::array<std::int32_t,1> choices{!priority&&*bound?static_cast<std::int32_t>(QRandomGenerator::system()->bounded(static_cast<quint32>(*bound))):0};if(!inputs.setChoices(choices)){return false;}auto result=ratingPolicy_->enqueue(at->second,priority,inputs);if(!result){return false;}ratingPolicyReceipt(*result);return true;
}
void Owner::ratingPolicyReceipt(const plugins::rating::publication::Receipt& receipt){
 if(receipt.timer){for(std::size_t n=0;n<receipt.size;++n){if(receipt.events[n].kind==plugins::rating::publication::EventKind::Timer){ratingTimers_.push_back({*receipt.timer,receipt.events[n].when});}}}
 if(receipt.write){for(std::size_t n=0;n<receipt.size;++n){const auto& event=receipt.events[n];if(event.kind==plugins::rating::publication::EventKind::Write){const auto label=ratingLabels_.find(event.number);if(label!=ratingLabels_.end()){ratingWriting_=RatingWrite{*receipt.write,label->second,{},false};}}}}
}
void Owner::ratingScheduleTick(){
 if(!ratingPolicy_){return;}std::vector<plugins::rating::publication::ReadHint> hints;bool refresh=false;
 {std::lock_guard lock(shared_->mutex);refresh=shared_->ratingHintsPending;if(refresh){shared_->ratingHintsPending=false;hints=shared_->ratingHints;}}
 if(refresh&&ratingReads_.update(hints)){
  std::set<std::string> present;for(const auto& hint:hints){present.insert(hint.id);auto at=ratingRegistrations_.find(hint.id);if(at==ratingRegistrations_.end()){auto download=ratingPolicy_->registerDownload(hint.id);if(!download){continue;}ratingLabels_.emplace(download->id(),hint.id);ratingRegistrations_.emplace(hint.id,*download);if(hint.scored){static_cast<void>(ratingEnqueue(hint.id,false));}}else{static_cast<void>(ratingPolicy_->setRemoved(at->second,false));}}
  for(const auto& [hash,download]:ratingRegistrations_){if(!present.contains(hash)){static_cast<void>(ratingPolicy_->setRemoved(download,true));static_cast<void>(ratingPolicy_->remove(download));if(auto work=ratings_.find(hash);work!=ratings_.end()){ratingCancel(work->second,"Torrent was removed from rating library");}}}
 }
 const auto now=QDateTime::currentMSecsSinceEpoch();
 for(auto at=ratingTimers_.begin();at!=ratingTimers_.end();){if(at->when<=now){auto timer=at->timer;at=ratingTimers_.erase(at);if(auto receipt=ratingPolicy_->fire(timer)){ratingPolicyReceipt(*receipt);}}else{++at;}}
 if(ratingWriting_){auto& write=*ratingWriting_;if(!write.started){auto existing=ratings_.find(write.hash);if(existing==ratings_.end()||!existing->second->view.running){const auto raw=QByteArray::fromHex(QByteArray::fromStdString(write.hash));auto hash=core::TorrentHash::parse(std::string_view(raw.constData(),raw.size()));if(hash){ratingStart({*hash,true,false});auto fresh=ratings_.find(write.hash);if(fresh!=ratings_.end()&&fresh->second->view.publishing){write.work=fresh->second;write.started=true;}}}}
  if(write.started){const auto work=write.work.lock();if(!work||!work->view.running){RatingInputs inputs;const std::array<std::int64_t,1> times{now};static_cast<void>(inputs.setTimes(times));auto receipt=ratingPolicy_->complete(write.completion,inputs);const auto hash=write.hash;ratingWriting_.reset();if(receipt){ratingPolicyReceipt(*receipt);}static_cast<void>(ratingReads_.request(hash,true));}}
 }
 for(unsigned n=0;n<256&&ratingPolicy_->pendingDispatches();++n){RatingInputs inputs;auto receipt=ratingPolicy_->dispatchOne(inputs);if(!receipt){break;}ratingPolicyReceipt(*receipt);}
 if(ratingReadActive_){const auto work=ratingReading_.lock();if(!work||!work->view.running){ratingReadActive_=false;ratingReading_.reset();ratingReads_.complete(now);}}
 if(ratingReadWaiting_){auto existing=ratings_.find(*ratingReadWaiting_);if(existing==ratings_.end()||!existing->second->view.running){const auto raw=QByteArray::fromHex(QByteArray::fromStdString(*ratingReadWaiting_));auto hash=core::TorrentHash::parse(std::string_view(raw.constData(),raw.size()));if(hash){ratingStart({*hash,false,false});auto work=ratings_.find(*ratingReadWaiting_);if(work!=ratings_.end()){ratingReading_=work->second;ratingReadActive_=true;}}ratingReadWaiting_.reset();}}
 if(ratingReading_.expired()&&!ratingReadWaiting_){if(auto next=ratingReads_.take(now)){ratingReadWaiting_=std::move(*next);}}
}
