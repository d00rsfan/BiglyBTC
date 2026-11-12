// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime_metrics_presentation.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
class QDnsLookup;
namespace biglybtc::app {
class NetworkIdentityController final:public QObject {
 Q_OBJECT
public:
 explicit NetworkIdentityController(QObject* parent=nullptr,QHostAddress resolver={},quint16 port=53,int timeout=5000);
 ~NetworkIdentityController()override;
 void beginSession(quint64);void observe(ui::RuntimeMetricsPresentation);
signals:
 void ready(biglybtc::ui::RuntimeMetricsPresentation);
private:
 struct Slot {QPointer<QDnsLookup> lookup;QTimer* timer=nullptr;services::network_identity::Family value;bool name=false; qint64 retry=0;};
 void cancel(Slot&);void start(std::size_t,QString);void finished(std::size_t);void publish();
 std::array<Slot,2> slots_;quint64 session_=0,epoch_=0,sourceSequence_=0,sequence_=0;
 ui::RuntimeMetricsPresentation latest_;QHostAddress resolver_;quint16 port_;int timeout_;
 struct Cached {services::network_identity::Asn value;qint64 until;};std::vector<Cached> cache_;
};
}
