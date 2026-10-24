#pragma once

#include "options_presentation.hpp"

#include <QDialog>
#include <memory>

class QCloseEvent;
class QShowEvent;

namespace biglybtc::ui {

// GUI-only editor. The owner performs every read/save on its worker and returns
// immutable tagged packets. No profile, engine, or network object is retained.
class OptionsDialog final : public QDialog {
  Q_OBJECT
public:
  explicit OptionsDialog(QWidget* parent = nullptr);
  ~OptionsDialog() override;
  bool beginSession(quint64 session);
  void invalidateSession();
  quint64 session() const noexcept;
  quint64 revision() const noexcept;
  bool dirty() const noexcept;
  bool pending() const noexcept;

public slots:
  bool setSnapshot(biglybtc::ui::OptionsPresentation);
  bool setSaveResult(biglybtc::ui::OptionsSavePresentation);
  void setBusy(bool);
  void accept() override;
  void reject() override;

signals:
  void loadRequested(biglybtc::ui::OptionsRequest request);
  void saveRequested(biglybtc::ui::OptionsSubmission submission);

protected:
  void showEvent(QShowEvent*) override;
  void closeEvent(QCloseEvent*) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace biglybtc::ui
