// SPDX-License-Identifier: GPL-2.0-or-later
#include "metasearch_magnet_dialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace biglybtc::ui {
MetasearchMagnetDialog::MetasearchMagnetDialog(QWidget* parent):QDialog(parent){
 setObjectName("metasearch-magnet-review");setWindowTitle(tr("Review Search Magnet"));setWindowModality(Qt::WindowModal);
 auto* outer=new QVBoxLayout(this);auto* form=new QFormLayout;
 const auto label=[&](const char* object){auto* value=new QLabel(this);value->setObjectName(QLatin1String(object));value->setTextFormat(Qt::PlainText);value->setWordWrap(true);value->setTextInteractionFlags(Qt::TextSelectableByMouse);return value;};
 identity_=label("metasearch-magnet-identity");name_=label("metasearch-magnet-name");source_=label("metasearch-magnet-source");query_=label("metasearch-magnet-query");
 form->addRow(tr("Torrent identity:"),identity_);form->addRow(tr("Name:"),name_);form->addRow(tr("Source:"),source_);form->addRow(tr("Search:"),query_);
 auto* pathRow=new QWidget(this);auto* pathLayout=new QHBoxLayout(pathRow);pathLayout->setContentsMargins(0,0,0,0);destination_=new QLineEdit(pathRow);destination_->setObjectName("metasearch-magnet-destination");destination_->setMaxLength(4096);browse_=new QPushButton(tr("Browse…"),pathRow);browse_->setObjectName("metasearch-magnet-browse");pathLayout->addWidget(destination_,1);pathLayout->addWidget(browse_);form->addRow(tr("Download folder:"),pathRow);
 network_=new QComboBox(this);network_->setObjectName("metasearch-magnet-network");network_->addItem(tr("Public"),static_cast<int>(TorrentIntakeNetwork::Public));form->addRow(tr("Network:"),network_);outer->addLayout(form);
 status_=label("metasearch-magnet-status");outer->addWidget(status_);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);confirm_=buttons->button(QDialogButtonBox::Ok);confirm_->setObjectName("metasearch-magnet-confirm");confirm_->setText(tr("Resolve Metadata and Add Stopped"));buttons->button(QDialogButtonBox::Cancel)->setObjectName("metasearch-magnet-cancel");outer->addWidget(buttons);
 connect(buttons,&QDialogButtonBox::accepted,this,&MetasearchMagnetDialog::confirm);connect(buttons,&QDialogButtonBox::rejected,this,&MetasearchMagnetDialog::reject);connect(destination_,&QLineEdit::textChanged,this,&MetasearchMagnetDialog::controls);connect(network_,&QComboBox::currentIndexChanged,this,&MetasearchMagnetDialog::controls);
 connect(browse_,&QPushButton::clicked,this,[this]{if(!frame_||busy_||pending_){return;}QPointer<MetasearchMagnetDialog> alive(this);const auto frame=frame_;const auto path=QFileDialog::getExistingDirectory(this,tr("Download folder"),destination_->text());if(alive&&frame_==frame&&!pending_&&!path.isEmpty()){destination_->setText(path);}});
 resize(660,360);controls();
}
bool MetasearchMagnetDialog::beginReview(MetasearchDownloadPresentation packet,QString directory){
 if(!packet||packet->state!=MetasearchDownloadState::MagnetReviewReady||!packet->magnetReview||!packet->magnetReview->revision||packet->magnetReview->identity.isEmpty()||pending_){return false;}
 ++revision_;const auto revision=revision_;frame_=std::move(packet);pending_=false;QPointer<MetasearchMagnetDialog> alive(this);const auto frame=frame_;
 const auto current=[&]{return alive&&revision_==revision&&frame_==frame;};
 identity_->setText(frame->magnetReview->identity.left(256));if(!current()){return false;}name_->setText(frame->magnetReview->name.left(2048));if(!current()){return false;}source_->setText(frame->magnetReview->source.left(2048));if(!current()){return false;}query_->setText(frame->magnetReview->query.left(1024));if(!current()){return false;}
 destination_->setText(std::move(directory));if(!current()){return false;}
 {QSignalBlocker blocked(network_);network_->setCurrentIndex(0);}
 if(!current()){return false;}status_->setText(tr("Nothing is saved or started until you confirm. Confirmation saves this Public magnet and requests its torrent metadata. The torrent will be added stopped. A confirmed pending magnet remains in the library when you start another search."));if(!current()){return false;}controls();return current();
}
void MetasearchMagnetDialog::controls(){
 QPointer<MetasearchMagnetDialog> alive(this);const auto revision=revision_;const bool active=frame_&&!busy_&&!pending_;
 destination_->setEnabled(active);if(!alive||revision_!=revision){return;}browse_->setEnabled(active);if(!alive||revision_!=revision){return;}network_->setEnabled(active);if(!alive||revision_!=revision){return;}
 const bool validNetwork=network_->currentData().toInt()==static_cast<int>(TorrentIntakeNetwork::Public);const auto path=destination_->text();confirm_->setEnabled(active&&validNetwork&&!path.isEmpty()&&!path.contains(QChar(0))&&path.toUtf8().size()<=4096);
}
void MetasearchMagnetDialog::setBusy(bool value){busy_=value;controls();}
void MetasearchMagnetDialog::confirm(){
 if(!frame_||!frame_->magnetReview||busy_||pending_||network_->currentData().toInt()!=static_cast<int>(TorrentIntakeNetwork::Public)||destination_->text().isEmpty()||destination_->text().contains(QChar(0))||destination_->text().toUtf8().size()>4096){return;}
 MetasearchMagnetSubmission command{frame_->key,frame_->magnetReview->revision,destination_->text(),TorrentIntakeNetwork::Public};pending_=true;const auto revision=++revision_;QPointer<MetasearchMagnetDialog> alive(this);status_->setText(tr("Confirming the current search result…"));if(!alive||revision_!=revision){return;}controls();if(alive&&revision_==revision){emit submitted(std::move(command));}
}
void MetasearchMagnetDialog::refuse(const MetasearchMagnetSubmission& command){if(!frame_||!frame_->magnetReview||command.key!=frame_->key||command.revision!=frame_->magnetReview->revision){return;}pending_=false;++revision_;QPointer<MetasearchMagnetDialog> alive(this);status_->setText(tr("This confirmation could not start. Try again after the current operation, or cancel and search again."));if(alive){controls();}}
void MetasearchMagnetDialog::complete(){frame_.reset();pending_=false;++revision_;QDialog::accept();}
void MetasearchMagnetDialog::invalidate(){frame_.reset();pending_=false;++revision_;QDialog::reject();}
void MetasearchMagnetDialog::reject(){if(!frame_){QDialog::reject();return;}const auto key=frame_->key;frame_.reset();pending_=false;const auto revision=++revision_;QPointer<MetasearchMagnetDialog> alive(this);emit cancelled(key);if(alive&&revision_==revision&&!frame_){QDialog::reject();}}
}
