from pathlib import Path
base=Path(__file__).resolve().parent
p=base/'before/preview/native/ui/scheduler_dialog.cpp'
s=p.read_text()
def swap(old,new):
 global s
 assert s.count(old)==1,old[:100]
 s=s.replace(old,new)
swap('#include <limits>','#include <limits>\n#include <optional>\n#include <vector>')
swap('constexpr int MaximumRate=std::numeric_limits<int>::max()/1024;','''constexpr int MaximumRate=std::numeric_limits<int>::max()/1024;
// QObject::destroyed is delivered even while signals are blocked. Restore only
// surviving objects; a normal QSignalBlocker can outlive its deleted target.
class SafeSignalBlock final {
public:
  explicit SafeSignalBlock(QObject* object):object_(object),previous_(object->blockSignals(true)) {}
  ~SafeSignalBlock() { if(object_) {object_->blockSignals(previous_);} }
private:
  QPointer<QObject> object_; bool previous_;
};''')
swap('  quint64 session=0, highest=0, request=0, active=0;','  quint64 session=0, highest=0, request=0, active=0, serial=0;')
swap('''  QTableWidget* table;
  QCheckBox *enabled,*tags,*forced,*enforce;
  QSpinBox *poll,*upload,*download;
  QLabel* status;
  QPushButton *apply,*ok,*cancel,*reload,*add,*remove;''','''  QPointer<QTableWidget> table;
  QPointer<QCheckBox> enabled,tags,forced,enforce;
  QPointer<QSpinBox> poll,upload,download;
  QPointer<QLabel> status;
  QPointer<QPushButton> apply,ok,cancel,reload,add,remove;''')
start=s.index('  SchedulerPreferences preferences() const')
end=s.index('  QCheckBox* check(',start)
s=s[:start]+'''  struct Guard {
    QPointer<SchedulerDialog> self; const Impl* owner; quint64 expected;
    bool current() const {return self&&self->impl_.get()==owner&&owner->serial==expected;}
  };
  Guard guard() const {return {q,this,serial};}
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
    const bool editable=session&&baseline&&!busy&&!loading&&!saving;
    const std::vector<QPointer<QWidget>> widgets{table.data(),enabled.data(),tags.data(),forced.data(),enforce.data(),
        poll.data(),upload.data(),download.data(),add.data(),remove.data()};
    for(const auto& widget:widgets){if(widget){widget->setEnabled(editable);}if(!alive.current()){return false;}}
    if(apply){apply->setEnabled(editable&&changed);}if(!alive.current()){return false;}
    if(ok){ok->setEnabled(editable);}if(!alive.current()){return false;}
    if(cancel){cancel->setEnabled(!saving);}if(!alive.current()){return false;}
    if(reload){reload->setEnabled(session&&!busy&&!loading&&!saving&&!changed);}
    return alive.current();
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
    if(!controls()||!alive.current()){return;}emit q->loadRequested({session,active});
  }
''' + s[end:]
swap('  void append(const schedule::Schedule& s,std::optional<std::size_t> source) {\n    const int row=table->rowCount();table->insertRow(row);sources.push_back(source);',
'''  bool append(const schedule::Schedule& s,std::optional<std::size_t> source) {
    const auto alive=guard();if(!table){return false;}
    const int row=table->rowCount();table->insertRow(row);if(!alive.current()||!table){return false;}sources.push_back(source);''')
swap('    QObject::connect(category,&QLineEdit::textChanged,q,[this,category]{category->setProperty("wasNull",false);edited();});\n    check(row,9,s.onlyCategory);check(row,10,s.excludeCategory);',
'''    QObject::connect(category,&QLineEdit::textChanged,q,[self=QPointer<SchedulerDialog>(q),field=QPointer<QLineEdit>(category)]{
      if(!self||!field){return;}const auto alive=self->impl_->guard();field->setProperty("wasNull",false);
      if(alive.current()){self->impl_->edited();}
    });
    check(row,9,s.onlyCategory);if(!alive.current()||!table){return false;}
    check(row,10,s.excludeCategory);return alive.current();''')
# Guard every table mutation that can publish model/child callbacks.
swap('    check(row,0,s.enabled);','    check(row,0,s.enabled);if(!alive.current()||!table){return false;}')
swap('    table->setCellWidget(row,1,days);','    table->setCellWidget(row,1,days);if(!alive.current()||!table){return false;}')
swap('time->setDisplayFormat(QStringLiteral("HH:mm"));table->setCellWidget(row,column,time);',
     'time->setDisplayFormat(QStringLiteral("HH:mm"));table->setCellWidget(row,column,time);if(!alive.current()||!table){return false;}')
swap('rate->setValue(column==4?s.upload:s.download);table->setCellWidget(row,column,rate);',
     'rate->setValue(column==4?s.upload:s.download);table->setCellWidget(row,column,rate);if(!alive.current()||!table){return false;}')
swap('    check(row,6,s.downloadsPaused);check(row,7,s.seedsPaused);',
     '    check(row,6,s.downloadsPaused);if(!alive.current()||!table){return false;}\n    check(row,7,s.seedsPaused);if(!alive.current()||!table){return false;}')
swap('category->setProperty("wasNull",!s.category);table->setCellWidget(row,8,category);',
     'category->setProperty("wasNull",!s.category);table->setCellWidget(row,8,category);if(!alive.current()||!table){return false;}')
