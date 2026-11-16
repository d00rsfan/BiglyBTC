// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/download_state_tags.hpp>
#include <biglybtc/download_state_names.hpp>
#include <biglybtc/stock_tag_names.hpp>
#include <biglybtc/bencode.hpp>
#include <QTest>
#include <QFile>
using namespace biglybtc;
namespace state=core::tags::download_state;
class DownloadStateTagTests final:public QObject {
  Q_OBJECT
private slots:
  void allStockTagKeysMatchActualOriginalMessageText(){
    QFile file(QString::fromUtf8(STOCK_TAG_NAMES_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));
    auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);unsigned names=0;
    for(const auto& row:doc->find("rows")->children){const auto& values=row.find("names")->children;
      for(std::size_t i=0;i<values.size();i+=2){
        auto actual=core::tags::stockTagName(row.find("locale")->bytes,QString::fromUtf8(values[i].bytes.data(),qsizetype(values[i].bytes.size())).toStdU16String());QVERIFY(actual);
        QCOMPARE(*actual,QString::fromUtf8(values[i+1].bytes.data(),qsizetype(values[i+1].bytes.size())).toStdU16String());++names;
      }
    }
    QCOMPARE(names,3894u);QVERIFY(!core::tags::stockTagName("",u"tag.name.product.updates"));
    QVERIFY(!core::tags::stockTagName("zz_ZZ",u"tag.name.product.updates"));
    QVERIFY(!core::tags::stockTagName("en_US",u"tag.missing.generated"));
    QVERIFY(!core::tags::stockTagName("EN_US",u"tag.name.product.updates"));
  }
  void actualOriginalLocalizedStockNames(){
    QFile file(QString::fromUtf8(DOWNLOAD_STATE_LABEL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));
    auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);unsigned names=0;
    for(const auto& row:doc->find("rows")->children){
      const auto lookup=core::tags::stockDownloadStateNames(row.find("locale")->bytes);QVERIFY(lookup);
      const auto& labels=row.find("names")->children;QCOMPARE(labels.size(),lookup->size());
      for(unsigned id=0;id<15;++id){const auto& label=labels[id].bytes;QCOMPARE((*lookup)[id],QString::fromUtf8(label.data(),qsizetype(label.size())).toStdU16String());++names;}
    }
    QCOMPARE(names,990u);QVERIFY(!core::tags::stockDownloadStateNames("zz_ZZ"));
    QVERIFY(!core::tags::stockDownloadStateNames(""));QVERIFY(!core::tags::stockDownloadStateNames("EN"));
  }
  void actualOriginalTransitionsActivityAndOperationCallbacks(){
    QFile file(QString::fromUtf8(DOWNLOAD_STATE_ORIGINAL_FIXTURE));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto original=core::Document::parse(file.readAll().toStdString());QVERIFY(original);
    const auto* cases=original->find("cases");QVERIFY(cases);
    unsigned steps=0;
    for(const auto& test:cases->children){
      state::State value;
      for(const auto& step:test.find("steps")->children){
        const auto& action=step.find("action")->bytes;
        const auto& args=step.find("args")->children;
        const auto arg=[&](unsigned index){return args[index].integer;};
        bool activeWrite=false;
        if(action=="state"){state::stateChanged(value,{int(arg(0)),bool(arg(1)),bool(arg(2)),bool(arg(3))});}
        else if(action=="activity"){activeWrite=state::updateActive(value,{arg(0),arg(1),arg(2),arg(3),bool(arg(4))});}
        else if(action=="operation"){state::operation(value,int(arg(0)),bool(arg(1)),bool(arg(2)));}
        else if(action=="delete"){state::removeInitiated(value);}
        else if(action=="remove"){state::remove(value);}
        else{QFAIL("Unknown original action");}
        const auto context=QByteArray(test.find("name")->bytes.data(),qsizetype(test.find("name")->bytes.size()))+":"+QByteArray::number(steps);
        const auto& members=step.find("members")->children;QCOMPARE(members.size(),value.members.size());
        for(unsigned id=0;id<15;++id){QVERIFY2(value.members[id]==bool(members[id].integer),context.constData());}
        QCOMPARE(value.main?static_cast<int>(*value.main):-1,int(step.find("main")->integer));
        QCOMPARE(value.complete?int(*value.complete):-1,int(step.find("complete")->integer));
        QCOMPARE(value.activityPrefix.has_value(),bool(step.find("active_prefix_known")->integer));
        QCOMPARE(value.activityPrefix.value_or(0),step.find("active_prefix")->integer);
        QCOMPARE(activeWrite,bool(step.find("active_write")->integer));++steps;
      }
    }
    QCOMPARE(cases->children.size(),std::size_t{259});QCOMPARE(steps,485u);
  }
};
QTEST_GUILESS_MAIN(DownloadStateTagTests)
#include "download_state_tag_tests.moc"
