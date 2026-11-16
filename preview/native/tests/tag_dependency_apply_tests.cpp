// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_dependency_apply.hpp>
#include <biglybtc/bencode.hpp>
#include <QFile>
#include <algorithm>
#include <QTest>
using namespace biglybtc;namespace apply=core::tags::dependency;
class TagDependencyApplyTests final:public QObject {Q_OBJECT
private slots:
 void actualOriginalDependencyControlOrder(){
  QFile file(QString::fromUtf8(TAG_DEPENDENCY_ORIGINAL_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));auto doc=core::Document::parse(file.readAll().toStdString());QVERIFY(doc);unsigned count=0;
  for(const auto& test:doc->find("cases")->children){
   std::vector<apply::Node> nodes;const auto& source=test.find("nodes")->children;
   for(const auto& n:source){apply::Node node;const auto mode=n.find("mode")->integer;node.mode={mode==0||mode==1,mode==0||mode==2,mode==3};node.enabled=n.find("enabled")->integer!=0;node.member=n.find("member")->integer!=0;node.mayAdd=test.find("recent")->integer!=std::int64_t(nodes.size());for(const auto& dependency:n.find("dependencies")->children){node.dependencies.push_back(std::size_t(dependency.integer));}nodes.push_back(std::move(node));}
   std::vector<std::size_t> checked;if(test.find("prechecked")->integer>=0){checked.push_back(std::size_t(test.find("prechecked")->integer));}
   auto machine=apply::Apply::create(std::move(nodes),0,test.find("check")->integer!=0,test.find("fresh")->integer!=0,checked);QVERIFY(machine);
   std::vector<std::string> trace;unsigned evaluations=0;
   for(unsigned budget=0;;++budget){QVERIFY(budget<1024);auto next=machine->next();QVERIFY(next);if(next->action==apply::Action::Finished){break;}
    const auto id=std::to_string(next->node);
    if(next->action==apply::Action::Evaluate){const auto& expression=source[next->node];const auto mask=expression.find("test_mask")->integer;const auto kind=expression.find("expression")->integer;bool any=false;for(std::size_t node=0;node<source.size();++node){any=any||((mask&(std::int64_t{1}<<node))!=0&&machine->member(node));}const bool value=kind==0?false:kind==1?true:kind==2?any:!any;trace.push_back("eval:"+id+":"+(value?"1":"0"));if(++evaluations==unsigned(test.find("stop_after")->integer)){machine->stopping();}QVERIFY(machine->accept(*next,value));}
    else{trace.push_back((next->action==apply::Action::Add?"add:":"remove:")+id);QVERIFY(machine->accept(*next,true));}
   }
   std::vector<std::string> expected;for(const auto& step:test.find("trace")->children){expected.emplace_back(step.bytes);}QCOMPARE(trace,expected);
   for(std::size_t node=0;node<source.size();++node){QCOMPARE(machine->member(node),bool(test.find("members")->children[node].integer));const auto& after=test.find("checked_after")->children;const bool present=std::any_of(after.begin(),after.end(),[&](const auto& entry){return entry.integer==std::int64_t(node);});QCOMPARE(machine->checked(node),present);}
   ++count;
  }QCOMPARE(count,137u);
 }
 void staleRecheckRepliesUnknownEvaluationAndBoundsCannotIssueEffects(){
  std::vector<apply::Node> graph(2);graph[0].dependencies={1};auto machine=apply::Apply::create(graph,0,true,false);QVERIFY(machine);
  auto first=machine->next();QVERIFY(first&&first->action==apply::Action::Evaluate);QCOMPARE(machine->next(),first);QVERIFY(machine->accept(*first,true));
  auto child=machine->next();QVERIFY(child&&child->node==1);QVERIFY(!machine->accept(*first,true));QVERIFY(machine->accept(*child,true));
  auto effect=machine->next();QVERIFY(effect&&effect->action==apply::Action::Add);QVERIFY(machine->accept(*effect,false));QVERIFY(!machine->member(1));
  auto recheck=machine->next();QVERIFY(recheck&&recheck->node==0&&recheck->sequence!=first->sequence);QVERIFY(!machine->accept(*first,true));machine->unavailable();QVERIFY(!machine->next());QVERIFY(!machine->accept(*recheck,true));QVERIFY(!machine->member(0));
  apply::Limits limits;limits.depth=1;auto bounded=apply::Apply::create(graph,0,true,false,{},limits);QVERIFY(bounded);auto request=bounded->next();QVERIFY(request);QVERIFY(bounded->accept(*request,true));QVERIFY(!bounded->next());QVERIFY(!bounded->member(0));
  graph[0].dependencies={1,1};QVERIFY(!apply::Apply::create(graph,0,true,false));
 }
};
QTEST_GUILESS_MAIN(TagDependencyApplyTests)
#include "tag_dependency_apply_tests.moc"
