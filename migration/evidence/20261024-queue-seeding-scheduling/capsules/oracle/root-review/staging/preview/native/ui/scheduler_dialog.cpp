#include "scheduler_dialog.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace biglybtc::ui {
namespace {
constexpr int MaximumRate=std::numeric_limits<int>::max()/1024;
// QObject::destroyed is delivered even while signals are blocked. Restore only
// surviving objects; a normal QSignalBlocker can outlive its deleted target.
class SafeSignalBlock final {
public:
  explicit SafeSignalBlock(QObject* object):object_(object),previous_(object->blockSignals(true)) {}
  ~SafeSignalBlock() { if(object_) {object_->blockSignals(previous_);} }
private:
  QPointer<QObject> object_; bool previous_;
};
bool valid(const SchedulerPreferences& p) {
  return p.pollMilliseconds>=1000; // int itself supplies the original upper bound.
}
bool valid(const schedule::Schedule& s) {
  return s.start>=0&&s.start<1440&&s.end>=0&&s.end<1440&&s.upload>=0&&s.upload<=MaximumRate
      &&s.download>=0&&s.download<=MaximumRate&&(!s.category||s.category->size()<=4096);
}
bool valid(const SchedulerPresentation& p) {
  if (!p||!p->session||!p->request||!p->revision||!p->snapshot) { return false; }
  const auto& c=p->snapshot->configuration;
  return valid(p->snapshot->preferences)&&c.defaultUpload>=0&&c.defaultUpload<=MaximumRate
      &&c.defaultDownload>=0&&c.defaultDownload<=MaximumRate&&c.schedules.size()<=schedule::MaximumSchedules
      &&std::ranges::all_of(c.schedules,[](const auto& s){return valid(s);});
}
}
struct SchedulerDialog::Impl {
  SchedulerDialog* q;
  quint64 session=0, highest=0, request=0, active=0, serial=0, controlsSerial=0;
  bool busy=false, loading=false, saving=false, rendering=false, changed=false, closeAfterSave=false, loadWanted=false;
  SchedulerPresentation baseline;
  std::vector<std::optional<std::size_t>> sources;
  QPointer<QTableWidget> table;
  QPointer<QCheckBox> enabled,tags,forced,enforce;
  QPointer<QSpinBox> poll,upload,download;
  QPointer<QLabel> status;
  QPointer<QPushButton> apply,ok,cancel,reload,add,remove;
  QPointer<QMessageBox> question;
  struct Guard {
    QPointer<SchedulerDialog> self; const Impl* owner; quint64 expected,session,request;
    bool current() const {return self&&self->impl_.get()==owner&&owner->serial==expected&&owner->session==session&&owner->active==request;}
  };
  Guard guard() const {return {q,this,serial,session,active};}
  bool advance() {
    if(serial==std::numeric_limits<quint64>::max()){return false;}++serial;return true;
  }
  bool text(const QString& value) {
    const auto alive=guard();if(status){status->setText(value);}return alive.current();
  }
  std::optional<SchedulerPreferences> preferences() const {
    if(!enabled||!tags||!forced||!poll){return {};}
    return SchedulerPreferences{enabled->isChecked(),tags->isChecked(),forced->isChecked(),poll->value()};
  }
  bool setPreferences(const SchedulerPreferences& p) {
    const auto alive=guard();
    if(!enabled||!tags||!forced||!poll){return false;}
    enabled->setChecked(p.enabled);if(!alive.current()||!tags){return false;}
    tags->setChecked(p.useTags);if(!alive.current()||!forced){return false;}
    forced->setChecked(p.overrideForced);if(!alive.current()||!poll){return false;}
    poll->setValue(p.pollMilliseconds);return alive.current();
  }
  bool controls() {
    const auto alive=guard();
    if(controlsSerial==std::numeric_limits<quint64>::max()){return false;}
    const auto expected=++controlsSerial;
    const auto current=[this,alive,expected]{return alive.current()&&controlsSerial==expected;};
    const bool editable=session&&baseline&&!busy&&!loading&&!saving;
    const std::vector<QPointer<QWidget>> widgets{table.data(),enabled.data(),tags.data(),forced.data(),enforce.data(),
        poll.data(),upload.data(),download.data(),add.data(),remove.data()};
    for(const auto& widget:widgets){if(widget){widget->setEnabled(editable);}if(!current()){return false;}}
    if(apply){apply->setEnabled(editable&&changed);}if(!current()){return false;}
    if(ok){ok->setEnabled(editable);}if(!current()){return false;}
    if(cancel){cancel->setEnabled(!saving);}if(!current()){return false;}
    if(reload){reload->setEnabled(session&&!busy&&!loading&&!saving&&!changed);}
    return current();
  }
  void edited() {
    if(rendering||!baseline||saving||loading){return;}
    changed=true;if(text(QStringLiteral("Unsaved changes"))){controls();}
  }
  void load() {
    if(!session||busy||loading||saving||!loadWanted||!q->isVisible()){return;}
    if(request==std::numeric_limits<quint64>::max()||!advance()){
      loadWanted=false;text(QStringLiteral("Request identity exhausted; reopen the window."));return;
    }
    loadWanted=false;loading=true;active=++request;const auto alive=guard();
    if(!text(QStringLiteral("Loading schedules…"))||!alive.current()||!controls()||!alive.current()){return;}emit q->loadRequested({session,active});
  }
  QCheckBox* check(int row,int column,bool value) {
    const auto alive=guard();if(!table){return nullptr;}
    QPointer<QCheckBox> box=new QCheckBox(table);box->setChecked(value);
    if(!alive.current()||!box||!table){return nullptr;}table->setCellWidget(row,column,box);
    if(!alive.current()||!box){return nullptr;}
    QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});return box.data();
  }
  bool append(const schedule::Schedule& s,std::optional<std::size_t> source) {
    const auto alive=guard();if(!table){return false;}
    const int row=table->rowCount();table->insertRow(row);if(!alive.current()||!table){return false;}sources.push_back(source);
    check(row,0,s.enabled);if(!alive.current()||!table){return false;}
    auto* days=new QWidget(table);auto* daysLayout=new QHBoxLayout(days);daysLayout->setContentsMargins(0,0,0,0);
    const std::array<QString,7> names{QStringLiteral("Su"),QStringLiteral("Mo"),QStringLiteral("Tu"),QStringLiteral("We"),QStringLiteral("Th"),QStringLiteral("Fr"),QStringLiteral("Sa")};
    for (int day=0;day<7;++day) {
      auto* box=new QCheckBox(names[day],days);box->setObjectName(QStringLiteral("day-%1").arg(day));box->setChecked(s.days[day]);
      daysLayout->addWidget(box);QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});
    }
    table->setCellWidget(row,1,days);if(!alive.current()||!table){return false;}
    for (int column : {2,3}) {
      const int minute=column==2?s.start:s.end;auto* time=new QTimeEdit(QTime(minute/60,minute%60),table);
      time->setDisplayFormat(QStringLiteral("HH:mm"));table->setCellWidget(row,column,time);if(!alive.current()||!table){return false;}
      QObject::connect(time,&QTimeEdit::timeChanged,q,[this]{edited();});
    }
    for (int column : {4,5}) {
      auto* rate=new QSpinBox(table);rate->setRange(0,MaximumRate);rate->setSpecialValueText(QStringLiteral("Unlimited"));
      rate->setValue(column==4?s.upload:s.download);table->setCellWidget(row,column,rate);if(!alive.current()||!table){return false;}
      QObject::connect(rate,&QSpinBox::valueChanged,q,[this]{edited();});
    }
    check(row,6,s.downloadsPaused);if(!alive.current()||!table){return false;}
    check(row,7,s.seedsPaused);if(!alive.current()||!table){return false;}
    auto* category=new QLineEdit(table);category->setMaxLength(4096);
    if (s.category) { category->setText(QString::fromUtf8(*s.category)); }
    category->setProperty("wasNull",!s.category);table->setCellWidget(row,8,category);if(!alive.current()||!table){return false;}
    QObject::connect(category,&QLineEdit::textChanged,q,[self=QPointer<SchedulerDialog>(q),field=QPointer<QLineEdit>(category)]{
      if(!self||!field){return;}const auto alive=self->impl_->guard();field->setProperty("wasNull",false);
      if(alive.current()){self->impl_->edited();}
    });
    check(row,9,s.onlyCategory);if(!alive.current()||!table){return false;}
    check(row,10,s.excludeCategory);return alive.current();
  }
  std::optional<SchedulerSubmission> submission() const {
    const auto settings=preferences();
    if(!settings||!baseline||!table||!upload||!download||!enforce||sources.size()!=static_cast<std::size_t>(table->rowCount())){return {};}
    SchedulerSubmission value{session,active,baseline->revision,*settings,upload->value(),download->value(),enforce->isChecked(),{}};
    for (int row=0;row<table->rowCount();++row) {
      schedule::Schedule s;
      for(int column:{0,6,7,9,10}){if(!qobject_cast<QCheckBox*>(table->cellWidget(row,column))){return {};}}
      for(int column:{2,3}){if(!qobject_cast<QTimeEdit*>(table->cellWidget(row,column))){return {};}}
      for(int column:{4,5}){if(!qobject_cast<QSpinBox*>(table->cellWidget(row,column))){return {};}}
      if(!qobject_cast<QLineEdit*>(table->cellWidget(row,8))||!table->cellWidget(row,1)){return {};}
      for(int day=0;day<7;++day){if(!table->cellWidget(row,1)->findChild<QCheckBox*>(QStringLiteral("day-%1").arg(day))){return {};}}
      auto boolean=[&](int column){return qobject_cast<QCheckBox*>(table->cellWidget(row,column))->isChecked();};
      s.enabled=boolean(0);s.downloadsPaused=boolean(6);s.seedsPaused=boolean(7);s.onlyCategory=boolean(9);s.excludeCategory=boolean(10);
      for (int day=0;day<7;++day) {s.days[day]=table->cellWidget(row,1)->findChild<QCheckBox*>(QStringLiteral("day-%1").arg(day))->isChecked();}
      auto minute=[&](int column){const auto t=qobject_cast<QTimeEdit*>(table->cellWidget(row,column))->time();return t.hour()*60+t.minute();};
      s.start=minute(2);s.end=minute(3);
      s.upload=qobject_cast<QSpinBox*>(table->cellWidget(row,4))->value();s.download=qobject_cast<QSpinBox*>(table->cellWidget(row,5))->value();
      auto* category=qobject_cast<QLineEdit*>(table->cellWidget(row,8));
      s.category=category->property("wasNull").toBool()?std::nullopt:std::optional{category->text().toUtf8().toStdString()};
      value.schedules.push_back({sources[static_cast<std::size_t>(row)],std::move(s)});
    }
    return value;
  }
  bool render(SchedulerPresentation packet) {
    if(!advance()){return false;}const auto alive=guard();
    rendering=true;baseline=std::move(packet);
    struct Restore {
      Guard alive;~Restore(){if(alive.current()){alive.self->impl_->rendering=false;}}
    } restore{alive};
    std::vector<std::unique_ptr<SafeSignalBlock>> blocked;
    const std::vector<QPointer<QObject>> objects{table.data(),enabled.data(),tags.data(),
      forced.data(),enforce.data(),poll.data(),upload.data(),download.data()};
    for(const auto& object:objects){if(object){blocked.push_back(std::make_unique<SafeSignalBlock>(object));}}
    if(!table||!upload||!download||!enforce){return false;}
    const auto retained=baseline;
    table->setRowCount(0);if(!alive.current()||!table){return false;}sources.clear();
    if(!setPreferences(retained->snapshot->preferences)||!alive.current()){return false;}
    const auto& c=retained->snapshot->configuration;
    upload->setValue(c.defaultUpload);if(!alive.current()||!download){return false;}
    download->setValue(c.defaultDownload);if(!alive.current()||!enforce){return false;}
    enforce->setChecked(c.defaultsEnforce);if(!alive.current()){return false;}
    for(std::size_t i=0;i<c.schedules.size();++i){if(!append(c.schedules[i],i)||!alive.current()){return false;}}
    changed=false;rendering=false;
    if(!text(retained->snapshot->diagnostic.isEmpty()?QStringLiteral("Schedules loaded. Zero speed means unlimited."):retained->snapshot->diagnostic)){return false;}
    return controls()&&alive.current();
  }
  void save(bool close) {
    if(!baseline||busy||loading||saving||rendering){return;}
    if(!changed){if(close){q->done(QDialog::Accepted);}return;}
    auto value=submission();
    if(!value){text(QStringLiteral("A schedule control is unavailable; reload before saving."));return;}
    if(request==std::numeric_limits<quint64>::max()||!advance()){
      text(QStringLiteral("Request identity exhausted; reopen the window."));return;
    }
    closeAfterSave=close;saving=true;active=++request;value->request=active;
    const auto alive=guard();if(!controls()||!alive.current()){return;}emit q->saveRequested(std::move(*value));
  }
  void close() {
    if (saving) {return;}
    if (!changed) {q->done(QDialog::Rejected);return;}
    if(question){question->raise();return;}
    auto* prompt=new QMessageBox(QMessageBox::Question,QStringLiteral("Unsaved schedules"),QStringLiteral("Save changes before closing?"),
        QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,q);question=prompt;prompt->setAttribute(Qt::WA_DeleteOnClose);
    const auto expected=session;QObject::connect(prompt,&QMessageBox::finished,q,[self=QPointer<SchedulerDialog>(q),expected](int result){
      if (!self||self->impl_->session!=expected) {return;}self->impl_->question.clear();
      if(result==QMessageBox::Save){self->impl_->save(true);}else if(result==QMessageBox::Discard){self->impl_->changed=false;self->done(QDialog::Rejected);}
    });prompt->open();
  }
};
SchedulerDialog::SchedulerDialog(QWidget* parent):QDialog(parent),impl_(std::make_unique<Impl>()) {
  impl_->q=this;setObjectName(QStringLiteral("speed-scheduler-dialog"));setWindowTitle(QStringLiteral("Speed Scheduler"));resize(1240,650);
  auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
  auto check=[&](QString label,QString name){auto* box=new QCheckBox(this);box->setObjectName(name);form->addRow(label,box);connect(box,&QCheckBox::toggled,this,[this]{impl_->edited();});return box;};
  impl_->enabled=check(QStringLiteral("Enable schedules"),QStringLiteral("scheduler-enabled"));
  impl_->tags=check(QStringLiteral("Match manual tags instead of categories"),QStringLiteral("scheduler-use-tags"));
  impl_->forced=check(QStringLiteral("Schedules may pause force-started torrents"),QStringLiteral("scheduler-override-forced"));
  auto spin=[&](QString label,QString name,int low,int high){auto* value=new QSpinBox(this);value->setObjectName(name);value->setRange(low,high);form->addRow(label,value);connect(value,&QSpinBox::valueChanged,this,[this]{impl_->edited();});return value;};
  impl_->poll=spin(QStringLiteral("Check schedules every (milliseconds)"),QStringLiteral("scheduler-poll"),1000,std::numeric_limits<int>::max());
  impl_->upload=spin(QStringLiteral("Default upload (KiB/s, 0 unlimited)"),QStringLiteral("scheduler-default-upload"),0,MaximumRate);
  impl_->download=spin(QStringLiteral("Default download (KiB/s, 0 unlimited)"),QStringLiteral("scheduler-default-download"),0,MaximumRate);
  impl_->enforce=check(QStringLiteral("Restore default speeds outside schedules"),QStringLiteral("scheduler-enforce-defaults"));layout->addLayout(form);
  auto* note=new QLabel(QStringLiteral("Times use local time and include both endpoints. For an overnight period, add one schedule on each side of midnight. Overlapping schedules use the largest numeric speed."),this);
  note->setWordWrap(true);note->setTextFormat(Qt::PlainText);layout->addWidget(note);
  impl_->table=new QTableWidget(0,11,this);impl_->table->setObjectName(QStringLiteral("scheduler-schedules"));
  impl_->table->setHorizontalHeaderLabels({QStringLiteral("Enabled"),QStringLiteral("Days"),QStringLiteral("From"),QStringLiteral("Until"),QStringLiteral("Upload KiB/s"),QStringLiteral("Download KiB/s"),QStringLiteral("Pause downloads"),QStringLiteral("Pause seeds"),QStringLiteral("Category / tag"),QStringLiteral("Only matching"),QStringLiteral("Exclude matching")});
  impl_->table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);impl_->table->setColumnWidth(1,360);layout->addWidget(impl_->table,1);
  auto* row=new QHBoxLayout;impl_->add=new QPushButton(QStringLiteral("Add"),this);impl_->remove=new QPushButton(QStringLiteral("Remove selected"),this);
  impl_->add->setObjectName(QStringLiteral("scheduler-add"));impl_->remove->setObjectName(QStringLiteral("scheduler-remove"));row->addWidget(impl_->add);row->addWidget(impl_->remove);row->addStretch();layout->addLayout(row);
  connect(impl_->add,&QPushButton::clicked,this,[self=QPointer<SchedulerDialog>(this)]{
    if(!self||self->impl_->sources.size()>=schedule::MaximumSchedules){return;}
    const auto alive=self->impl_->guard();schedule::Schedule value;value.days.fill(true);
    if(self->impl_->append(value,{})&&alive.current()){self->impl_->edited();}
  });
  connect(impl_->remove,&QPushButton::clicked,this,[self=QPointer<SchedulerDialog>(this)]{
    if(!self||!self->impl_->table){return;}const auto alive=self->impl_->guard();const int r=self->impl_->table->currentRow();
    if(r>=0&&static_cast<std::size_t>(r)<self->impl_->sources.size()){
      self->impl_->table->removeRow(r);if(!alive.current()){return;}
      self->impl_->sources.erase(self->impl_->sources.begin()+r);self->impl_->edited();
    }
  });
  impl_->status=new QLabel(this);impl_->status->setWordWrap(true);impl_->status->setTextFormat(Qt::PlainText);impl_->status->setObjectName(QStringLiteral("scheduler-status"));layout->addWidget(impl_->status);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);impl_->apply=buttons->button(QDialogButtonBox::Apply);impl_->ok=buttons->button(QDialogButtonBox::Ok);impl_->cancel=buttons->button(QDialogButtonBox::Cancel);
  impl_->reload=buttons->addButton(QStringLiteral("Reload"),QDialogButtonBox::ResetRole);impl_->apply->setObjectName(QStringLiteral("scheduler-apply"));impl_->ok->setObjectName(QStringLiteral("scheduler-ok"));impl_->reload->setObjectName(QStringLiteral("scheduler-reload"));
  connect(impl_->apply,&QPushButton::clicked,this,[this]{impl_->save(false);});connect(impl_->ok,&QPushButton::clicked,this,&SchedulerDialog::accept);connect(impl_->cancel,&QPushButton::clicked,this,&SchedulerDialog::reject);
  connect(impl_->reload,&QPushButton::clicked,this,[this]{impl_->loadWanted=true;impl_->load();});layout->addWidget(buttons);impl_->controls();
}
SchedulerDialog::~SchedulerDialog()=default;
bool SchedulerDialog::beginSession(quint64 session) {
  if(!session||session<=impl_->highest||impl_->serial>std::numeric_limits<quint64>::max()-2){return false;}
  const QPointer<SchedulerDialog> self(this);const auto expectedSerial=impl_->serial+1;const auto previousHighest=impl_->highest;
  invalidateSession();
  if(!self||self->impl_->serial!=expectedSerial||self->impl_->highest!=previousHighest){return false;}
  self->impl_->session=session;self->impl_->highest=session;self->impl_->loadWanted=true;
  self->impl_->advance();self->impl_->load();return true;
}
void SchedulerDialog::invalidateSession() {
  const QPointer<SchedulerDialog> self(this);impl_->advance();const auto alive=impl_->guard();
  impl_->session=0;impl_->baseline.reset();impl_->changed=false;impl_->loadWanted=false;impl_->rendering=false;
  impl_->loading=impl_->saving=impl_->closeAfterSave=false;
  const auto prompt=impl_->question;impl_->question.clear();if(prompt){prompt->close();}
  if(self&&alive.current()){self->impl_->controls();}
}
bool SchedulerDialog::dirty() const noexcept {return impl_->changed;}
bool SchedulerDialog::pending() const noexcept {return impl_->loading||impl_->saving;}
void SchedulerDialog::setBusy(bool busy) {
  const QPointer<SchedulerDialog> self(this);const auto alive=impl_->guard();impl_->busy=busy;
  if(impl_->controls()&&self&&alive.current()&&!busy){self->impl_->load();}
}
bool SchedulerDialog::setSnapshot(SchedulerPresentation packet) {
  if (!packet||!impl_->loading||packet->session!=impl_->session||packet->request!=impl_->active) {return false;}
  impl_->loading=false;
  if(!valid(packet)||(impl_->baseline&&packet->revision<impl_->baseline->revision)){
    if(impl_->text(packet->failure.isEmpty()?QStringLiteral("Schedules unavailable; reload to retry."):packet->failure)){impl_->controls();}return false;
  }
  return impl_->render(std::move(packet));
}
bool SchedulerDialog::setSaveResult(SchedulerSavePresentation packet) {
  if(!packet||!impl_->saving||!impl_->baseline||packet->session!=impl_->session||packet->request!=impl_->active||packet->revision!=impl_->baseline->revision){return false;}
  const bool fresh=valid(packet->snapshot)&&packet->snapshot->session==impl_->session&&packet->snapshot->request==impl_->active&&packet->snapshot->revision>=impl_->baseline->revision;
  if((packet->saved||packet->schedulesSaved)&&!fresh){return false;}
  const auto desired=impl_->preferences();if(!desired){return false;}
  const bool close=impl_->closeAfterSave;impl_->saving=false;impl_->closeAfterSave=false;
  const QPointer<SchedulerDialog> self(this);
  if(fresh&&(packet->saved||packet->schedulesSaved)){
    if(!impl_->render(packet->snapshot)||!self){return false;}
    if(!packet->saved){
      const auto alive=impl_->guard();impl_->rendering=true;
      std::vector<std::unique_ptr<SafeSignalBlock>> blocked;
      for(QObject* object:std::initializer_list<QObject*>{impl_->enabled.data(),impl_->tags.data(),impl_->forced.data(),impl_->poll.data()}){
        if(object){blocked.push_back(std::make_unique<SafeSignalBlock>(object));}
      }
      if(!impl_->setPreferences(*desired)||!alive.current()){return false;}
      impl_->rendering=false;impl_->changed=*desired!=packet->snapshot->snapshot->preferences;
    }
  }
  if(!self){return false;}
  if(!packet->saved){
    if(impl_->text(packet->failure.isEmpty()?QStringLiteral("Save failed; edits retained."):packet->failure)){impl_->controls();}return true;
  }
  impl_->changed=false;
  if(!impl_->text(QStringLiteral("Schedules and settings saved"))||!self){return false;}
  const auto alive=impl_->guard();if(!impl_->controls()||!self||!alive.current()){return false;}
  if(close){self->done(QDialog::Accepted);}return true;
}
void SchedulerDialog::accept(){impl_->save(true);}
void SchedulerDialog::reject(){impl_->close();}
void SchedulerDialog::showEvent(QShowEvent* event){
  const QPointer<SchedulerDialog> self(this);QDialog::showEvent(event);
  if(self){QMetaObject::invokeMethod(self.data(),[self]{if(self){self->impl_->load();}},Qt::QueuedConnection);}
}
void SchedulerDialog::closeEvent(QCloseEvent* event){const QPointer<SchedulerDialog> self(this);impl_->close();if(!self||!self->isVisible()){event->accept();}else{event->ignore();}}
} // namespace biglybtc::ui
