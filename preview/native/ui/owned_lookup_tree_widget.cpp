#include "owned_lookup_tree_widget.hpp"
#include <QAbstractItemModel>
#include <QByteArray>
#include <QEvent>
#include <QHeaderView>
#include <QHostAddress>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QShowEvent>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <iterator>
#include <utility>
namespace biglybtc::ui {
namespace {
namespace control=services::azureus_control;
QString endpoint(const services::azureus_dht::Address& value){
  if(value.length==4){quint32 ip=0;for(unsigned i=0;i<4;++i){ip=(ip<<8)|value.bytes[i];}return QHostAddress(ip).toString()+QLatin1Char(':')+QString::number(value.port);}
  if(value.length==16){Q_IPV6ADDR ip{};std::copy_n(value.bytes.begin(),16,ip.c);return QLatin1Char('[')+QHostAddress(ip).toString()+QStringLiteral("]:")+QString::number(value.port);}
  return QObject::tr("Unavailable endpoint");
}
QString idText(const control::NodeId& id){return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(id.data()),static_cast<qsizetype>(id.size())).toHex());}
QString nodeState(control::LookupNodeState state){switch(state){
  case control::LookupNodeState::Candidate:return QObject::tr("Admitted candidate; not sent");case control::LookupNodeState::Reserved:return QObject::tr("Dispatch reserved; not confirmed");
  case control::LookupNodeState::Sent:return QObject::tr("Sent");case control::LookupNodeState::Partial:return QObject::tr("Partial reply");case control::LookupNodeState::Replied:return QObject::tr("Reply received");
  case control::LookupNodeState::Timeout:return QObject::tr("Timed out");case control::LookupNodeState::Cancelled:return QObject::tr("Cancellation acknowledged");case control::LookupNodeState::Closed:return QObject::tr("Closed");
  case control::LookupNodeState::Failed:return QObject::tr("Failed");case control::LookupNodeState::Rejected:return QObject::tr("Dispatch rejected");case control::LookupNodeState::Skipped:return QObject::tr("Not dispatched");}
  return QObject::tr("Unavailable");
}
QString lookupState(control::LookupState state){switch(state){
  case control::LookupState::Idle:return QObject::tr("Idle");case control::LookupState::Running:return QObject::tr("Running");case control::LookupState::Stopping:return QObject::tr("Stopping; awaiting actual completions");
  case control::LookupState::Complete:return QObject::tr("Complete");case control::LookupState::TimedOut:return QObject::tr("Timed out");case control::LookupState::Cancelled:return QObject::tr("Cancelled");
  case control::LookupState::Closed:return QObject::tr("Closed");case control::LookupState::Failed:return QObject::tr("Failed");}return QObject::tr("Unavailable");
}
QString stopReason(control::LookupStop reason){switch(reason){
  case control::LookupStop::Exhausted:return QObject::tr("frontier exhausted");case control::LookupStop::Closest:return QObject::tr("closest-contact policy");case control::LookupStop::ValueLimit:return QObject::tr("value limit");
  case control::LookupStop::TwoValueReplies:return QObject::tr("two value replies");case control::LookupStop::Deadline:return QObject::tr("deadline");case control::LookupStop::Cancelled:return QObject::tr("cancellation requested");
  case control::LookupStop::Closed:return QObject::tr("owner closed");case control::LookupStop::Capacity:return QObject::tr("bounded capacity");case control::LookupStop::Unsupported:return QObject::tr("unsupported reply");
  case control::LookupStop::Clock:return QObject::tr("source clock discontinuity");case control::LookupStop::Memory:return QObject::tr("optional allocation failed");case control::LookupStop::Overflow:return QObject::tr("source arithmetic limit");}return QObject::tr("unavailable");
}
QString optionalTime(const std::optional<std::int64_t>& value){return value?QString::number(*value)+QObject::tr(" ms"):QObject::tr("not observed");}
QString nodeDescription(const control::LookupNode& node){return QObject::tr("Node %1; parent %2; endpoint %3; node ID %4; %5%6; actual ticket %7; depth %8; source discovered %9 ms, intent %10, sent %11, finished %12; %13 reply slices; %14 accepted values.")
    .arg(node.id).arg(node.parent).arg(endpoint(node.contact.address),idText(node.contact.id),nodeState(node.state),node.cancellationRequested?QObject::tr("; cancellation requested"):QString{})
    .arg(node.ticket?QString::number(node.ticket):QObject::tr("not observed")).arg(node.depth).arg(node.discoveredMillis).arg(optionalTime(node.intentMillis),optionalTime(node.sentMillis),optionalTime(node.finishedMillis)).arg(node.slices).arg(node.values)+QObject::tr(" Explicit admission: %1.").arg(node.owned?QObject::tr("admitted"):QObject::tr("not admitted"));}
