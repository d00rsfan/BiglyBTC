// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "recheck_presentation.hpp"
#include <QDialog>
class QLabel;class QProgressBar;class QPushButton;
namespace biglybtc::ui {
class RecheckDialog final:public QDialog {
 Q_OBJECT
public:
 explicit RecheckDialog(QWidget* parent=nullptr);~RecheckDialog()override;
 bool begin(RecheckRequest);void present(RecheckPresentation);void invalidate();void refused(RecheckKey);void reject()override;
 std::optional<RecheckKey> key()const{return request_?std::optional{request_->key}:std::nullopt;}
signals:
 void cancelled(biglybtc::ui::RecheckKey);
private:
 void stop(bool notify);std::optional<RecheckRequest> request_;quint64 sequence_=0,revision_=0;bool terminal_=false;
 QLabel *scope_,*status_,*counts_;QProgressBar* progress_;QPushButton* cancel_;
};
}
