// Private two-file history publication/recovery. The actual source gate is held
// only during bounded synchronous file operations, never over HTTP/parser/UI.
std::string bString(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string bInteger(std::int64_t n){return "i"+std::to_string(n)+"e";}
std::string bDictionary(const std::map<std::string,std::string>& fields){std::string out="d";for(const auto& [key,value]:fields){out+=bString(key)+value;}return out+"e";}
std::string sha256(std::string_view bytes){return QCryptographicHash::hash(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha256).toHex().toStdString();}
bool stillNamed(int dir,const File& value){
  struct stat held{},label{};if(!value.present){return ::fstatat(dir,value.name.c_str(),&label,AT_SYMLINK_NOFOLLOW)!=0&&errno==ENOENT;}
  return ::fstat(value.descriptor.get(),&held)==0&&::fstatat(dir,value.name.c_str(),&label,AT_SYMLINK_NOFOLLOW)==0&&same(value.identity,held)&&same(held,label);
}
std::expected<void,E> unlinkOwned(int dir,const File& value){if(!value.present){return {};}if(!stillNamed(dir,value)||::unlinkat(dir,value.name.c_str(),0)!=0){return std::unexpected(E::SourceChanged);}return {};}
std::expected<void,E> writeAt(int dir,const std::string& leaf,const std::optional<std::string>& desired){
  auto primary=named(dir,leaf,4*1024*1024),backup=named(dir,leaf+".bak",4*1024*1024),saving=named(dir,leaf+".saving",4*1024*1024);if(!primary||!backup||!saving){return std::unexpected(E::SourceChanged);}
  if(!desired){auto removed=unlinkOwned(dir,*saving);if(!removed){return removed;}removed=unlinkOwned(dir,*primary);if(!removed){return removed;}if(::fsync(dir)!=0){return std::unexpected(E::SourceChanged);}return {};}
  if(primary->present&&primary->bytes==*desired){auto removed=unlinkOwned(dir,*saving);if(!removed){return removed;}return ::fsync(dir)==0?std::expected<void,E>{}:std::unexpected(E::SourceChanged);}
  if(saving->present&&saving->bytes!=*desired){auto removed=unlinkOwned(dir,*saving);if(!removed){return removed;}saving=named(dir,leaf+".saving",4*1024*1024);if(!saving){return std::unexpected(E::SourceChanged);}}
  if(!saving->present){auto owned=storage::detail::writeExclusiveOwned(dir,leaf+".saving",*desired,0600);if(!owned){return std::unexpected(E::SourceChanged);}if(::fchmod(owned->get(),0600)!=0||::fsync(owned->get())!=0){return std::unexpected(E::SourceChanged);}saving=named(dir,leaf+".saving",4*1024*1024);if(!saving||!saving->present){return std::unexpected(E::SourceChanged);}}
  if(!stillNamed(dir,*primary)||!stillNamed(dir,*backup)||!stillNamed(dir,*saving)){return std::unexpected(E::SourceChanged);}
  if(primary->present&&::renameat(dir,leaf.c_str(),dir,(leaf+".bak").c_str())!=0){return std::unexpected(E::SourceChanged);}
  if(!stillNamed(dir,*saving)||::syscall(SYS_renameat2,dir,(leaf+".saving").c_str(),dir,leaf.c_str(),RENAME_NOREPLACE)!=0||::fsync(dir)!=0){return std::unexpected(E::SourceChanged);}return {};
}
struct Intent {
  bool automatic=false;
  std::size_t selected=0;std::string sid,stem,beforeConfig,afterConfig,afterResults;
  std::optional<std::string> beforeResults,beforeConfigBackup,beforeResultsBackup;
  std::map<std::string,std::optional<std::string>> roots,children;
};
std::string intentBytes(const Intent& in){
  const auto bindings=[](const auto& values){std::map<std::string,std::string> out;for(const auto& [key,value]:values){out[key]=value?bString(*value):"i0e";}return bDictionary(out);};
  std::map<std::string,std::string> fields{{"schema",bString(in.automatic?"BiglyBTC.Subscription.Write.Automatic.V2":"BiglyBTC.Subscription.Write.V1")},{"selected",bInteger(static_cast<std::int64_t>(in.selected))},{"sid",bString(in.sid)},{"before_config",bString(in.beforeConfig)},{"after_config",bString(in.afterConfig)},{"after_results",bString(in.afterResults)},{"roots",bindings(in.roots)},{"children",bindings(in.children)}};
  for(const auto& item:{std::pair{"before_results",in.beforeResults},std::pair{"before_config_backup",in.beforeConfigBackup},std::pair{"before_results_backup",in.beforeResultsBackup}}){fields[item.first]=item.second?bString(*item.second):"i0e";}return bDictionary(fields);
}
std::expected<Intent,E> parseIntent(std::string raw){
  auto doc=core::Document::parse(std::move(raw),{28*1024*1024,4*1024*1024,500000,64});if(!doc||!unique(doc->root())){return std::unexpected(E::Malformed);}Intent out;
  const auto text=[&](std::string_view name)->std::optional<std::string>{const auto* p=doc->find(name);if(!p||p->type!=core::Node::Type::Bytes){return {};}return std::string(p->bytes);};
  out.automatic=text("schema")==std::optional{std::string("BiglyBTC.Subscription.Write.Automatic.V2")};if(!out.automatic&&text("schema")!=std::optional{std::string("BiglyBTC.Subscription.Write.V1")}){return std::unexpected(E::Malformed);}const auto sid=text("sid"),before=text("before_config"),after=text("after_config"),results=text("after_results");const auto* selected=doc->find("selected");if(!sid||sid->size()!=10||!before||!after||!results||!selected||selected->type!=core::Node::Type::Integer||selected->integer<0||selected->integer>=4096){return std::unexpected(E::Malformed);}
  out.sid=*sid;out.stem=QByteArray::fromStdString(*sid).toHex().toUpper().toStdString();out.selected=static_cast<std::size_t>(selected->integer);out.beforeConfig=*before;out.afterConfig=*after;out.afterResults=*results;
  for(auto item:{std::pair{"before_results",&out.beforeResults},std::pair{"before_config_backup",&out.beforeConfigBackup},std::pair{"before_results_backup",&out.beforeResultsBackup}}){const auto* p=doc->find(item.first);if(!p){return std::unexpected(E::Malformed);}if(p->type==core::Node::Type::Bytes){*item.second=std::string(p->bytes);}else if(p->type!=core::Node::Type::Integer||p->integer!=0){return std::unexpected(E::Malformed);}}
  const auto readBindings=[&](std::string_view field,auto& output)->bool{const auto* p=doc->find(field);if(!p||p->type!=core::Node::Type::Dictionary||p->children.size()>64){return false;}for(std::size_t i=0;i<p->children.size();i+=2){const auto key=std::string(p->children[i].bytes);if(key.empty()||key.find('/')!=std::string::npos||key=="."||key==".."){return false;}const auto& value=p->children[i+1];if(value.type==core::Node::Type::Integer&&value.integer==0){output[key]=std::nullopt;}else if(value.type==core::Node::Type::Bytes&&value.bytes.size()==64){output[key]=std::string(value.bytes);}else{return false;}}return true;};
  if(!readBindings("roots",out.roots)||!readBindings("children",out.children)){return std::unexpected(E::Malformed);}
  std::set<std::string> requiredRoots,requiredChildren;for(const auto* stem:{"metasearch.config","biglybt.config",".certs"}){for(const auto* suffix:{"",".bak",".saving"}){requiredRoots.insert(std::string(stem)+suffix);}}for(const auto* suffix:{"",".bak",".saving"}){requiredChildren.insert(out.stem+".biglybt"+suffix);}std::set<std::string> actualRoots,actualChildren;for(const auto& [name,value]:out.roots){(void)value;actualRoots.insert(name);}for(const auto& [name,value]:out.children){(void)value;actualChildren.insert(name);}if(requiredRoots!=actualRoots||requiredChildren!=actualChildren){return std::unexpected(E::Malformed);}const auto purpose=out.automatic?SubscriptionIndexPurpose::AutomaticAcquisition:SubscriptionIndexPurpose::Manual;auto index=parseSubscriptionIndex(out.beforeConfig,purpose),next=parseSubscriptionIndex(out.afterConfig,purpose);auto current=parseSubscriptionResults(out.beforeResults.value_or("d7:resultslee")),changed=parseSubscriptionResults(out.afterResults);if(!index||!next||!current||!changed||out.selected>=index->rows.size()||out.selected>=next->rows.size()||index->rows[out.selected].selected.shortId!=out.sid||index->rows[out.selected].selected!=next->rows[out.selected].selected||index->rows[out.selected].issue||next->rows[out.selected].issue){return std::unexpected(E::Malformed);}
  if(out.automatic&&(!index->rows[out.selected].autoDownload||!index->rows[out.selected].enabled||!index->rows[out.selected].subscribed)){return std::unexpected(E::Malformed);}
  // A recoverable intent may change only this existing row's known history
  // counters/times. All definition, identity, unknown and sibling bytes remain.
  const auto* nextList=next->source->find("subs");const auto* oldList=index->source->find("subs");const auto& oldRow=oldList->children[out.selected];const auto& newRow=nextList->children[out.selected];const auto* oldHistory=oldRow.find("history");const auto* newHistory=newRow.find("history");if(!newHistory||newHistory->type!=core::Node::Type::Dictionary){return std::unexpected(E::Malformed);}
  auto rawNode=[](const core::Node& node)->std::string{auto v=core::encodeValue(node);return v?*v:std::string{};};auto expectedHistory=core::Document::parse(oldHistory?rawNode(*oldHistory):"de");if(!expectedHistory){return std::unexpected(E::Malformed);}for(const auto* key:{"num_read","num_unread","last_scan","last_new"}){const auto* p=newHistory->find(key);if(p){if(p->type!=core::Node::Type::Integer||p->integer<0||!expectedHistory->replaceValue(key,rawNode(*p))){return std::unexpected(E::Malformed);}}}
  if(out.automatic){if(const auto* supported=newHistory->find("auto_dl_supported")){if(supported->type!=core::Node::Type::Integer||(supported->integer!=0&&supported->integer!=1)||!expectedHistory->replaceValue("auto_dl_supported",rawNode(*supported))){return std::unexpected(E::Malformed);}}}
  std::int64_t readCount=0,unreadCount=0;for(const auto& row:changed->rows){if(!row.deleted){if(row.read){++readCount;}else{++unreadCount;}}}const auto* reads=newHistory->find("num_read");const auto* unreads=newHistory->find("num_unread");if(!reads||!unreads||reads->type!=core::Node::Type::Integer||unreads->type!=core::Node::Type::Integer||reads->integer!=readCount||unreads->integer!=unreadCount){return std::unexpected(E::Malformed);}
  auto expectedRow=core::Document::parse(rawNode(oldRow));if(!expectedRow||!expectedRow->replaceValue("history",expectedHistory->serialize())){return std::unexpected(E::Malformed);}std::string expectedList="l";for(std::size_t i=0;i<oldList->children.size();++i){expectedList+=i==out.selected?expectedRow->serialize():rawNode(oldList->children[i]);}expectedList+='e';auto expectedConfig=core::Document::parse(out.beforeConfig);if(!expectedConfig||!expectedConfig->replaceValue("subs",expectedList)||expectedConfig->serialize()!=out.afterConfig){return std::unexpected(E::Malformed);}
  return out;
}
bool matchesOptional(const File& file,const std::optional<std::string>& expected){return file.present==expected.has_value()&&(!expected||file.bytes==*expected);}
std::expected<void,E> recoverIntent(const storage::ProfileLease& lease,int subdir,const Intent& in){
  auto config=named(lease.directoryHandle(),"subscriptions.config",4*1024*1024),configBackup=named(lease.directoryHandle(),"subscriptions.config.bak",4*1024*1024),configSaving=named(lease.directoryHandle(),"subscriptions.config.saving",4*1024*1024);
  const auto leaf=in.stem+".results";auto result=named(subdir,leaf,4*1024*1024),backup=named(subdir,leaf+".bak",4*1024*1024),saving=named(subdir,leaf+".saving",4*1024*1024);if(!config||!configBackup||!configSaving||!result||!backup||!saving){return std::unexpected(E::SourceChanged);}
  const auto allowed=[](const File& f,const std::optional<std::string>& a,const std::optional<std::string>& b,const std::optional<std::string>& c){return matchesOptional(f,a)||matchesOptional(f,b)||matchesOptional(f,c);};
  const bool configRenamed=!config->present&&((matchesOptional(*configBackup,in.beforeConfig)&&matchesOptional(*configSaving,in.afterConfig))||(matchesOptional(*configBackup,in.afterConfig)&&matchesOptional(*configSaving,in.beforeConfig)));
  const bool resultsRenamed=!result->present&&in.beforeResults&&((matchesOptional(*backup,in.beforeResults)&&matchesOptional(*saving,in.afterResults))||(matchesOptional(*backup,in.afterResults)&&matchesOptional(*saving,in.beforeResults)));
  if((!allowed(*config,in.beforeConfig,in.afterConfig,in.beforeConfig)&&!configRenamed)||!allowed(*configBackup,in.beforeConfigBackup,in.beforeConfig,in.afterConfig)||!allowed(*configSaving,std::nullopt,in.afterConfig,in.beforeConfig)
      ||(!allowed(*result,in.beforeResults,in.afterResults,in.beforeResults)&&!resultsRenamed)||!allowed(*backup,in.beforeResultsBackup,in.beforeResults,in.afterResults)||!allowed(*saving,std::nullopt,in.afterResults,in.beforeResults)){return std::unexpected(E::SourceChanged);}
  bool unchangedBindings=true;
  const auto bindings=[&](int dir,const auto& values){for(const auto& [name,expected]:values){auto file=named(dir,name,cap(name));if(!file||file->present!=expected.has_value()||(expected&&sha256(file->bytes)!=*expected)){return false;}}return true;};
  unchangedBindings=bindings(lease.directoryHandle(),in.roots)&&bindings(subdir,in.children);
  // Once final config is visible, the completed after state wins. Otherwise a
  // changed relevant source rolls back this owned unfinished history update.
  const bool completedAfter=config->present&&config->bytes==in.afterConfig&&result->present&&result->bytes==in.afterResults;
  const bool forward=completedAfter||unchangedBindings;
  auto results=writeAt(subdir,leaf,forward?std::optional{in.afterResults}:in.beforeResults);if(!results){return results;}
  auto configuration=writeAt(lease.directoryHandle(),"subscriptions.config",forward?std::optional{in.afterConfig}:std::optional{in.beforeConfig});if(!configuration){return configuration;}
  return {};
}
