#pragma once
#include "publication_presentation.hpp"
#include <QDialog>
class QPlainTextEdit;class QPushButton;
namespace biglybtc::ui {
class PublicationReviewDialog final:public QDialog {
 Q_OBJECT
public:
 explicit PublicationReviewDialog(QWidget* parent=nullptr);
 bool beginReview(PublicationPresentation);void invalidate();void complete();void reject()override;
 PublicationPresentation frame()const{return frame_;}
signals:
 void confirmed(biglybtc::ui::PublicationConfirmation);
 void cancelled(biglybtc::ui::PublicationCommandKey);
private:
 PublicationPresentation frame_;quint64 revision_=0;bool submitting_=false;QPlainTextEdit* text_;QPushButton* confirm_;
};
}
