#pragma once
#include "local_rating_presentation.hpp"
#include <QDialog>
#include <QPointer>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;

namespace biglybtc::ui {
// Bound to one exact library/session request. Reopen for a changed target or
// profile. Opening/closing emits intentions only; the widget does no storage IO.
class LocalRatingDialog final : public QDialog {
  Q_OBJECT
public:
  explicit LocalRatingDialog(LocalRatingRequest, QWidget* parent=nullptr);
  void setPresentation(LocalRatingPresentation);
  void setResult(LocalRatingSavePresentation);
signals:
  void readRequested(LocalRatingRequest);
  void saveRequested(LocalRatingSubmission);
protected:
  void showEvent(QShowEvent*) override;
private:
  void editChanged();
  void submit();
  void updateButtons();
  bool controls() const;
  LocalRatingRequest request_;
  QPointer<QComboBox> score_;
  QPointer<QPlainTextEdit> comment_;
  QPointer<QLabel> status_;
  QPointer<QPushButton> save_;
  QByteArray token_;
  QString originalComment_;
  qint32 originalScore_=0;
  bool requested_=false, received_=false, loaded_=false, busy_=false, rendering_=false;
  quint64 revision_=0;
};
}
