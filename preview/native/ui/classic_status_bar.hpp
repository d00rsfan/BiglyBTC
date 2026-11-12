// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "classic_status_settings.hpp"
#include "native_session_presentation.hpp"
#include "runtime_metrics_presentation.hpp"
#include "transfer_duration.hpp"
#include <QStatusBar>
class QLabel;class QToolButton;class QResizeEvent;
namespace biglybtc::ui {
class ClassicStatusBar final:public QStatusBar {
 Q_OBJECT
public:
 explicit ClassicStatusBar(QWidget* parent=nullptr);
 void beginSession(quint64);
 void setSettings(ClassicStatusSettings,TransferDisplaySettings);
 void setSessionSnapshot(NativeSessionPresentation);
 void setRuntimeSnapshot(RuntimeMetricsPresentation);
 void setOverall(qint64 downloaded,qint64 uploaded,bool available);
 void setOffline(bool);
signals:void statisticsRequested(QString section);
protected:void resizeEvent(QResizeEvent*)override;
private:
 QToolButton* field(const char*,const QString&,const QString&);
 void render();void visibility();
 std::optional<qint64> metric(std::string_view)const;
 quint64 session_=0;ClassicStatusSettings settings_;TransferDisplaySettings units_;
 NativeSessionPresentation native_;RuntimeMetricsPresentation runtime_;
 bool ratioKnown_=false,offline_=false;qint64 downloaded_=0,uploaded_=0;
 QLabel* version_;QToolButton *mainline_,*ratio_,*network_,*nat_,*ips_,*down_,*up_;
};
}
