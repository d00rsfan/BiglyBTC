// SPDX-License-Identifier: GPL-2.0-or-later
#include "file_merge_view.hpp"
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QSignalBlocker>
namespace biglybtc::ui {
FileMergeView::FileMergeView(QWidget* parent):QWidget(parent){
 setObjectName(QStringLiteral("swarm-file-merge"));auto* layout=new QVBoxLayout(this);summary_=new QLabel(tr("Waiting for a library session"),this);summary_->setWordWrap(true);layout->addWidget(summary_);
 auto* note=new QLabel(tr("Local reuse requires a matching target hash. These bytes are excluded from network download totals. Configure merging in Tools → Options → Files."),this);note->setWordWrap(true);layout->addWidget(note);
 table_=new QTableWidget(this);table_->setColumnCount(7);table_->setHorizontalHeaderLabels({tr("Source torrent / file"),tr("Target torrent / file"),tr("Supplied bytes"),tr("Verified pieces"),tr("Failures"),tr("State"),tr("Source scope")});table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->horizontalHeader()->setStretchLastSection(true);table_->setColumnWidth(0,240);table_->setColumnWidth(1,240);table_->setColumnWidth(5,280);layout->addWidget(table_);
}
void FileMergeView::setPresentation(FileMergePresentation p){
 if(!p||!p->session||p->session<session_){return;}session_=p->session;
 summary_->setText(tr("%1 — %2 candidates; %3 pending; %4 MiB reserved; %5 bytes supplied; %6 engine-verified pieces; %7 rejected candidates%8").arg(p->enabled?p->state:tr("Disabled")).arg(p->candidates).arg(p->pending).arg(double(p->reservedBytes)/1048576.0,0,'f',1).arg(p->submitted).arg(p->verified).arg(p->rejected).arg(p->truncated?tr(" (bounded view)"):QString{}));
 const QSignalBlocker block(table_);const auto top=table_->verticalScrollBar()->value(),left=table_->horizontalScrollBar()->value();const auto selected=table_->currentRow();table_->setRowCount(int(p->rows.size()));
 for(std::size_t i=0;i<p->rows.size();++i){const auto& r=p->rows[i];const QStringList values{r.source+QStringLiteral(" / %1").arg(r.sourceFile+1),r.target+QStringLiteral(" / %1").arg(r.targetFile+1),QString::number(r.submitted),QString::number(r.verified),QString::number(r.failures),r.state,tr("Local retained files")};for(int col=0;col<values.size();++col){auto* item=table_->item(int(i),col);if(!item){item=new QTableWidgetItem;table_->setItem(int(i),col,item);}if(item->text()!=values[col]){item->setText(values[col]);}}}
 if(selected>=0&&selected<table_->rowCount()&&table_->currentRow()!=selected){table_->setCurrentCell(selected,0,QItemSelectionModel::NoUpdate);}table_->verticalScrollBar()->setValue(top);table_->horizontalScrollBar()->setValue(left);
}
}
