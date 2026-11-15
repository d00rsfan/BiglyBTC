// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../services/azureus_key_block/state.hpp"
#include <QFile>
namespace generated_key_blocks {
namespace block=biglybtc::services::azureus_key_block;
struct Data {
 std::string modulus,exponent,request,signature;
 Data(){QFile file(QString::fromUtf8(KEY_BLOCK_POLICY_FIXTURE));if(!file.open(QIODevice::ReadOnly)){throw std::runtime_error("Generated signed-block fixture");}while(!file.atEnd()){const auto f=file.readLine().trimmed().split('\t');if(f[0]=="test-key"){modulus=f[1].toStdString();exponent=f[2].toStdString();}else if(f[1]=="operation"){request=QByteArray::fromHex(f[5]).toStdString();signature=QByteArray::fromHex(f[6]).toStdString();break;}}}
 block::Verifier verifier()const{return [n=modulus,e=exponent](auto req,auto sig){return block::verifySignatureStatus(req,sig,{n,e});};}
 std::shared_ptr<block::State> state()const{return std::make_shared<block::State>(verifier());}
};
}
