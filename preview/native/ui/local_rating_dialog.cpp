#include "local_rating_dialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QVBoxLayout>
#include <limits>

namespace biglybtc::ui {
namespace {
constexpr qsizetype MaximumComment=1024*1024;
bool validRequest(const LocalRatingRequest& value) {
  static const QRegularExpression hash(QStringLiteral("\\A(?:[0-9A-Fa-f]{40}|[0-9A-Fa-f]{64})\\z"));
  return value.session && value.request && value.revision && value.target.generation
      && hash.match(value.target.hash).hasMatch();
}
}
LocalRatingDialog::LocalRatingDialog(LocalRatingRequest request,QWidget* parent)
    : QDialog(parent), request_(std::move(request)) {
  setWindowTitle(tr("My rating")); setObjectName(QStringLiteral("local-rating-dialog")); resize(500,330);
  auto* layout=new QVBoxLayout(this);
  auto* scope=new QLabel(tr("Save your rating and comment for this torrent. Distributed rating publication is not available yet."),this);
  scope->setWordWrap(true); layout->addWidget(scope);
  auto* form=new QFormLayout;
  score_=new QComboBox(this); score_->setObjectName(QStringLiteral("local-rating-score"));
  score_->setAccessibleName(tr("My rating"));
  score_->addItem(tr("Not rated"),0);
  for(int i=1;i<=5;++i) { score_->addItem(tr("%1 of 5").arg(i),i); }
  comment_=new QPlainTextEdit(this); comment_->setObjectName(QStringLiteral("local-rating-comment"));
  comment_->setAccessibleName(tr("My comment"));
  form->addRow(tr("&Rating"),score_); form->addRow(tr("&Comment"),comment_); layout->addLayout(form);
  status_=new QLabel(validRequest(request_)?tr("Loading my rating…"):tr("This torrent selection is no longer available."),this);
  status_->setObjectName(QStringLiteral("local-rating-status")); status_->setWordWrap(true); layout->addWidget(status_);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Close,this);
  save_=buttons->button(QDialogButtonBox::Save); save_->setObjectName(QStringLiteral("local-rating-save"));
  layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(save_,&QPushButton::clicked,this,&LocalRatingDialog::submit);
  connect(score_,qOverload<int>(&QComboBox::currentIndexChanged),this,&LocalRatingDialog::editChanged);
  connect(comment_,&QPlainTextEdit::textChanged,this,&LocalRatingDialog::editChanged);
  updateButtons();
}
bool LocalRatingDialog::controls() const { return score_ && comment_ && status_ && save_; }
void LocalRatingDialog::showEvent(QShowEvent* event) {
  QPointer<LocalRatingDialog> self(this); QDialog::showEvent(event);
  if(!self || requested_ || received_ || !validRequest(request_)) { return; }
  requested_=true;
  QMetaObject::invokeMethod(this,[this] {
    if(!isVisible() || isMinimized()) { requested_=false; return; }
    emit readRequested(request_); // Final access; receiver may delete the dialog.
  },Qt::QueuedConnection);
}
void LocalRatingDialog::updateButtons() {
  if(!controls()) { return; }
  const auto revision=revision_; QPointer<LocalRatingDialog> self(this);
  score_->setEnabled(loaded_ && !busy_);
  if(!self || !controls() || revision_!=revision) { return; }
  comment_->setEnabled(loaded_ && !busy_ && score_->currentData().toInt()!=0);
  if(!self || !controls() || revision_!=revision) { return; }
  const auto text=comment_->toPlainText(); bool number=false;
  const auto score=score_->currentData().toInt(&number);
  const bool dirty=number && (score!=originalScore_ || text!=originalComment_);
  save_->setEnabled(loaded_ && !busy_ && number && score<=5 && text.size()<=MaximumComment && dirty);
}
void LocalRatingDialog::setPresentation(LocalRatingPresentation packet) {
  if(!packet || packet->identity!=request_ || !validRequest(request_) || received_ || busy_ || !controls()) { return; }
  if(!packet->failure.isEmpty()) {
    if(packet->snapshot) { return; }
    received_=true; ++revision_; status_->setText(packet->failure); return;
  }
  if(!packet->snapshot || packet->snapshot->attributesRevision.size()!=32
      || packet->snapshot->score>5 || packet->snapshot->comment.size()>MaximumComment) { return; }
  // Copy every accepted fact before calling Qt setters. Caller aliases are not
  // retained, and direct property callbacks may delete/reenter the widget.
  const auto values=*packet->snapshot;
  token_=values.attributesRevision; originalScore_=values.score; originalComment_=values.comment;
  received_=true; loaded_=true; rendering_=true; const auto revision=++revision_;
  QPointer<LocalRatingDialog> self(this);
  if(values.score<0) {
    score_->addItem(tr("Saved rating: %1").arg(values.score),values.score);
    if(!self || !controls() || revision_!=revision) { return; }
  }
  const auto scoreBlocked=score_->blockSignals(true);
  score_->setCurrentIndex(score_->findData(values.score));
  if(!self || !controls() || revision_!=revision) { return; }
  score_->blockSignals(scoreBlocked);
  const auto commentBlocked=comment_->blockSignals(true);
  comment_->setPlainText(values.comment);
  if(!self || !controls() || revision_!=revision) { return; }
  comment_->blockSignals(commentBlocked);
  status_->setToolTip(tr("These are your local azrating attributes. No distributed rating write or lookup is performed."));
  if(!self || !controls() || revision_!=revision) { return; }
  status_->setText(values.invalidScoreText?tr("The saved rating text could not be read; the original value is treated as not rated.")
      :values.recoveredBackup?tr("My rating was read from the saved-state backup."):tr("My saved rating"));
  if(!self || !controls() || revision_!=revision) { return; }
  rendering_=false; updateButtons();
}
void LocalRatingDialog::editChanged() {
  if(rendering_) { return; }
  ++revision_;
  // Do not run deletion-capable property callbacks from QComboBox setter tails.
  QMetaObject::invokeMethod(this,&LocalRatingDialog::updateButtons,Qt::QueuedConnection);
}
void LocalRatingDialog::submit() {
  if(!controls() || !loaded_ || busy_ || rendering_ || token_.size()!=32 || !save_->isEnabled()) { return; }
  bool number=false; const auto score=score_->currentData().toInt(&number);
  const auto comment=comment_->toPlainText();
  if(!number || score>5 || comment.size()>MaximumComment) { return; }
  LocalRatingSubmission command{request_,token_,score,comment};
  busy_=true; const auto revision=++revision_; QPointer<LocalRatingDialog> self(this);
  status_->setText(tr("Saving my rating…"));
  if(!self || !controls() || revision_!=revision) { return; }
  updateButtons();
  if(!self || !controls() || revision_!=revision || !busy_) { return; }
  emit saveRequested(std::move(command));
}
void LocalRatingDialog::setResult(LocalRatingSavePresentation packet) {
  if(!packet || packet->identity!=request_ || !busy_ || !controls()
      || (packet->saved && !packet->failure.isEmpty()) || (!packet->saved && packet->failure.isEmpty())) { return; }
  busy_=false; const auto revision=++revision_;
  if(packet->saved) { accept(); return; }
  QPointer<LocalRatingDialog> self(this); status_->setText(packet->failure);
  if(!self || !controls() || revision_!=revision) { return; }
  // Do not automatically retry a possibly published resilient save.
  loaded_=false; updateButtons();
}
}
