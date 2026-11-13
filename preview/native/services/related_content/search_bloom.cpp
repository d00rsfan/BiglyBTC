// SPDX-License-Identifier: GPL-2.0-or-later
#include "search_bloom.hpp"
#include <QString>
#include <QStringList>
#include <QStringConverter>
#include <algorithm>
#include <random>
#include <set>
namespace biglybtc::services::related_content {
namespace {
auto fail(const char* s){return std::unexpected(Error{s});}
Result<QString> utf(std::string_view text){QStringDecoder decoder(QStringDecoder::Utf8);auto value=QString(decoder(QByteArrayView(text.data(),qsizetype(text.size()))));if(decoder.hasError()){return fail("Search Bloom requires UTF-8");}return value;}
QString trim(QString value){while(!value.isEmpty()&&value.front().unicode()<=32){value.remove(0,1);}while(!value.isEmpty()&&value.back().unicode()<=32){value.chop(1);}return value;}
bool javaLetterOrDigit(QChar c){const auto category=c.category();return (category>=QChar::Letter_Uppercase&&category<=QChar::Letter_Other)||category==QChar::Number_DecimalDigit;}
}
Result<std::vector<std::string>> searchWords(const Item& item){
 if(item.title.size()>16384||item.tags.size()>128){return fail("Search Bloom text exceeds its bound");}auto title=utf(item.title);if(!title){return std::unexpected(title.error());}
 auto lower=title->toLower();for(auto& c:lower){if(!javaLetterOrDigit(c)){c=QChar(' ');}}
 static const std::set<QString> ignored={"a","in","of","at","the","and","or","if","to","an","for","with"};std::vector<std::string> out;
 for(const auto& word:lower.split(' ',Qt::SkipEmptyParts)){if(!ignored.contains(word)){out.push_back(word.toUtf8().toStdString());}}
 for(const auto& tag:item.tags){if(tag.size()>512){return fail("Search Bloom tag exceeds its bound");}auto decoded=utf(tag);if(!decoded){return std::unexpected(decoded.error());}decoded->replace(' ','+');for(qsizetype n=1;n<=decoded->size();++n){out.push_back(("tag:"+decoded->left(n)).toUtf8().toStdString());}}
 if(item.hash.size()==20){out.push_back("hash:"+base32(item.hash));}return out;
}
Result<SearchBlooms> buildSearchBlooms(std::span<const Item> cached,std::span<const Item> dht,std::uint64_t seed){
 if(cached.size()>MaximumItems||dht.size()>MaximumItems){return fail("Search Bloom item bound exceeded");}
 std::set<std::string> local,remote;std::size_t bytes=0;auto collect=[&](auto items,auto& words)->Result<bool>{for(const auto& item:items){auto next=searchWords(item);if(!next){return std::unexpected(next.error());}for(auto& word:*next){if(words.insert(word).second){bytes+=word.size();if(words.size()>100000||bytes>16*1024*1024){return fail("Search Bloom word budget exceeded");}}}}return true;};
 if(auto result=collect(cached,local);!result){return std::unexpected(result.error());}if(auto result=collect(dht,remote);!result){return std::unexpected(result.error());}for(const auto& word:local){remote.erase(word);}
 const auto bits=std::uint32_t(std::clamp((local.size()+remote.size())*8,std::size_t(1000),std::size_t(50000)));auto all=SearchBloom::createForRelatedSearch(bits),without=SearchBloom::createForRelatedSearch(std::uint32_t(std::clamp(local.size()*8,std::size_t(1000),std::size_t(50000))));if(!all||!without){return fail("Cannot allocate original search Bloom");}
 std::mt19937_64 random(seed);std::vector<std::string> words(local.begin(),local.end());std::shuffle(words.begin(),words.end(),random);
 for(const auto& word:words){all->add(word);if(all->count()>=6250){break;}if(without->count()<6250){without->add(word);}}
 words.assign(remote.begin(),remote.end());std::shuffle(words.begin(),words.end(),random);for(const auto& word:words){all->add(word);if(all->count()>=6250){break;}}return SearchBlooms{std::move(*all),std::move(*without)};
}
Result<bool> bloomMayMatch(const SearchBloom& bloom,const SearchQuery& query){
 if(query.popular()){return true;}auto decoded=utf(query.normalisedTerm());if(!decoded){return std::unexpected(decoded.error());}int matched=0;
 for(auto bit:decoded->toLower().split(' ',Qt::KeepEmptyParts)){bit=trim(bit);if(bit.isEmpty()){continue;}bool negative=false;if(bit.startsWith('+')){bit.remove(0,1);}else if(bit.startsWith('-')){negative=true;bit.remove(0,1);}if(bit.isEmpty()||(bit.startsWith('(')&&bit.endsWith(')'))){continue;}
 if(bit.contains('|')){bool any=false,hit=false;for(auto part:bit.split('|')){part=trim(part);if(part.isEmpty()){continue;}any=true;hit|=bloom.contains(part.toUtf8().toStdString());}if(!any){continue;}if(!hit){return false;}++matched;}
 else{const bool hit=bloom.contains(bit.toUtf8().toStdString());if(negative?hit:!hit){return false;}++matched;}
 }return matched>0;
}
}