swap('  SchedulerSubmission submission() const {\n    SchedulerSubmission value{session,active,baseline->revision,preferences(),upload->value(),download->value(),enforce->isChecked(),{}};',
'''  std::optional<SchedulerSubmission> submission() const {
    const auto settings=preferences();
    if(!settings||!baseline||!table||!upload||!download||!enforce||sources.size()!=static_cast<std::size_t>(table->rowCount())){return {};}
    SchedulerSubmission value{session,active,baseline->revision,*settings,upload->value(),download->value(),enforce->isChecked(),{}};''')
swap('''      schedule::Schedule s;
      auto boolean=[&](int column){return qobject_cast<QCheckBox*>(table->cellWidget(row,column))->isChecked();};''',
'''      schedule::Schedule s;
      for(int column:{0,6,7,9,10}){if(!qobject_cast<QCheckBox*>(table->cellWidget(row,column))){return {};}}
      for(int column:{2,3}){if(!qobject_cast<QTimeEdit*>(table->cellWidget(row,column))){return {};}}
      for(int column:{4,5}){if(!qobject_cast<QSpinBox*>(table->cellWidget(row,column))){return {};}}
      if(!qobject_cast<QLineEdit*>(table->cellWidget(row,8))||!table->cellWidget(row,1)){return {};}
      for(int day=0;day<7;++day){if(!table->cellWidget(row,1)->findChild<QCheckBox*>(QStringLiteral("day-%1").arg(day))){return {};}}
      auto boolean=[&](int column){return qobject_cast<QCheckBox*>(table->cellWidget(row,column))->isChecked();};''')
start=s.index('  void render(SchedulerPresentation packet)')
end=s.index('  void close() {',start)
s=s[:start]+'''  bool render(SchedulerPresentation packet) {
    if(!advance()){return false;}const auto alive=guard();
    rendering=true;baseline=std::move(packet);
    struct Restore {
      Guard alive;~Restore(){if(alive.current()){alive.self->impl_->rendering=false;}}
    } restore{alive};
    std::vector<std::unique_ptr<SafeSignalBlock>> blocked;
    const std::vector<QPointer<QObject>> objects{table.data(),table?table->model():nullptr,enabled.data(),tags.data(),
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
''' + s[end:]
swap('''      if (!self||self->impl_->session!=expected) {return;}self->impl_->question.clear();''',
'''      if (!self||self->impl_->session!=expected) {return;}self->impl_->question.clear();''')
# Header/table creation remains identical. Add/remove callbacks guard actual model emissions.
swap('''  connect(impl_->add,&QPushButton::clicked,this,[this]{if(impl_->sources.size()>=schedule::MaximumSchedules){return;}schedule::Schedule value;value.days.fill(true);impl_->append(value,{});impl_->edited();});
  connect(impl_->remove,&QPushButton::clicked,this,[this]{const int r=impl_->table->currentRow();if(r>=0){impl_->table->removeRow(r);impl_->sources.erase(impl_->sources.begin()+r);impl_->edited();}});''',
'''  connect(impl_->add,&QPushButton::clicked,this,[self=QPointer<SchedulerDialog>(this)]{
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
  });''')
start=s.index('bool SchedulerDialog::beginSession(')
end=s.index('bool SchedulerDialog::dirty()',start)
s=s[:start]+'''bool SchedulerDialog::beginSession(quint64 session) {
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
''' + s[end:]
swap('void SchedulerDialog::setBusy(bool busy) {impl_->busy=busy;impl_->controls();if(!busy){impl_->load();}}',
'''void SchedulerDialog::setBusy(bool busy) {
  const QPointer<SchedulerDialog> self(this);const auto alive=impl_->guard();impl_->busy=busy;
  if(impl_->controls()&&self&&alive.current()&&!busy){self->impl_->load();}
}''')
swap('''  impl_->loading=false;
  if(!valid(packet)){impl_->status->setText(packet->failure.isEmpty()?QStringLiteral("Schedules unavailable; reload to retry."):packet->failure);impl_->controls();return false;}
  impl_->render(std::move(packet));return true;''',
'''  impl_->loading=false;
  if(!valid(packet)||(impl_->baseline&&packet->revision<impl_->baseline->revision)){
    if(impl_->text(packet->failure.isEmpty()?QStringLiteral("Schedules unavailable; reload to retry."):packet->failure)){impl_->controls();}return false;
  }
  return impl_->render(std::move(packet));''')
start=s.index('  const auto desired=impl_->preferences();const bool close=impl_->closeAfterSave;')
end=s.index('\n}\nvoid SchedulerDialog::accept()',start)
s=s[:start]+'''  const auto desired=impl_->preferences();if(!desired){return false;}
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
  if(close){self->done(QDialog::Accepted);}return true;''' + s[end:]
swap('void SchedulerDialog::showEvent(QShowEvent* event){QDialog::showEvent(event);impl_->load();}',
'''void SchedulerDialog::showEvent(QShowEvent* event){
  const QPointer<SchedulerDialog> self(this);QDialog::showEvent(event);if(self){self->impl_->load();}
}''')
# check() may deliver direct cell/model callbacks; do not connect a deleted checkbox.
swap('''    auto* box=new QCheckBox(table);box->setChecked(value);table->setCellWidget(row,column,box);
    QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});return box;''',
'''    const auto alive=guard();if(!table){return nullptr;}
    QPointer<QCheckBox> box=new QCheckBox(table);box->setChecked(value);table->setCellWidget(row,column,box);
    if(!alive.current()||!box){return nullptr;}
    QObject::connect(box,&QCheckBox::toggled,q,[this]{edited();});return box.data();''')
(base/'staging/preview/native/ui/scheduler_dialog.cpp').write_text(s)
print(len(s.splitlines()))
