// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_identity_view.hpp"
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
namespace biglybtc::ui {
NetworkIdentityView::NetworkIdentityView(QWidget* parent):QWidget(parent){setObjectName("statistics-current-network");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);auto* group=new QGroupBox(tr("Current public network"),this);auto* form=new QFormLayout(group);const char* names[]={"address","asn","provider","prefix","status"};const char* titles[]={"Public IP:","ASN:","Network operator:","BGP prefix:",""};for(std::size_t i=0;i<2;++i){for(std::size_t k=0;k<5;++k){auto* label=new QLabel(group);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);label->setObjectName(QString("network-ipv%1-%2").arg(i?6:4).arg(names[k]));labels_[i][k]=label;form->addRow(k==0?tr("IPv%1 public IP:").arg(i?6:4):tr(titles[k]),label);}}layout->addWidget(group);setSnapshot({});}
void NetworkIdentityView::setSnapshot(std::shared_ptr<const services::network_identity::Snapshot> value){for(std::size_t i=0;i<2;++i){const auto row=value?value->families[i]:services::network_identity::Family{};const bool available=value&&value->current&&!row.address.isEmpty();labels_[i][0]->setText(available?row.address:tr("Not detected yet"));labels_[i][1]->setText(available&&row.asn?"AS"+row.asn->number:tr("Unavailable"));labels_[i][2]->setText(available&&row.asn&&!row.asn->name.isEmpty()?row.asn->name:tr("Unavailable"));labels_[i][3]->setText(available&&row.asn?row.asn->prefix:tr("Unavailable"));labels_[i][4]->setText(row.status);for(auto* label:labels_[i]){label->setToolTip(row.cached?tr("Cached ASN information matched to this observed public address"):row.status);}}}
}
