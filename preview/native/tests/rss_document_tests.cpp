// SPDX-License-Identifier: GPL-2.0-or-later
#include "rss_document.hpp"
#include <iostream>
#include <stdexcept>
namespace s=biglybtc::search;
namespace {
void check(bool value,const char* message){if(!value){throw std::runtime_error(message);}}
s::RssDocument read(const char* xml,s::RssContext context={},s::RssLimits limits={}){
  auto result=s::parseRssDocument(QByteArray(xml),std::move(context),limits);check(bool(result),"Expected bounded RSS document");return std::move(*result);
}
void originalNodeAndHtmlValues(){
  const auto doc=read(R"(<rss><channel><title>Channel</title><item><title>A<b>ignored child value</b>C</title><guid> urn:one </guid><enclosure type="application/x-bittorrent" url="/a.torrent" length="1048575"/></item><item><title><![CDATA[ A&nbsp;<b> title </b> ]]></title><contentlength>2 MiB</contentlength></item></channel></rss>)",{QStringLiteral("https://engine.invalid"),QStringLiteral("https://engine.invalid/base/")});
  check(!doc.atom&&doc.channels.size()==1&&doc.results.size()==2,"RSS channels/items lost");
  check(doc.results[0].title==QStringLiteral("AC")&&doc.results[0].uid==QStringLiteral("urn:one"),"Original immediate text/first UID semantics");
  check(doc.results[0].torrentLink==QStringLiteral("https://engine.invalid/a.torrent")&&!doc.results[0].size,"Enclosure threshold/reconstruction changed");
  check(doc.results[1].title==QStringLiteral("A\u00a0 title")&&doc.results[1].size==2097152,"Pinned HTML40/binary size semantics");
  // Owning raw32 setter text is distinct from the canonical ASCII subset
  // that may synthesize a magnet; Unicode case folding must not widen it.
  const auto raw=QString(31,u'A')+QChar(0x017f);
  auto unicode=s::parseRssDocument((QStringLiteral("<rss><channel><item><infohash>")+raw+QStringLiteral("</infohash></item></channel></rss>")).toUtf8());
  check(unicode&&unicode->results[0].hash==raw&&unicode->results[0].torrentLink&&unicode->results[0].torrentLink->isEmpty(),"Unicode case folding widened canonical ASCII hash synthesis");
}
void originalRelativeAndOrderedLinks(){
  const auto doc=read(R"(<rss><channel><link>https://feed.invalid/root/</link><item><title>One</title><link>/details</link><enclosure type="application/x-bittorrent" url="first.torrent"/><vuze:downloadurl xmlns:vuze="urn:vuze">/last.torrent</vuze:downloadurl></item></channel></rss>)",{QStringLiteral("https://engine.invalid"),QStringLiteral("https://engine.invalid/base/")});
  check(doc.results[0].detailsLink==QStringLiteral("https://feed.invalid/root/details"),"Original channel concatenation replaced by URL resolution");
  check(doc.results[0].torrentLink==QStringLiteral("https://engine.invalid/last.torrent")&&!doc.results[0].headUnavailable,"Ordered Vuze override lost");
  const auto atom=read(R"(<feed xmlns="urn:atom"><title>A</title><entry><title>Atom</title><id> urn:id </id><published>2026-01-02T03:04:05Z</published><link type="application/x-bittorrent" href="https://feed.invalid/first"/><content type="application/x-bittorrent" src="https://feed.invalid/last"/></entry></feed>)");
  check(atom.atom&&atom.results[0].torrentLink==QStringLiteral("https://feed.invalid/last")&&atom.results[0].uid==QStringLiteral("urn:id"),"Atom href/content/UID order changed");
  check(atom.results[0].publicationText==QStringLiteral("2026-01-02T03:04:05Z"),"Raw date was fabricated or lost");
}
void reconstructedNestedLinkConditions(){
  const auto missing=read(R"(<rss><channel><item><torrent><magnetURI/><infoHash>0000000000000000000000000000000000000000</infoHash></torrent></item></channel></rss>)");
  const auto magnet=QStringLiteral("magnet:?xt=urn:btih:")+QString(32,u'A');
  check(missing.results[0].torrentLink==magnet,"Empty nested raw setter suppressed original explicit hash synthesis");
  const auto base=QStringLiteral("https://engine.invalid/base/");
  const auto existing=read(R"(<rss><channel><item><title>magnet:unparsed</title><enclosure type="application/x-bittorrent" url=""/><torrent><magnetURI>magnet:?xt=urn:btih:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB</magnetURI><infoHash>0000000000000000000000000000000000000000</infoHash></torrent></item></channel></rss>)",{{},base});
  check(existing.results[0].torrentLink==base&&existing.results[0].hash==QString(32,u'A')&&existing.issues.empty(),"Raw empty setter overrode nonempty original reconstructed download link");
  const auto initiallyNull=read(R"(<rss><channel><item><torrent><magnetURI/><infoHash>0000000000000000000000000000000000000000</infoHash></torrent></item></channel></rss>)",{{},base});
  check(initiallyNull.results[0].torrentLink==magnet,"Nested local raw dlink was incorrectly reconstructed after its empty setter");
  const auto uncertain=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><torrent><magnetURI/><infoHash>0000000000000000000000000000000000000000</infoHash></torrent></item></channel></rss>)");
  check(uncertain.results[0].headUnavailable&&!uncertain.results[0].torrentLink&&uncertain.results[0].hash==QString(32,u'A'),"Conditional nested hash cleared unresolved real HEAD branch");
}
void realHeadDependencyIsUnavailable(){
  const auto doc=read(R"(<rss><channel><item><title>One</title><link>https://feed.invalid/page</link><infohash>0000000000000000000000000000000000000000</infohash></item></channel></rss>)");
  check(doc.results[0].headUnavailable&&!doc.results[0].torrentLink&&doc.results[0].hash==QString(32,u'A'),"HEAD result guessed from extension or hash");
  const auto ordered=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><enclosure type="application/x-bittorrent" url="https://feed.invalid/final.torrent"/></item></channel></rss>)");
  check(!ordered.results[0].headUnavailable&&ordered.results[0].torrentLink==QStringLiteral("https://feed.invalid/final.torrent"),"Later unconditional enclosure failed to settle link");
}
void dependentDetailUrlRemainsUnavailable(){
  const auto comment=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><comments>https://feed.invalid/comments</comments><infohash>0000000000000000000000000000000000000000</infohash></item></channel></rss>)");
  check(comment.results[0].headUnavailable&&!comment.results[0].torrentLink&&!comment.results[0].detailsLink,"Unavailable HEAD guessed final comments-versus-page CDP");
  bool diagnosed=false;for(const auto& issue:comment.issues){if(issue.field==QStringLiteral("detailsLink")&&issue.code==s::RssIssueCode::UnavailableHead){diagnosed=true;}}
  check(diagnosed,"Unavailable final CDP lacks its provider diagnostic");
  const auto guid=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><guid>https://feed.invalid/guid</guid><infohash>0000000000000000000000000000000000000000</infohash></item></channel></rss>)");
  check(guid.results[0].headUnavailable&&!guid.results[0].detailsLink,"Unavailable HEAD guessed final GUID-versus-page CDP");
  const auto fixed=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><comments>https://feed.invalid/comments</comments><enclosure type="application/x-bittorrent" url="https://feed.invalid/final.torrent"/></item></channel></rss>)");
  check(!fixed.results[0].headUnavailable&&fixed.results[0].detailsLink==QStringLiteral("https://feed.invalid/page"),"Later unconditional link failed to preserve known final CDP");
  const auto initial=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><infohash>0000000000000000000000000000000000000000</infohash></item></channel></rss>)");
  check(initial.results[0].headUnavailable&&initial.results[0].detailsLink==QStringLiteral("https://feed.invalid/page"),"Known CDP without any replacement was needlessly removed");
  const auto title=read(R"(<rss><channel><item><title>magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA</title><vuze:rank xmlns:vuze="urn:vuze">0</vuze:rank><link>https://feed.invalid/page</link><comments>https://feed.invalid/comments</comments></item></channel></rss>)");
  check(!title.results[0].headUnavailable&&!title.results[0].detailsLink&&!title.results[0].torrentLink,"Unsupported title-magnet choice guessed dependent CDP");
}
void inferredMagnetHashRequiresSettledDownloadLink(){
  const auto conditional=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><torrent><magnetURI>magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA</magnetURI></torrent></item></channel></rss>)");
  check(conditional.results[0].headUnavailable&&!conditional.results[0].torrentLink&&!conditional.results[0].hash,"Unresolved HEAD exported a hash from the conditional nested magnet");
  const auto independent=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><torrent><magnetURI>magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA</magnetURI></torrent><infohash>BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB</infohash></item></channel></rss>)");
  check(independent.results[0].headUnavailable&&!independent.results[0].torrentLink&&independent.results[0].hash==QString(32,u'B'),"Unresolved link choice suppressed an independent explicit hash");
  const auto settled=read(R"(<rss><channel><item><link>https://feed.invalid/page</link><torrent><magnetURI>magnet:?xt=urn:btih:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA</magnetURI></torrent><enclosure type="application/x-bittorrent" url="magnet:?xt=urn:btih:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"/></item></channel></rss>)");
  check(!settled.results[0].headUnavailable&&settled.results[0].torrentLink==QStringLiteral("magnet:?xt=urn:btih:")+QString(32,u'B')&&settled.results[0].hash==QString(32,u'B'),"Later unconditional magnet failed to settle inferred hash");
}
void trackerCountsAndFieldPrecedence(){
  const auto doc=read(R"(<rss><channel><item><torrent><infoHash>0000000000000000000000000000000000000000</infoHash><contentLength>12345678</contentLength><trackers><group><tracker seeds="4" peers="6"/><tracker seeds="8" peers="2"/></group></trackers></torrent><attr name="seeders" value="99"/></item><item><vuze:superseeds xmlns:vuze="urn:vuze">1</vuze:superseeds><seeds>9</seeds><peers>8</peers></item><item><seeds>7</seeds><peers>50</peers><leechers>3</leechers></item></channel></rss>)");
  check(doc.results[0].seeds==4&&doc.results[0].peers==6&&doc.results[0].size==12345678,"Tracker first maximum/attr priority changed");
  check(doc.results[0].hash==QString(32,u'A')&&doc.results[0].torrentLink==QStringLiteral("magnet:?xt=urn:btih:")+QString(32,u'A'),"Original explicit hash synthesis lost");
  check(!doc.results[1].seeds&&!doc.results[1].peers,"Superseed marker invented ordinary peer counts");
  check(doc.results[2].seeds==7&&doc.results[2].peers==3,"Original leechers precedence lost");
  const auto overflow=read(R"(<rss><channel><item><torrent><trackers><g><tracker seeds="2147483647" peers="1"/><tracker seeds="1" peers="1"/></g></trackers></torrent></item></channel></rss>)");
  check(overflow.results[0].seeds==1&&overflow.results[0].peers==1,"Original signed32 total wrap changed");
}
void owningLimitsAndForbiddenXml(){
  QByteArray bytes("<rss><channel><item><title>kept</title></item></channel></rss>");auto parsed=s::parseRssDocument(bytes);check(bool(parsed),"Owned parser input refused");bytes.fill('x');check(parsed->results[0].title==QStringLiteral("kept")&&parsed->source.startsWith("<rss>"),"Borrowed XML/result lifetime");
  const auto dtd=s::parseRssDocument(QByteArray(R"(<!DOCTYPE rss [<!ENTITY external SYSTEM "file:///not-a-fixture">]><rss/>)"));check(!dtd&&dtd.error()==s::RssError::ForbiddenDtd,"DTD accepted or resolved");
  check(!s::parseRssDocument(QByteArray("<rss><broken></rss>")),"Malformed XML accepted");
  auto limits=s::RssLimits{};limits.items=0;check(!s::parseRssDocument(QByteArray("<rss><channel><item/></channel></rss>"),{},limits),"Item cap ignored");
  limits={};limits.depth=1;check(!s::parseRssDocument(QByteArray("<rss><channel/></rss>"),{},limits),"Depth cap ignored");
  limits={};++limits.bytes;check(!s::parseRssDocument(QByteArray("<rss/>"),{},limits),"Raised cap accepted");
}
}
int main(){try{originalNodeAndHtmlValues();originalRelativeAndOrderedLinks();reconstructedNestedLinkConditions();realHeadDependencyIsUnavailable();dependentDetailUrlRemainsUnavailable();inferredMagnetHashRequiresSettledDownloadLink();trackerCountsAndFieldPrecedence();owningLimitsAndForbiddenXml();std::cout<<"PASS bounded original RSS document values\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
