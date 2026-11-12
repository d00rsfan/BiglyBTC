// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "file_merge_presentation.hpp"
#include <QWidget>
class QLabel;class QTableWidget;
namespace biglybtc::ui {
class FileMergeView final:public QWidget {
public:explicit FileMergeView(QWidget* parent=nullptr);void setPresentation(FileMergePresentation);
private:QLabel* summary_;QTableWidget* table_;quint64 session_=0;
};
}
