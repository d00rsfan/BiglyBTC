// SPDX-License-Identifier: GPL-2.0-or-later
#include "search.hpp"
#include <QStringConverter>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
#include <map>
namespace biglybtc::services::related_content {
namespace {
std::unexpected<Error> fail(const char* text){return std::unexpected(Error{text});}
QString javaTrim(QString text){while(!text.isEmpty()&&text.front().unicode()<=32){text.remove(0,1);}while(!text.isEmpty()&&text.back().unicode()<=32){text.chop(1);}return text;}
bool equalIgnoringCase(const QString& first,const QString& second){
  if(first.size()!=second.size()){return false;}const auto a=first.toUcs4(),b=second.toUcs4();if(a.size()!=b.size()){return false;}
  for(qsizetype i=0;i<a.size();++i){if(a[i]==b[i]){continue;}const auto x=QChar::toUpper(a[i]),y=QChar::toUpper(b[i]);if(x!=y&&QChar::toLower(x)!=QChar::toLower(y)){return false;}}return true;
}
QString truncateTag(QString tag){if(tag.size()>20){tag.truncate(20);}while(!tag.isEmpty()&&tag.toUtf8().size()>20){tag.chop(1);}return tag;}
bool mightBeEvil(const QString& pattern){
  if(!pattern.contains(')')){return false;}std::vector<qsizetype> stack;
  for(qsizetype i=0;i<pattern.size();++i){if(pattern[i]=='('){stack.push_back(i+1);}else if(pattern[i]==')'){
    if(stack.empty()){return true;}const auto start=stack.back();stack.pop_back();
    if(i+1<pattern.size()&&QStringLiteral("*+{").contains(pattern[i+1])){for(auto j=start;j<i;++j){if(QStringLiteral("+*{|").contains(pattern[j])){return true;}}}
  }}return false;
}
Result<QString> utf(std::string_view bytes){QStringDecoder decode(QStringDecoder::Utf8);auto value=QString(decode(QByteArrayView(bytes.data(),qsizetype(bytes.size()))));if(decode.hasError()){return fail("Search requires valid UTF-8");}return value;}
QString normalise(QString text){
  text.replace(QRegularExpression(" *\\| *"),"|");
  const QRegularExpression quoted("\"([^\"]+)\"");auto matches=quoted.globalMatch(text);QString out;qsizetype previous=0;
  while(matches.hasNext()){const auto match=matches.next();out+=text.mid(previous,match.capturedStart()-previous);auto value=match.captured(1);
    if(value.contains(' ')){value.replace(QRegularExpression("\\s+")," ");value='('+value.replace(" ",".*?")+')';}out+=value;previous=match.capturedEnd();}
  out+=text.mid(previous);return out;
}
}
struct SearchQuery::Impl {
  struct Bit {QString value;bool negative=false;std::optional<QRegularExpression> pattern;};
  SearchRequest request;QString term;std::string bytes;std::vector<Bit> bits;bool popular=false;
};
SearchQuery::SearchQuery(std::shared_ptr<const Impl> value):impl_(std::move(value)){}
Result<SearchQuery> SearchQuery::compile(SearchRequest request){
  if(request.term.size()>1024){return fail("Search term exceeds its byte bound");}auto decoded=utf(request.term);if(!decoded){return std::unexpected(decoded.error());}
  auto value=std::make_shared<Impl>();value->request=std::move(request);value->term=normalise(*decoded);value->bytes=value->term.toUtf8().toStdString();value->popular=value->term=="(.)";
  const auto parts=value->term.toLower().split(' ',Qt::KeepEmptyParts);if(parts.size()>128){return fail("Search exceeds its term bound");}
  for(auto part:parts){Impl::Bit bit;part=javaTrim(part);if(part.startsWith('+')){part.remove(0,1);}else if(part.startsWith('-')){bit.negative=true;part.remove(0,1);}bit.value=part;
    const bool wrapped=part.startsWith('(')&&part.endsWith(')');
    if(wrapped||(part.contains('|')&&!part.contains("tag:"))){const auto pattern=wrapped?part.mid(1,part.size()-2):part;
      if(pattern.size()>512){return fail("Search regular expression exceeds its bound");}
      // Java rejects these PCRE-only constructs. Keep its literal fallback;
      // caller text cannot override the bounded matching directives below.
      if(!mightBeEvil(pattern)&&!pattern.contains("(*")&&!pattern.contains("(?P")&&!pattern.contains("(?R")&&!pattern.contains("\\K")){
        QRegularExpression compiled("(*LIMIT_MATCH=20000)(*LIMIT_DEPTH=128)"+pattern,QRegularExpression::CaseInsensitiveOption);
        if(compiled.isValid()){bit.pattern=std::move(compiled);}
      }
    }value->bits.push_back(std::move(bit));
  }return SearchQuery(std::move(value));
}
std::string_view SearchQuery::normalisedTerm()const noexcept{return impl_->bytes;}
bool SearchQuery::popular()const noexcept{return impl_->popular;}
Result<bool> SearchQuery::matches(const Item& item,std::int64_t nowMillis)const{
  const auto& q=*impl_;if(item.seeds()<q.request.minimumSeeds||item.leechers()<q.request.minimumLeechers){return false;}
  if(q.request.maximumAgeSeconds>0){const auto published=std::int64_t(item.publishedHours)*3600000;if(published<=0||(nowMillis-published)/1000>q.request.maximumAgeSeconds){return false;}}
  if(item.title.size()>16384||item.tags.size()>128){return fail("Search item exceeds its text bound");}const auto title=utf(item.title);if(!title){return std::unexpected(title.error());}
  if(q.term.startsWith("hash:")&&item.hash.size()==20&&q.term.mid(5)==QString::fromStdString(base32(item.hash))){return true;}
  if(!javaTrim(q.term).isEmpty()&&equalIgnoringCase(*title,q.term)){return true;}
  const auto lower=title->toLower();bool any=false;
  for(const auto& bit:q.bits){if(bit.value.isEmpty()){continue;}bool hit=false;
    if(bit.pattern){const auto matched=bit.pattern->match(lower);if(!matched.isValid()){return fail("Search regular expression exceeded its work limit");}hit=matched.hasMatch();}
    else{auto alternatives=bit.value.split('|',Qt::KeepEmptyParts);while(!alternatives.isEmpty()&&alternatives.back().isEmpty()){alternatives.removeLast();}for(const auto& part:alternatives){
      if(part.startsWith("tag:")){auto target=part.mid(4);target.replace('+',' ');target=truncateTag(target.toLower());
        for(const auto& tag:item.tags){if(tag.size()>512){return fail("Search tag exceeds its bound");}if(QString::fromUtf8(tag.data(),qsizetype(tag.size())).startsWith(target)){hit=true;break;}}
      }else{hit=lower.contains(part);}if(hit){break;}
    }}if(bit.negative?hit:!hit){return false;}any=true;
  }return any;
}
Result<std::vector<Item>> search(std::span<const Item> items,const SearchQuery& query,bool remote,std::int64_t nowMillis,std::size_t maximum,std::chrono::milliseconds budget){
  if(items.size()>MaximumItems||maximum>MaximumItems||nowMillis<0||budget.count()<=0||budget>std::chrono::seconds(1)){return fail("Search workload exceeds its bound");}
  const auto end=std::chrono::steady_clock::now()+budget;std::map<std::string,const Item*> unique;
  for(const auto& item:items){if(std::chrono::steady_clock::now()>=end){return fail("Related-content search exceeded its work budget");}const auto matched=query.matches(item,nowMillis);if(!matched){return std::unexpected(matched.error());}if(*matched){unique.insert_or_assign(item.hash.empty()?"private:"+item.key:item.hash,&item);}}
  std::vector<Item> out;out.reserve(unique.size());for(const auto& [key,item]:unique){(void)key;out.push_back(*item);}
  const auto cap=std::min(maximum,remote?std::size_t{30}:query.popular()?std::size_t{50}:MaximumItems);
  if(out.size()>cap){std::stable_sort(out.begin(),out.end(),[&](const Item& a,const Item& b){if(query.popular()){if(a.version!=b.version){return a.version>b.version;}return a.seeds()+a.leechers()>b.seeds()+b.leechers();}return a.randoms.size()>b.randoms.size();});out.resize(cap);}return out;
}
}
