// SPDX-License-Identifier: GPL-2.0-or-later
#include "sharing_values.hpp"
#include <QtTest>
namespace {
using namespace biglybtc;
std::string b(std::string_view value){return std::to_string(value.size())+":"+std::string(value);}
std::string row(std::string_view path="/owned/payload.bin",std::string_view cache="cache1/7.torrent",std::string_view tail=""){
 return "d10:attributesde4:file"+b(path)+"5:ifile"+b(cache)+"5:ihash16:0123456789abcdef5:propsd10:persistent4:truee4:typei1e"+std::string(tail)+"e";
}
std::string inventory(std::string_view entries){return "d9:resourcesl"+std::string(entries)+"e14:unknown.futured6:opaqueli-7e3:raweee";}
}
class SharingValuesTests final:public QObject {
 Q_OBJECT
private slots:
 void actualOriginalFingerprintVectors_data(){
  QTest::addColumn<QByteArray>("input");QTest::addColumn<QByteArray>("expected");
    QTest::newRow("byte-length-0")<<QByteArray::fromHex("")<<QByteArray::fromHex("D98C1DD404B2008F980980E97E42F8EC");
    QTest::newRow("byte-length-1")<<QByteArray::fromHex("0B")<<QByteArray::fromHex("D9FFC813033701778ECF01A765ACDE11");
    QTest::newRow("byte-length-55")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9")<<QByteArray::fromHex("04AA72D8A94DA273C54ACE9567E7AD18");
    QTest::newRow("byte-length-56")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE")<<QByteArray::fromHex("646735E27C6746580FE85D201B087997");
    QTest::newRow("byte-length-63")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01")<<QByteArray::fromHex("62B6754713FCA8782309F82FCD168237");
    QTest::newRow("byte-length-64")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC0126")<<QByteArray::fromHex("8240B6495BCCBBC449E0ACD38111C50B");
    QTest::newRow("byte-length-65")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B")<<QByteArray::fromHex("9819B53F799BC6D3AB26460B5F133227");
    QTest::newRow("byte-length-119")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577CA1C6EB10355A7FA4C9EE13385D82A7CCF1163B6085AACFF419")<<QByteArray::fromHex("60803001200A6B6242F6B40B0D3BD3F5");
    QTest::newRow("byte-length-120")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577CA1C6EB10355A7FA4C9EE13385D82A7CCF1163B6085AACFF4193E")<<QByteArray::fromHex("04AC21C55AE77F96C3761BA31325CA7D");
    QTest::newRow("byte-length-127")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577CA1C6EB10355A7FA4C9EE13385D82A7CCF1163B6085AACFF4193E6388ADD2F71C41")<<QByteArray::fromHex("AE9420C4CFE3AAED2E05E3AA90BBBBBA");
    QTest::newRow("byte-length-128")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577CA1C6EB10355A7FA4C9EE13385D82A7CCF1163B6085AACFF4193E6388ADD2F71C4166")<<QByteArray::fromHex("66E21A16C79866F9077AA92A3F638970");
    QTest::newRow("byte-length-129")<<QByteArray::fromHex("0B30557A9FC4E90E33587DA2C7EC11365B80A5CAEF14395E83A8CDF2173C6186ABD0F51A3F6489AED3F81D42678CB1D6FB20456A8FB4D9FE23486D92B7DC01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577CA1C6EB10355A7FA4C9EE13385D82A7CCF1163B6085AACFF4193E6388ADD2F71C41668B")<<QByteArray::fromHex("FB01E469AA5CD9DBD85F239CCDA8C3EF");
    QTest::newRow("fingerprint-12")<<QByteArray::fromHex("66696C652E62696E3A313730303030303030303132333A37")<<QByteArray::fromHex("2CCA1E245142B3B166E6DD72ED65673A");
    QTest::newRow("fingerprint-13")<<QByteArray::fromHex("CEBCF09F98802E62696E3A313730303030303030303132333A37")<<QByteArray::fromHex("BBB6AEE157737D97644DB336E94C9A89");
    QTest::newRow("fingerprint-14")<<QByteArray::fromHex("3A612E7478743A313730303030303030303132333A373A7A2E7478743A313730303030303030303939393A39")<<QByteArray::fromHex("DD9E7B4FF4A2A0DD3EB1EB5D3319C8D3");
    QTest::newRow("multiblock-directory-fingerprint")<<QByteArray::fromHex("3A66696C652D302E62696E3A313730303030303030303132333A3938373635343332313A66696C652D312E62696E3A313730303030303030303132333A3938373635343332313A66696C652D322E62696E3A313730303030303030303132333A3938373635343332313A66696C652D332E62696E3A313730303030303030303132333A3938373635343332313A66696C652D342E62696E3A313730303030303030303132333A3938373635343332313A66696C652D352E62696E3A313730303030303030303132333A3938373635343332313A66696C652D362E62696E3A313730303030303030303132333A3938373635343332313A66696C652D372E62696E3A313730303030303030303132333A3938373635343332313A66696C652D382E62696E3A313730303030303030303132333A3938373635343332313A66696C652D392E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31302E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31312E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31322E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31332E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31342E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31352E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31362E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31372E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31382E62696E3A313730303030303030303132333A3938373635343332313A66696C652D31392E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32302E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32312E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32322E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32332E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32342E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32352E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32362E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32372E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32382E62696E3A313730303030303030303132333A3938373635343332313A66696C652D32392E62696E3A313730303030303030303132333A3938373635343332313A66696C652D33302E62696E3A313730303030303030303132333A3938373635343332313A66696C652D33312E62696E3A313730303030303030303132333A393837363534333231")<<QByteArray::fromHex("8EA58ECA852723257F3CBA735B153F03");
 }
 void actualOriginalFingerprintVectors(){
  QFETCH(QByteArray,input);QFETCH(QByteArray,expected);
  const auto value=sharing::originalShareFingerprint(std::string_view(input.constData(),static_cast<std::size_t>(input.size())));QVERIFY(value);
  QCOMPARE(QByteArray::fromStdString(*value),expected);
 }
 void cacheNames_data(){
  QTest::addColumn<QString>("name");QTest::addColumn<bool>("accepted");
  QTest::newRow("first")<<QStringLiteral("cache1/0.torrent")<<true;
  QTest::newRow("last")<<QStringLiteral("cache1000/9223372036854775807.torrent")<<true;
  QTest::newRow("missing-directory")<<QStringLiteral("0.torrent")<<false;
  QTest::newRow("zero-directory")<<QStringLiteral("cache0/7.torrent")<<false;
  QTest::newRow("too-high-directory")<<QStringLiteral("cache1001/7.torrent")<<false;
  QTest::newRow("noncanonical-directory")<<QStringLiteral("cache01/7.torrent")<<false;
  QTest::newRow("overflow")<<QStringLiteral("cache1/9223372036854775808.torrent")<<false;
  QTest::newRow("absolute")<<QStringLiteral("/cache1/7.torrent")<<false;
  QTest::newRow("traversal")<<QStringLiteral("cache1/../7.torrent")<<false;
  QTest::newRow("empty-name")<<QStringLiteral("cache1/.torrent")<<false;
  QTest::newRow("nonnumeric-name")<<QStringLiteral("cache1/name.torrent")<<false;
 }
 void cacheNames(){QFETCH(QString,name);QFETCH(bool,accepted);QCOMPARE(sharing::supportedShareCacheName(name.toStdString()),accepted);}
 void preservesOpaqueRootAndUntouchedRows(){
  const auto originalRow=row("/owned/original.bin","cache2/8.torrent","14:future.private6:opaque");
  const auto original=inventory(originalRow);auto values=sharing::readSharingValues(original);QVERIFY(values);QCOMPARE(values->rows.size(),std::size_t{1});QVERIFY(values->rows.front().available);
  const auto appended=sharing::appendPersistentShare(*values,core::SavedShareKind::Directory,"/owned/bundle","cache3/9.torrent",std::string(16,'x'));QVERIFY(appended);
  auto reparsed=sharing::readSharingValues(*appended);QVERIFY(reparsed);QCOMPARE(reparsed->rows.size(),std::size_t{2});
  QVERIFY(appended->find(originalRow)!=std::string::npos);QVERIFY(appended->find("14:unknown.futured6:opaqueli-7e3:rawee")!=std::string::npos);
  const auto restored=sharing::eraseShare(*reparsed,reparsed->rows.back().selected);QVERIFY(restored);QCOMPARE(*restored,original);
 }
 void duplicateAndStaleSelectorsCannotMutate(){
  auto values=sharing::readSharingValues(inventory(row()));QVERIFY(values);
  QVERIFY(!sharing::appendPersistentShare(*values,core::SavedShareKind::File,"/owned/payload.bin","cache2/8.torrent",std::string(16,'x')));
  QVERIFY(!sharing::appendPersistentShare(*values,core::SavedShareKind::File,"/owned/other.bin","cache1/7.torrent",std::string(16,'x')));
  auto stale=values->rows.front().selected;stale.file="/owned/changed.bin";QVERIFY(!sharing::eraseShare(*values,stale));
  auto duplicate=sharing::readSharingValues(inventory(row()+row()));QVERIFY(duplicate);QCOMPARE(duplicate->rows.size(),std::size_t{2});
  QVERIFY(!duplicate->rows[0].available&&!duplicate->rows[1].available);QVERIFY(!sharing::eraseShare(*duplicate,duplicate->rows[0].selected));
 }
 void malformedListsAndDuplicateKeysAreRejected(){
  const auto malformed=inventory("i7e");auto projection=sharing::readSharingValues(malformed);
  if(projection){QCOMPARE(projection->source->source->originalBytes(),malformed);
    QVERIFY(!sharing::appendPersistentShare(*projection,core::SavedShareKind::File,"/owned/new.bin","cache2/8.torrent",std::string(16,'x')));}

  QVERIFY(!sharing::readSharingValues("d9:resourcesle9:resourceslee"));
  QVERIFY(!sharing::readSharingValues(inventory(row("/owned/x","cache1/7.torrent","4:typei2e"))));
 }
 void unsupportedModesStayVisibleWithoutMutation_data(){
  QTest::addColumn<QByteArray>("entry");
  auto item=row();const auto at=item.find("4:typei1e");item.replace(at,9,"4:typei3e");QTest::newRow("recursive-directory-contents")<<QByteArray::fromStdString(item);
  item=row();item.replace(item.find("10:persistent4:true"),std::string_view("10:persistent4:true").size(),"10:persistent5:false");QTest::newRow("nonpersistent")<<QByteArray::fromStdString(item);
  QTest::newRow("personal")<<QByteArray::fromStdString(row("/owned/payload.bin","cache1/7.torrent","7:per_key3:key"));
  QTest::newRow("relative-source")<<QByteArray::fromStdString(row("relative.bin"));
  QTest::newRow("unsafe-cache")<<QByteArray::fromStdString(row("/owned/payload.bin","../7.torrent"));
 }
 void unsupportedModesStayVisibleWithoutMutation(){QFETCH(QByteArray,entry);const auto raw=inventory(entry.toStdString());auto values=sharing::readSharingValues(raw);QVERIFY(values);QCOMPARE(values->rows.size(),std::size_t{1});QVERIFY(!values->rows.front().available);QVERIFY(!values->rows.front().refusal.empty());QVERIFY(!sharing::eraseShare(*values,values->rows.front().selected));QCOMPARE(values->source->source->originalBytes(),raw);}
};
QTEST_GUILESS_MAIN(SharingValuesTests)
#include "sharing_values_tests.moc"
