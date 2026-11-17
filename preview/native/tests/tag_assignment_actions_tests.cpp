// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_assignment_actions.hpp>
#include <biglybtc/bencode.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace a=biglybtc::core::tags::assignment;
namespace b=biglybtc::core;
namespace {
void need(bool value,const char* error){if(!value){throw std::runtime_error(error);}}
std::string text(a::Effect effect){
 switch(effect){
 case a::Effect::Queue:return "queue";case a::Effect::Resume:return "resume";
 case a::Effect::Stop:return "stop:70";case a::Effect::QueueAfterStop:return "stop:75";
 case a::Effect::Pause:return "pause:1";case a::Effect::ForceStart:return "force:1";
 case a::Effect::CancelForceStart:return "force:0";case a::Effect::CheckMaximum:return "check-max";
 }throw std::runtime_error("Unknown assignment effect");
}
void actualOriginal(){
 std::ifstream file(TAG_ASSIGNMENT_ORIGINAL_FIXTURE,std::ios::binary);need(bool(file),"Generated action fixture missing");
 auto source=b::Document::parse({std::istreambuf_iterator<char>(file),{}});need(bool(source),"Generated action fixture unreadable");
 unsigned rows=0;
 for(const auto& row:source->find("rows")->children){
  std::int32_t state=0;const auto token=row.find("state")->bytes;
  const auto parsed=std::from_chars(token.data(),token.data()+token.size(),state);need(parsed.ec==std::errc{}&&parsed.ptr==token.data()+token.size(),"Original state text differs");
  const auto result=a::plan(row.find("enabled")->integer,state,row.find("metadata")->integer,row.find("destroyed")->integer);
  need(bool(result)&&result->count==row.find("queued")->integer,"Original queue selection differs");
  std::vector<std::string> trace;
  for(const auto task:result->tasks()){
   const auto resolved=a::resolve(task,row.find("execution")->integer);need(bool(resolved),"Original queued action unavailable");
   for(const auto effect:resolved->effects()){trace.push_back(text(effect));}
  }
  const auto& expected=row.find("trace")->children;need(trace.size()==expected.size(),"Original action trace size differs");
  for(unsigned at=0;at<trace.size();++at){need(trace[at]==expected[at].bytes,"Original action order/precedence differs");}
  ++rows;
 }
 need(rows==1795,"Actual source action rows lost");
}
void invalidCapabilities(){
 for(const auto mask:{1U,32U,256U,65536U,0xffffffffU}){
  need(!a::plan(mask,70,false,false),"Unsupported effect was silently ignored");
  need(a::plan(mask,70,true,false)->count==0&&a::plan(mask,70,false,true)->count==0,"Destroyed/metadata guard queued a task");
  need(!a::resolve(a::Task::StartOrResume,mask),"Unknown current effect mask accepted");
 }
 need(!a::resolve(static_cast<a::Task>(99),0),"Invalid task accepted");
}
}
int main(){try{actualOriginal();invalidCapabilities();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
 std::cout<<"PASS1795 exact pinned lifecycle-action traces;precedence,async flag changes,metadata/destroyed guards and unsupported effect refusal\n";
}
