// SPDX-License-Identifier: GPL-2.0-or-later
#include <biglybtc/tag_assignment_actions.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace a=biglybtc::core::tags::assignment;
namespace b=biglybtc::core;
int main(){try{
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
