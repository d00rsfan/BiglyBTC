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
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace biglybtc::ui {
namespace {
constexpr int MaximumRate=std::numeric_limits<int>::max()/1024;
bool valid(const SchedulerPreferences& p) {
  return p.pollMilliseconds>=1000&&p.pollMilliseconds<=3600000;
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
  quint64 session=0, highest=0, request=0, active=0;
  bool busy=false, loading=false, saving=false, rendering=false, changed=false, closeAfterSave=false, loadWanted=false;
  SchedulerPresentation baseline;
  std::vector<std::optional<std::size_t>> sources;
  QTableWidget* table;
  QCheckBox *enabled,*tags,*forced,*enforce;
  QSpinBox *poll,*upload,*download;
  QLabel* status;
  QPushButton *apply,*ok,*cancel,*reload,*add,*remove;
  QPointer<QMessageBox> question;
  SchedulerPreferences preferences() const { return {enabled->isChecked(),tags->isChecked(),forced->isChecked(),poll->value()}; }
  void setPreferences(const SchedulerPreferences& p) {
    enabled->setChecked(p.enabled);tags->setChecked(p.useTags);forced->setChecked(p.overrideForced);poll->setValue(p.pollMilliseconds);
  }
  void controls() {
    const bool editable=session&&baseline&&!busy&&!loading&&!saving;
    for (QWidget* widget : std::initializer_list<QWidget*>{table,enabled,tags,forced,enforce,poll,upload,download,add,remove}) { widget->setEnabled(editable); }
    apply->setEnabled(editable&&changed);ok->setEnabled(editable);cancel->setEnabled(!saving);
    reload->setEnabled(session&&!busy&&!loading&&!saving&&!changed);
  }
  void edited() {
    if (rendering||!baseline||saving||loading) { return; }
    changed=true;status->setText(QStringLiteral("Unsaved changes"));controls();
  }
  void load() {
    if (!session||busy||loading||saving||!loadWanted||!q->isVisible()) { return; }
    loadWanted=false;loading=true;active=++request;controls();emit q->loadRequested({session,active});
  }
  QCheckBox* check(int row,int column,bool value) {
    auto* box=new QCheckBox(table);box->setChecked(value);table->setCellWidget(row,column,box);
    QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});return box;
  }
  void append(const schedule::Schedule& s,std::optional<std::size_t> source) {
    const int row=table->rowCount();table->insertRow(row);sources.push_back(source);
    check(row,0,s.enabled);
    auto* days=new QWidget(table);auto* daysLayout=new QHBoxLayout(days);daysLayout->setContentsMargins(0,0,0,0);
    const std::array<QString,7> names{QStringLiteral("Su"),QStringLiteral("Mo"),QStringLiteral("Tu"),QStringLiteral("We"),QStringLiteral("Th"),QStringLiteral("Fr"),QStringLiteral("Sa")};
    for (int day=0;day<7;++day) {
      auto* box=new QCheckBox(names[day],days);box->setObjectName(QStringLiteral("day-%1").arg(day));box->setChecked(s.days[day]);
      daysLayout->addWidget(box);QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});
    }
    table->setCellWidget(row,1,days);
    for (int column : {2,3}) {
      const int minute=column==2?s.start:s.end;auto* time=new QTimeEdit(QTime(minute/60,minute%60),table);
      time->setDisplayFormat(QStringLiteral("HH:mm"));table->setCellWidget(row,column,time);
      QObject::connect(time,&QTimeEdit::timeChanged,q,[this]{edited();});
    }
    for (int column : {4,5}) {
      auto* rate=new QSpinBox(table);rate->setRange(0,MaximumRate);rate->setSpecialValueText(QStringLiteral("Unlimited"));
      rate->setValue(column==4?s.upload:s.download);table->setCellWidget(row,column,rate);
      QObject::connect(rate,&QSpinBox::valueChanged,q,[this]{edited();});
    }
    check(row,6,s.downloadsPaused);check(row,7,s.seedsPaused);
    auto* category=new QLineEdit(table);category->setMaxLength(4096);
    if (s.category) { category->setText(QString::fromUtf8(*s.category)); }
    category->setProperty("wasNull",!s.category);table->setCellWidget(row,8,category);
    QObject::connect(category,&QLineEdit::textChanged,q,[this,category]{category->setProperty("wasNull",false);edited();});
    check(row,9,s.onlyCategory);check(row,10,s.excludeCategory);
  }
  SchedulerSubmission submission() const {
    SchedulerSubmission value{session,active,baseline->revision,preferences(),upload->value(),download->value(),enforce->isChecked(),{}};
    for (int row=0;row<table->rowCount();++row) {
      schedule::Schedule s;
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
  void render(SchedulerPresentation packet) {
    rendering=true;baseline=std::move(packet);table->setRowCount(0);sources.clear();
    setPreferences(baseline->snapshot->preferences);
    const auto& c=baseline->snapshot->configuration;
    upload->setValue(c.defaultUpload);download->setValue(c.defaultDownload);enforce->setChecked(c.defaultsEnforce);
    for (std::size_t i=0;i<c.schedules.size();++i) {append(c.schedules[i],i);}
    changed=false;rendering=false;
    status->setText(baseline->snapshot->diagnostic.isEmpty()?QStringLiteral("Schedules loaded. Zero speed means unlimited."):baseline->snapshot->diagnostic);
    controls();
  }
  void save(bool close) {
    if (!baseline||busy||loading||saving) {return;}
    if (!changed) {if(close){q->done(QDialog::Accepted);}return;}
    closeAfterSave=close;saving=true;active=++request;auto value=submission();controls();emit q->saveRequested(std::move(value));
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
  impl_->poll=spin(QStringLiteral("Check schedules every (milliseconds)"),QStringLiteral("scheduler-poll"),1000,3600000);
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
  connect(impl_->add,&QPushButton::clicked,this,[this]{if(impl_->sources.size()>=schedule::MaximumSchedules){return;}schedule::Schedule value;value.days.fill(true);impl_->append(value,{});impl_->edited();});
  connect(impl_->remove,&QPushButton::clicked,this,[this]{const int r=impl_->table->currentRow();if(r>=0){impl_->table->removeRow(r);impl_->sources.erase(impl_->sources.begin()+r);impl_->edited();}});
  impl_->status=new QLabel(this);impl_->status->setWordWrap(true);impl_->status->setTextFormat(Qt::PlainText);impl_->status->setObjectName(QStringLiteral("scheduler-status"));layout->addWidget(impl_->status);
  auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);impl_->apply=buttons->button(QDialogButtonBox::Apply);impl_->ok=buttons->button(QDialogButtonBox::Ok);impl_->cancel=buttons->button(QDialogButtonBox::Cancel);
  impl_->reload=buttons->addButton(QStringLiteral("Reload"),QDialogButtonBox::ResetRole);impl_->apply->setObjectName(QStringLiteral("scheduler-apply"));impl_->ok->setObjectName(QStringLiteral("scheduler-ok"));impl_->reload->setObjectName(QStringLiteral("scheduler-reload"));
  connect(impl_->apply,&QPushButton::clicked,this,[this]{impl_->save(false);});connect(impl_->ok,&QPushButton::clicked,this,&SchedulerDialog::accept);connect(impl_->cancel,&QPushButton::clicked,this,&SchedulerDialog::reject);
  connect(impl_->reload,&QPushButton::clicked,this,[this]{impl_->loadWanted=true;impl_->load();});layout->addWidget(buttons);impl_->controls();
}
SchedulerDialog::~SchedulerDialog()=default;
bool SchedulerDialog::beginSession(quint64 session) {
  if (!session||session<=impl_->highest) {return false;}invalidateSession();impl_->session=session;impl_->highest=session;impl_->loadWanted=true;impl_->load();return true;
}
void SchedulerDialog::invalidateSession() {impl_->session=0;impl_->baseline.reset();impl_->changed=false;impl_->loadWanted=false;impl_->loading=impl_->saving=false;if(impl_->question){impl_->question->close();}impl_->controls();}
bool SchedulerDialog::dirty() const noexcept {return impl_->changed;}
bool SchedulerDialog::pending() const noexcept {return impl_->loading||impl_->saving;}
void SchedulerDialog::setBusy(bool busy) {impl_->busy=busy;impl_->controls();if(!busy){impl_->load();}}
bool SchedulerDialog::setSnapshot(SchedulerPresentation packet) {
  if (!packet||!impl_->loading||packet->session!=impl_->session||packet->request!=impl_->active) {return false;}
  impl_->loading=false;
  if(!valid(packet)){impl_->status->setText(packet->failure.isEmpty()?QStringLiteral("Schedules unavailable; reload to retry."):packet->failure);impl_->controls();return false;}
  impl_->render(std::move(packet));return true;
}
bool SchedulerDialog::setSaveResult(SchedulerSavePresentation packet) {
  if(!packet||!impl_->saving||!impl_->baseline||packet->session!=impl_->session||packet->request!=impl_->active||packet->revision!=impl_->baseline->revision){return false;}
  const bool fresh=valid(packet->snapshot)&&packet->snapshot->session==impl_->session&&packet->snapshot->request==impl_->active&&packet->snapshot->revision>=impl_->baseline->revision;
  if((packet->saved||packet->schedulesSaved)&&!fresh){return false;}
  const auto desired=impl_->preferences();const bool close=impl_->closeAfterSave;impl_->saving=false;impl_->closeAfterSave=false;
  if(fresh&&(packet->saved||packet->schedulesSaved)){
    impl_->render(packet->snapshot);
    if(!packet->saved){impl_->rendering=true;impl_->setPreferences(desired);impl_->rendering=false;impl_->changed=desired!=packet->snapshot->snapshot->preferences;}
  }
  if(!packet->saved){impl_->status->setText(packet->failure.isEmpty()?QStringLiteral("Save failed; edits retained."):packet->failure);impl_->controls();return true;}
  impl_->changed=false;impl_->status->setText(QStringLiteral("Schedules and settings saved"));impl_->controls();if(close){done(QDialog::Accepted);}return true;
}
void SchedulerDialog::accept(){impl_->save(true);}
void SchedulerDialog::reject(){impl_->close();}
void SchedulerDialog::showEvent(QShowEvent* event){QDialog::showEvent(event);impl_->load();}
void SchedulerDialog::closeEvent(QCloseEvent* event){const QPointer<SchedulerDialog> self(this);impl_->close();if(!self||!self->isVisible()){event->accept();}else{event->ignore();}}
} // namespace biglybtc::ui
