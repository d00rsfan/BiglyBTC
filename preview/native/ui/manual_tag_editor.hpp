// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../app/manual_tag_commands.hpp"
#include <QDialog>
#include <QPointer>
class QTableWidget;class QLineEdit;class QLabel;class QPushButton;class QCheckBox;
namespace biglybtc::ui {
struct ManualTagEditorRow {
  std::optional<qint32> id;
  QString label,name,detail;
  bool editable=false,canAddMember=false,canRemoveMember=false,visible=true;
  std::size_t members=0;
};
struct ManualTagEditorValues {
  std::shared_ptr<const core::SavedDownloadTags> tags;
  std::vector<ManualTagEditorRow> rows;
  bool writable=false;
};
// Fresh bounded projection from owned source bytes; GUI hints confer no worker
// permission. Unknown/localization names and malformed rows remain explicit.
std::optional<ManualTagEditorValues> manualTagEditorValues(ManualTagCommandPresentation);
class ManualTagManagerDialog final:public QDialog {
  Q_OBJECT
public:
  explicit ManualTagManagerDialog(quint64 session,QWidget* parent=nullptr);
  void setPresentation(ManualTagCommandPresentation);
  void setPending(const ManualTagCommand&);
  void setResult(ManualTagCommandPresentation);
  void setAvailable(bool);
  void showFailure(const QString&);
  void invalidateSession();
signals:
  void commandRequested(biglybtc::ui::ManualTagCommand);
private:
  void selectRow();void updateButtons();void create();void rename();void remove();void visibility(bool);
  void intent(ManualTagCommand);
  const ManualTagEditorRow* selected() const;
  quint64 session_=0,generation_=0,lastRequest_=0;
  bool available_=true,writable_=false,rendering_=false;
  std::optional<ManualTagCommand> pending_;
  std::vector<ManualTagEditorRow> rows_;
  QPointer<QTableWidget> table_;QPointer<QLineEdit> name_;QPointer<QLabel> status_;
  QPointer<QPushButton> create_,rename_,remove_,reload_;QPointer<QCheckBox> visible_;
};
} // namespace biglybtc::ui
