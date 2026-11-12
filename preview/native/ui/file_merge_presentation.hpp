// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QMetaType>
#include <memory>
#include <vector>
namespace biglybtc::ui {
struct FileMergeRow {QString source,target,state;int sourceFile=0,targetFile=0; qint64 submitted=0;quint64 verified=0;unsigned failures=0;};
struct FileMergePacket {quint64 session=0;bool enabled=false,truncated=false;QString state;quint64 candidates=0,pending=0,reservedBytes=0,verified=0,rejected=0;qint64 submitted=0;std::vector<FileMergeRow> rows;};
using FileMergePresentation=std::shared_ptr<const FileMergePacket>;
}
Q_DECLARE_METATYPE(biglybtc::ui::FileMergePresentation)
