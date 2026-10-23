#pragma once
#include "saved_speed_manager_presentation.hpp"
#include <QPointer>
#include <QWidget>
#include <array>
class QLabel;
class QPushButton;
class QShowEvent;
namespace biglybtc::ui {
class SavedSpeedManagerView final:public QWidget {
 Q_OBJECT
public:
 explicit SavedSpeedManagerView(QWidget* parent=nullptr);
 bool beginSession(quint64);
 bool setPresentation(SavedSpeedManagerPresentation);
 SavedSpeedManagerPresentation presentation()const{return packet_;}
signals:
 void readRequested(quint64 session);
protected:
 void showEvent(QShowEvent*)override;
 bool eventFilter(QObject*,QEvent*)override;
private:
 void queueRead(bool explicitRead=false);void queueRefresh();void refresh();
 quint64 session_=0,revision_=0;bool dirty_=true,queued_=false,reading_=false,readSent_=false,explicitPending_=false;
 SavedSpeedManagerPresentation packet_;
 std::array<QPointer<QLabel>,9> facts_{};QPointer<QPushButton> refresh_;QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