QString resultDetails(const control::LookupSnapshot& source,const control::LookupNode& node){
  QString text=nodeDescription(node)+QObject::tr("\nTarget %1. Times are explicit owner-monotone milliseconds, not wall time.\n").arg(idText(source.options.target));std::size_t count=0;
  for(const auto& result:source.results){if(result.node!=node.id){continue;}++count;const auto& value=result.value;
    text+=QObject::tr("\nAccepted value %1: %2 bytes; version %3; source created %4; flags %5; life hours %6; replication %7; originator %8 / %9\nHex: %10\n")
        .arg(count).arg(value.bytes.size()).arg(value.version).arg(value.created).arg(value.flags).arg(value.lifeHours).arg(value.replication)
        .arg(endpoint(value.originator.address),idText(value.originator.id),QString::fromLatin1(QByteArray(value.bytes.data(),static_cast<qsizetype>(value.bytes.size())).toHex()));
  }
  if(!count){text+=QObject::tr("No accepted value observed for this node.");}return text;
}
}
class OwnedLookupTreeWidget::Model final:public QAbstractItemModel{
public:
  explicit Model(QObject* parent):QAbstractItemModel(parent){}
  int columnCount(const QModelIndex& parent={})const override{(void)parent;return 7;}
  int rowCount(const QModelIndex& parent={})const override{if(!source_||(parent.isValid()&&(parent.model()!=this||parent.column()!=0))){return 0;}const auto parentId=id(parent);int count=0;
    for(const auto& node:source_->nodes){if(node.parent==parentId){++count;}}return count;}
  QModelIndex index(int row,int column,const QModelIndex& parent={})const override{
    if(row<0||column<0||column>=7||!source_||(parent.isValid()&&(parent.model()!=this||parent.column()!=0))){return {};}
    const auto parentId=id(parent);int next=0;for(std::size_t i=0;i<source_->nodes.size();++i){if(source_->nodes[i].parent==parentId&&next++==row){return createIndex(row,column,static_cast<quintptr>(i+1));}}return {};
  }
  QModelIndex parent(const QModelIndex& index)const override{const auto* node=record(index);if(!node||!node->parent){return {};}
    const auto found=std::find_if(source_->nodes.begin(),source_->nodes.end(),[&](const auto& candidate){return candidate.id==node->parent;});if(found==source_->nodes.end()){return {};}
    int row=0;for(auto current=source_->nodes.begin();current!=found;++current){if(current->parent==found->parent){++row;}}
    return createIndex(row,0,static_cast<quintptr>(std::distance(source_->nodes.begin(),found)+1));
  }
  QVariant headerData(int section,Qt::Orientation orientation,int role)const override{if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole||section<0||section>=7){return {};}
    static constexpr std::array<const char*,7> labels{"Lookup node","Endpoint","Observed state","Accepted values","Actual ticket","Observed since send (ms)","Depth"};return tr(labels[static_cast<std::size_t>(section)]);}
  QVariant data(const QModelIndex& index,int role)const override{const auto* node=record(index);if(!node||index.column()<0||index.column()>=7){return {};}
    const auto end=node->finishedMillis.value_or(source_->updatedMillis);
    const auto elapsed=node->sentMillis&&*node->sentMillis>=0&&end>=*node->sentMillis?std::optional<std::int64_t>{end-*node->sentMillis}:std::nullopt;
    if(role==Qt::ToolTipRole||role==Qt::AccessibleDescriptionRole){return nodeDescription(*node);}
    if(role==Qt::UserRole){switch(index.column()){case 0:return QVariant::fromValue(static_cast<qulonglong>(node->id));case 3:return QVariant::fromValue(static_cast<qulonglong>(node->values));case 4:return node->ticket?QVariant::fromValue(static_cast<qulonglong>(node->ticket)):QVariant{};case 5:return elapsed?QVariant::fromValue(static_cast<qlonglong>(*elapsed)):QVariant{};case 6:return node->depth;default:return {};}}
    if(role!=Qt::DisplayRole&&role!=Qt::AccessibleTextRole){return {};}switch(index.column()){
      case 0:return tr("Node %1").arg(node->id);case 1:return endpoint(node->contact.address);case 2:return nodeState(node->state)+(node->cancellationRequested?tr("; cancellation requested"):QString{});
      case 3:return QString::number(node->values);case 4:return node->ticket?QString::number(node->ticket):tr("Not sent");case 5:return elapsed?QString::number(*elapsed):tr("Not observed");case 6:return node->depth;default:return {};}}
  const control::LookupNode* record(const QModelIndex& index)const{if(!source_||!index.isValid()||index.model()!=this||!index.internalId()||index.internalId()>source_->nodes.size()){return nullptr;}return &source_->nodes[static_cast<std::size_t>(index.internalId()-1)];}
  bool represents(const std::shared_ptr<const control::LookupSnapshot>& source)const{return source_==source;}
  std::uint64_t id(const QModelIndex& index)const{const auto* value=record(index);return value?value->id:0;}
  bool replace(std::shared_ptr<const control::LookupSnapshot> source,const std::function<bool()>& current){QPointer<Model> guard(this);beginResetModel();if(!guard){return false;}const bool accepted=current();if(!guard){return false;}if(accepted){source_=std::move(source);}endResetModel();return accepted&&guard;}
