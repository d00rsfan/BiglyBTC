// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "metasearch_presentation.hpp"
#include <QDialog>
#include <QPointer>
class QLabel;class QLineEdit;class QComboBox;class QPushButton;
namespace biglybtc::ui {
// An owning presentation only. The profile worker retains the real result
// handle; no URI/hash/name from this dialog can authorize persistence/network.
class MetasearchMagnetDialog final:public QDialog {
 Q_OBJECT
public:
 explicit MetasearchMagnetDialog(QWidget* parent=nullptr);
 bool beginReview(MetasearchDownloadPresentation,QString defaultDirectory);
 void complete();void invalidate();void setBusy(bool);void refuse(const MetasearchMagnetSubmission&);
 void reject()override;
 MetasearchDownloadPresentation review()const{return frame_;}
 bool pending()const noexcept{return pending_;}
signals:
 void submitted(biglybtc::ui::MetasearchMagnetSubmission);
 void cancelled(biglybtc::ui::MetasearchDownloadKey);
private:
 void confirm();void controls();
 MetasearchDownloadPresentation frame_;
 quint64 revision_=0;bool busy_=false,pending_=false;
 QLabel *identity_,*name_,*source_,*query_,*status_;
 QLineEdit* destination_;QComboBox* network_;QPushButton *confirm_,*browse_;
};
}
