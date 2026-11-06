// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../app/category_commands.hpp"
#include <QDialog>
#include <QPointer>
class QTableWidget;class QLineEdit;class QLabel;class QPushButton;
namespace biglybtc::ui {
std::shared_ptr<const core::SavedCategories> categoryEditorValues(CategoryCommandPresentation);
class CategoryManagerDialog final:public QDialog {
  Q_OBJECT
public:
  explicit CategoryManagerDialog(quint64 session,QWidget* parent=nullptr);
  void setPresentation(CategoryCommandPresentation);
  void setPending(const CategoryCommand&);
  void setResult(CategoryCommandPresentation);
  void setAvailable(bool);
  void showFailure(const QString&);
  void invalidateSession();
signals:
  void commandRequested(biglybtc::ui::CategoryCommand);
private:
  void updateButtons();void create();void remove();void resumeRemove();void intent(CategoryCommand);
  const core::SavedCategory* selected()const;
  quint64 session_=0,generation_=0,lastRequest_=0;
  bool available_=true,writable_=false,deletionSupported_=false;
  CategoryRecoveryInfo recovery_;
  std::optional<CategoryCommand> pending_;
  std::shared_ptr<const core::SavedCategories> categories_;
  QPointer<QTableWidget> table_;QPointer<QLineEdit> name_;QPointer<QLabel> status_;
  QPointer<QPushButton> create_,remove_,reload_,resume_;
};
} // namespace biglybtc::ui
