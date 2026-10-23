#pragma once
#include "lookup.hpp"
#include <QPointer>
#include <QWidget>
#include <memory>
class QLabel;
class QTreeView;
class QPlainTextEdit;
class QShowEvent;
namespace biglybtc::ui {
class OwnedLookupTreeWidget final:public QWidget {
  Q_OBJECT
public:
  explicit OwnedLookupTreeWidget(QWidget* parent=nullptr);
  bool beginSession(quint64);
  bool setSnapshot(std::shared_ptr<const services::azureus_control::LookupSnapshot>,bool current,quint64 session);
  std::shared_ptr<const services::azureus_control::LookupSnapshot> snapshot()const{return source_;}
protected:
  void showEvent(QShowEvent*)override;
  bool eventFilter(QObject*,QEvent*)override;
private:
  class Model;
  void refresh();void queueRefresh();void inspect();void queueInspect();
  quint64 session_=0,revision_=0,generation_=0,lookup_=0,sourceRevision_=0,inspectionRevision_=0;
  bool current_=false,dirty_=true,rendering_=false,queued_=false,inspectQueued_=false;
  std::shared_ptr<const services::azureus_control::LookupSnapshot> source_;
  QPointer<QLabel> state_;
  QPointer<QTreeView> tree_;
  QPointer<QPlainTextEdit> details_;
  QPointer<Model> model_;
  QPointer<QWidget> observedWindow_;
};
} // namespace biglybtc::ui
