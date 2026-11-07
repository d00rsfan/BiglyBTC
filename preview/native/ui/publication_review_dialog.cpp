#include "publication_review_dialog.hpp"
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
namespace biglybtc::ui {
PublicationReviewDialog::PublicationReviewDialog(QWidget* parent):QDialog(parent){setObjectName("publication-review-dialog");setWindowModality(Qt::WindowModal);auto* layout=new QVBoxLayout(this);text_=new QPlainTextEdit(this);text_->setObjectName("publication-review-details");text_->setReadOnly(true);layout->addWidget(text_);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);confirm_=buttons->button(QDialogButtonBox::Ok);confirm_->setObjectName("publication-review-confirm");buttons->button(QDialogButtonBox::Cancel)->setObjectName("publication-review-cancel");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(!frame_||!frame_->review||submitting_){return;}const auto frame=frame_;const auto revision=++revision_;submitting_=true;QPointer<PublicationReviewDialog> alive(this);confirm_->setEnabled(false);if(alive&&revision_==revision&&frame_==frame){emit confirmed({frame->key,frame->review->revision});}});connect(buttons,&QDialogButtonBox::rejected,this,&PublicationReviewDialog::reject);resize(680,360);}
bool PublicationReviewDialog::beginReview(PublicationPresentation packet){if(!packet||!packet->review||!packet->review->revision||packet->state!=PublicationState::ReviewReady){return false;}frame_=std::move(packet);const auto revision=++revision_;submitting_=false;QPointer<PublicationReviewDialog> alive(this);const auto frame=frame_;setWindowTitle(frame->review->title.left(256));if(!alive||revision_!=revision){return false;}text_->setPlainText(frame->review->details.left(16384));if(!alive||revision_!=revision){return false;}confirm_->setText(frame->review->confirmation.left(256));if(!alive||revision_!=revision){return false;}confirm_->setEnabled(true);return alive&&revision_==revision&&frame_==frame;}
void PublicationReviewDialog::invalidate(){frame_.reset();submitting_=false;++revision_;QDialog::reject();}
void PublicationReviewDialog::complete(){frame_.reset();submitting_=false;++revision_;QDialog::accept();}
void PublicationReviewDialog::reject(){if(!frame_){QDialog::reject();return;}const auto key=frame_->key;frame_.reset();submitting_=false;const auto revision=++revision_;QPointer<PublicationReviewDialog> alive(this);emit cancelled(key);if(alive&&revision_==revision&&!frame_){QDialog::reject();}}
}
