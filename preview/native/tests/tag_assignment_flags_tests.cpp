// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_assignment_actions.hpp>
#include <biglybtc/saved_download_tag_edits.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace a=biglybtc::core::tags::assignment;
namespace b=biglybtc::core;
void actualSetterProjection(){
 std::ifstream file(TAG_ASSIGNMENT_SETTER_ORIGINAL_FIXTURE,std::ios::binary);if(!file){throw std::runtime_error("Generated original setter fixture missing");}
 auto fixture=b::Document::parse({std::istreambuf_iterator<char>(file),{}});if(!fixture){throw std::runtime_error("Generated original setter fixture invalid");}unsigned cases=0;
 for(const auto& row:fixture->find("rows")->children){const std::string before(row.find("before")->bytes),after(row.find("after")->bytes);
  const std::string root="d1:3d1:1"+before+"ee";const auto action=static_cast<std::uint32_t>(row.find("action")->integer);
  auto native=b::editSavedDownloadTags(root,b::SavedTagSetAssignmentAction{1,action,row.find("enabled")->integer==1});
  auto original=b::Document::parse(after);if(!native||!original||native->changed!=(before!=after)||row.find("metadata")->integer!=1){throw std::runtime_error("Original setter changed/noop boundary differs");}
  const auto& record=*native->value.source->find("3")->find("1");const auto& expected=original->root();const auto* config=record.find("c");const auto* target=expected.find("c");
  if(!config||!target||config->children.size()!=target->children.size()){throw std::runtime_error("Original setter altered other fields");}
  for(std::size_t i=0;i<target->children.size();i+=2){const auto* actual=config->find(target->children[i].bytes);const auto& value=target->children[i+1];
    if(!actual||actual->type!=value.type||(value.type==b::Node::Type::Integer?actual->integer!=value.integer:actual->bytes!=value.bytes)){throw std::runtime_error("Original assignment setter value or unknown field differs");}}
  for(const auto key:{"n","o","p"}){const auto* a=record.find(key);const auto* v=expected.find(key);if(!a||!v||a->type!=v->type||a->bytes!=v->bytes||a->children.size()!=v->children.size()){throw std::runtime_error("Action setter changed membership/name data");}}
  auto noop=b::editSavedDownloadTags(native->value.source->originalBytes(),b::SavedTagSetAssignmentAction{1,action,row.find("enabled")->integer==1});if(!noop||noop->changed){throw std::runtime_error("Canonical setter no-op wrote again");}++cases;
 }
 if(cases!=126){throw std::runtime_error("Original setter cases lost");}
 for(unsigned action:{0,1,3,32,65535}){if(b::editSavedDownloadTags("d1:3d1:1d1:n1:Teee",b::SavedTagSetAssignmentAction{1,action,true})){throw std::runtime_error("Invalid action accepted");}}
 std::cout<<"PASS126 actual original single action setter cases;exact integer rewrite,metadata-only,no-op and unknown/member preservation\n";
}
int main(){try{
 actualSetterProjection();
 std::ifstream file(TAG_ASSIGNMENT_FLAGS_ORIGINAL_FIXTURE,std::ios::binary);if(!file){throw std::runtime_error("Generated action flag fixture missing");}
 auto source=b::Document::parse({std::istreambuf_iterator<char>(file),{}});if(!source){throw std::runtime_error("Generated action flag fixture unreadable");}
 unsigned rows=0;
 for(const auto& row:source->find("rows")->children){
  const std::string raw(row.find("record")->bytes);auto record=b::Document::parse(raw);if(!record){throw std::runtime_error("Original saved flag record unreadable");}
  const auto actual=a::readSaved(record->root(),row.find("type")->integer);
  if(actual.supported!=row.find("supported")->integer||actual.enabled!=row.find("enabled")->integer){throw std::runtime_error("Actual original getters differ at row "+std::to_string(rows)+" type="+std::to_string(row.find("type")->integer)+" supported="+std::to_string(actual.supported)+" expected="+std::to_string(row.find("supported")->integer)+" enabled="+std::to_string(actual.enabled)+" expected="+std::to_string(row.find("enabled")->integer));}
  if(actual.lifecycleOnly()!=((actual.enabled&~a::LifecycleMask)==0)||record->serialize()!=raw){throw std::runtime_error("Read-only projection changed source or classified effects incorrectly");}
  ++rows;
 }
 if(rows!=689){throw std::runtime_error("Original flag cases lost");}
 // A numeric getter is not a blanket policy for unknown properties. This
 // projection ignores them and never creates a membership/edit/actor grant.
 for(const auto raw:{"de","d1:c1:xe","d1:cd5:pp.02i1e6:pp.999i1eee","d1:cd4:pp.2i1e5:pp.32i1eee"}){
  auto record=b::Document::parse(raw);if(!record){throw std::runtime_error("Generated mixed flag record invalid");}
  const auto actual=a::readSaved(record->root(),3);
  if(record->serialize()!=raw){throw std::runtime_error("Unknown property was edited");}
  if(actual.enabled&&(actual.lifecycleOnly()||a::plan(actual.enabled,70,false,false))){throw std::runtime_error("Mixed script effect silently became lifecycle-only");}
 }
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
 std::cout<<"PASS689 actual supported/enabled getter cases;exact Long1 flags,default/type/malformed/boundary values,all128 lifecycle combinations and unchanged source bytes\n";
}