private:std::shared_ptr<const control::LookupSnapshot> source_;
};
OwnedLookupTreeWidget::OwnedLookupTreeWidget(QWidget* parent):QWidget(parent){
  setObjectName(QStringLiteral("azureus-lookup-tree-view"));auto* layout=new QVBoxLayout(this);
  state_=new QLabel(tr("Recursive GET observation unavailable"),this);state_->setObjectName(QStringLiteral("azureus-lookup-tree-status"));state_->setTextFormat(Qt::PlainText);state_->setWordWrap(true);layout->addWidget(state_);
  model_=new Model(this);tree_=new QTreeView(this);tree_->setObjectName(QStringLiteral("azureus-lookup-tree"));tree_->setModel(model_);tree_->setRootIsDecorated(true);tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);tree_->setMinimumHeight(110);tree_->setColumnWidth(0,135);tree_->setColumnWidth(1,180);tree_->setColumnWidth(2,230);layout->addWidget(tree_,1);
  details_=new QPlainTextEdit(this);details_->setObjectName(QStringLiteral("azureus-lookup-tree-details"));details_->setReadOnly(true);details_->setMinimumHeight(65);details_->setMaximumHeight(115);details_->setAccessibleName(tr("Actual lookup node and accepted value inspection"));layout->addWidget(details_);
  auto* scope=new QLabel(tr("GET lookup tree. Select a node to inspect requests and returned values."),this);scope->setObjectName(QStringLiteral("azureus-lookup-tree-scope"));scope->setTextFormat(Qt::PlainText);scope->setWordWrap(true);scope->setToolTip(tr("Parent/child edges show actual discovery, not geographic paths, current peer liveness, PUT activity or public routing. Dispatch requires explicit admission. Complete means lookup settled, not that a value was found. This view starts no lookup."));layout->addWidget(scope);
  connect(tree_->selectionModel(),&QItemSelectionModel::currentChanged,this,[this]{if(inspectionRevision_==std::numeric_limits<quint64>::max()){return;}++inspectionRevision_;if(!rendering_){queueInspect();}});
}
bool OwnedLookupTreeWidget::beginSession(quint64 session){if(!session||session<=session_||revision_==std::numeric_limits<quint64>::max()){return false;}++revision_;session_=session;generation_=lookup_=sourceRevision_=0;source_.reset();current_=false;dirty_=true;refresh();return true;}
bool OwnedLookupTreeWidget::setSnapshot(std::shared_ptr<const control::LookupSnapshot> source,bool current,quint64 session){
  if(!session||session!=session_||revision_==std::numeric_limits<quint64>::max()||(source&&!control::validLookupSnapshot(*source))){return false;}
  if(source){if(source->generation<generation_||(source->generation==generation_&&(source->lookup<lookup_||(source->lookup==lookup_&&source->revision<sourceRevision_)))){return false;}
    if(source_&&source->generation==generation_&&source->lookup==lookup_&&source->revision==sourceRevision_){source=source_;}
    generation_=source->generation;lookup_=source->lookup;sourceRevision_=source->revision;}
  if(source==source_&&current==current_){refresh();return true;}++revision_;source_=std::move(source);current_=current;dirty_=true;refresh();return true;
}
void OwnedLookupTreeWidget::queueRefresh(){if(queued_){return;}queued_=true;QPointer<OwnedLookupTreeWidget> guard(this);QMetaObject::invokeMethod(this,[guard]{if(guard){guard->queued_=false;guard->refresh();}},Qt::QueuedConnection);}
void OwnedLookupTreeWidget::refresh(){
  if(!dirty_||rendering_||(source_&&(!isVisible()||window()->isMinimized()))){return;}QPointer<OwnedLookupTreeWidget> guard(this);const auto revision=revision_;const auto source=source_;const auto model=model_;const auto tree=tree_;const auto details=details_;const auto label=state_;
  const auto valid=[&]{return guard&&guard->revision_==revision&&guard->source_==source&&(!source||(guard->isVisible()&&!guard->window()->isMinimized()));};
  rendering_=true;struct Finish{QPointer<OwnedLookupTreeWidget> owner;quint64 revision;~Finish(){if(owner){owner->rendering_=false;if(owner->dirty_&&owner->revision_!=revision){owner->queueRefresh();}}}} finish{guard,revision};
  if(!model||!tree||!details||!label||tree->model()!=model){return;}const bool replaced=!model->represents(source);if(replaced&&(!model->replace(source,valid)||!valid())){return;}
  QString text=tr("Recursive GET observation unavailable; no branches inferred");
  if(source){text=tr("GET %1; target %2; %3%4; %5 observed nodes / %6 active; %7 accepted values. Contact bound %8, value bound %9; %10 contacts rejected / %11 duplicate contacts / %12 duplicate values.")
      .arg(source->lookup).arg(idText(source->options.target),lookupState(source->state),source->stop?tr(" (%1)").arg(stopReason(*source->stop)):QString{})
      .arg(source->nodes.size()).arg(source->active).arg(source->results.size()).arg(source->options.maximumContacts).arg(source->options.maximumValues).arg(source->rejectedContacts).arg(source->duplicateContacts).arg(source->duplicateValues);
    if(!current_||source->state==control::LookupState::Closed){text+=tr(" Last observed.");}}
  label->setText(text);if(!valid()||!label){return;}label->setToolTip(text);if(!valid()||!label){return;}label->setAccessibleDescription(text);if(!valid()||!details){return;}
  if(replaced||!tree->currentIndex().isValid()){
    const auto empty=tr("Select an actual lookup node to inspect endpoint, dispatch/provenance and accepted value bytes. No selected node.");
    details->setPlainText(empty);if(!valid()||!details){return;}details->setToolTip(empty);if(!valid()||!details){return;}details->setAccessibleDescription(empty);if(!valid()){return;}
  }dirty_=false;
}
void OwnedLookupTreeWidget::queueInspect(){if(inspectQueued_){return;}inspectQueued_=true;QPointer<OwnedLookupTreeWidget> guard(this);QMetaObject::invokeMethod(this,[guard]{if(guard){guard->inspectQueued_=false;guard->inspect();}},Qt::QueuedConnection);}
void OwnedLookupTreeWidget::inspect(){
  if(rendering_||!isVisible()||window()->isMinimized()||!model_||!tree_||!details_||!model_->represents(source_)){return;}const auto* node=model_->record(tree_->currentIndex());const auto source=source_;if(!source||!node){return;}
  QPointer<OwnedLookupTreeWidget> guard(this);const auto revision=revision_,inspection=inspectionRevision_;const auto details=details_;const auto text=resultDetails(*source,*node);details->setPlainText(text);
  if(!guard||guard->revision_!=revision||guard->inspectionRevision_!=inspection||guard->source_!=source||!details){return;}details->setToolTip(text);
  if(!guard||guard->revision_!=revision||guard->inspectionRevision_!=inspection||guard->source_!=source||!details){return;}details->setAccessibleDescription(text);
}
void OwnedLookupTreeWidget::showEvent(QShowEvent* event){QPointer<OwnedLookupTreeWidget> guard(this);QWidget::showEvent(event);if(!guard){return;}if(observedWindow_!=window()){if(observedWindow_){observedWindow_->removeEventFilter(this);}observedWindow_=window();observedWindow_->installEventFilter(this);}queueRefresh();}
bool OwnedLookupTreeWidget::eventFilter(QObject* object,QEvent* event){if(object==observedWindow_&&(event->type()==QEvent::Show||event->type()==QEvent::WindowStateChange)){queueRefresh();}return QWidget::eventFilter(object,event);}
} // namespace biglybtc::ui
