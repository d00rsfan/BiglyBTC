// SPDX-License-Identifier: GPL-2.0-or-later
#include "tag_strip_data.hpp"
#include "original_tag_names.hpp"
#include <algorithm>
#include <limits>
namespace biglybtc::ui {
int originalTagNameCompare(QStringView a,QStringView b){
 qsizetype i=0,j=0;while(i<a.size()&&j<b.size()){
  auto x=a[i++],y=b[j++];if(x.isDigit()&&y.isDigit()){const auto beginA=i-1,beginB=j-1;while(i<a.size()&&a[i].isDigit()){++i;}while(j<b.size()&&b[j].isDigit()){++j;}const auto na=i-beginA,nb=j-beginB;if(na!=nb){return na<nb?-1:1;}for(qsizetype n=0;n<na;++n){const auto ca=a[beginA+n].unicode(),cb=b[beginB+n].unicode();if(ca!=cb){return ca<cb?-1:1;}}}
  else{x=x.toLower();y=y.toLower();if(x!=y){return x.unicode()<y.unicode()?-1:1;}}
 }return a.size()==b.size()?0:a.size()<b.size()?-1:1;
}
namespace {
QString text(std::string_view value){return QString::fromUtf8(value.data(),qsizetype(value.size()));}
std::optional<std::array<int,3>> colour(const core::Node* config){const auto* value=config?config->find("col.rgb"):nullptr;if(!value||value->type!=core::Node::Type::Bytes||value->bytes.size()>64){return {};}const auto parts=text(value->bytes).split(',');if(parts.size()!=3){return {};}std::array<int,3> result{};for(qsizetype i=0;i<3;++i){bool ok=false;result[std::size_t(i)]=parts[i].toInt(&ok);if(!ok||result[std::size_t(i)]<0||result[std::size_t(i)]>255){return {};}}return result;}
bool less(const TagChip& a,const TagChip& b){
 const bool ca=a.kind!=TagChipKind::Manual,cb=b.kind!=TagChipKind::Manual;if(ca!=cb){return ca;}
 if(ca){if(a.kind==TagChipKind::All||b.kind==TagChipKind::All){return a.kind==TagChipKind::All&&b.kind!=TagChipKind::All;}if(a.kind==TagChipKind::Uncategorised||b.kind==TagChipKind::Uncategorised){return a.kind!=TagChipKind::Uncategorised&&b.kind==TagChipKind::Uncategorised;}}
 if(a.group!=b.group){if(!a.group){return false;}if(!b.group){return true;}const auto order=originalTagNameCompare(*a.group,*b.group);if(order){return order<0;}}
 return originalTagNameCompare(a.name,b.name)<0;
}
}
std::expected<TagStripPresentation,TagStripDataError> makeTagStripData(quint64 session,quint64 libraryRevision,quint64 tagRevision,const core::SavedDownloadTags& tags,const core::SavedCategories& cats,std::span<const core::TorrentHash> hashes){
 if(!session||!libraryRevision||!tagRevision||!cats.source||!tags.source){return std::unexpected(TagStripDataError::Source);}auto checkedCats=core::readSavedCategories(cats.source->originalBytes());auto membership=core::resolveManualTagMembership(session,libraryRevision,tagRevision,tags,hashes,core::ManualTagVisibility::All);if(!checkedCats||!membership){return std::unexpected(TagStripDataError::Source);}
 auto out=std::make_shared<TagStripData>();out->session=session;out->libraryRevision=libraryRevision;out->tagRevision=tagRevision;out->membership=std::move(*membership);
 const auto addCategory=[&](QString key,QString name,TagChipKind kind){TagChip chip;chip.key=std::move(key);chip.name=std::move(name);chip.kind=kind;out->chips.push_back(std::move(chip));};addCategory(AllTagChip,QStringLiteral("All"),TagChipKind::All);addCategory(QStringLiteral("category:uncategorised"),QStringLiteral("Uncategorised"),TagChipKind::Uncategorised);
 for(const auto& cat:checkedCats->categories){if(cat.kind!=core::SavedCategoryKind::User){continue;}const auto name=text(cat.name);out->categories.insert(name);addCategory(QStringLiteral("category:user:")+QString::fromLatin1(QByteArray(cat.name.data(),qsizetype(cat.name.size())).toBase64()),name,TagChipKind::Category);}
 const auto* type=tags.source->find("3");
 for(const auto& row:out->membership->rows()){
  if(!row.uid||row.ambiguousIdentity||!row.rawName){continue;}TagChip chip;chip.key=QStringLiteral("tag:")+QString::number(*row.uid);chip.name=originalTagDisplayName(text(*row.rawName));chip.membershipKnown=row.membershipKnown;chip.visible=row.rawVisibility?*row.rawVisibility==1:true;
  if(!type||type->type!=core::Node::Type::Dictionary||row.sourceIndex>=type->children.size()/2){return std::unexpected(TagStripDataError::Source);}chip.tagId=static_cast<qint32>(*row.uid&0xffffffff);const auto& record=type->children[row.sourceIndex*2+1];const auto* config=record.find("c");
  if(const auto* group=config?config->find("gr"):nullptr;group&&group->type==core::Node::Type::Bytes){if(group->bytes.size()>4096){return std::unexpected(TagStripDataError::Limit);}chip.group=text(group->bytes);}if(const auto* flags=config?config->find("flag"):nullptr;flags&&flags->type==core::Node::Type::Integer){chip.filter=(static_cast<std::uint64_t>(flags->integer)&1)!=0;}chip.colour=colour(config);
  if(const auto* image=config?config->find("img.file"):nullptr;image&&image->type==core::Node::Type::Bytes){if(image->bytes.size()>4096){return std::unexpected(TagStripDataError::Limit);}chip.imageFile=text(image->bytes);}
  for(const auto& hash:row.resolvedHashes){chip.hashes.insert(QString::fromStdString(hash.hex()));}out->chips.push_back(std::move(chip));
 }
 std::stable_sort(out->chips.begin(),out->chips.end(),less);for(qsizetype i=0;i<out->chips.size();++i){if(out->byKey.contains(out->chips[i].key)){return std::unexpected(TagStripDataError::Source);}out->byKey.insert(out->chips[i].key,i);}return TagStripPresentation(std::move(out));
}
bool tagChipShown(const TagChip& chip,const TagStripSettings& settings,bool completed){if(chip.kind==TagChipKind::Manual){return settings.tags&&(!settings.tagsCompletedOnly||completed)&&(!settings.filtersOnly||chip.filter);}return settings.categories&&(!settings.categoriesCompletedOnly||completed);}
void TagStripSelection::click(const QString& key,bool control){if(selected_.contains(key)){if(selected_.size()==1){selected_={AllTagChip};}else{selected_.remove(key);}}else if(!control||key==AllTagChip){selected_={key};}else{selected_.remove(AllTagChip);selected_.insert(key);}}
bool TagStripSelection::accepts(const QString& hash,const QString& category)const{
 const auto matches=[&](const QString& key){if(key==AllTagChip){return true;}if(!data_){return false;}const auto found=data_->byKey.constFind(key);if(found==data_->byKey.cend()){return false;}const auto& chip=data_->chips[*found];switch(chip.kind){case TagChipKind::All:return true;case TagChipKind::Category:return !category.isNull()&&category==chip.name;case TagChipKind::Uncategorised:return category.isNull()||!data_->categories.contains(category);case TagChipKind::Manual:return chip.membershipKnown&&chip.hashes.contains(hash);}return false;};
 if(inclusive_){return std::ranges::any_of(selected_,matches);}return std::ranges::all_of(selected_,matches);
}
}
