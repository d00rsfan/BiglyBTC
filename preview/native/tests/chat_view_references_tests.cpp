// SPDX-License-Identifier: GPL-2.0-or-later
#include "../services/msgsync/view_references.hpp"
#include <iostream>
#include <stdexcept>
using biglybtc::services::msgsync::ViewReferences;
void require(bool v){if(!v){throw std::runtime_error("Runtime chat reference invariant");}}
int main(){try{using R=ViewReferences::Released;ViewReferences chat;require(!chat.shouldRetire());require(chat.release(99)==R::Unknown&&!chat.shouldRetire());require(!chat.acquire(0));require(chat.acquire(1)&&chat.acquire(1)&&chat.views().size()==1);require(chat.acquire(2));require(chat.release(1)==R::Retained);chat.keepAlive(true);require(chat.release(2)==R::Retained&&!chat.shouldRetire());require(chat.release(2)==R::Unknown);chat.keepAlive(false);require(chat.shouldRetire());ViewReferences bounded;for(unsigned n=1;n<=64;++n){require(bounded.acquire(n));}require(!bounded.acquire(65)&&bounded.acquire(64));require(bounded.release(1)==R::Retained&&bounded.acquire(65));std::cout<<"PASS original private clone/last-view/keep-alive runtime ownership and finite bounds\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
