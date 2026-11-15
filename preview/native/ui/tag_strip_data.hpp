// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tag_strip_settings.hpp"
#include <biglybtc/manual_tag_statistics.hpp>
#include <biglybtc/saved_categories.hpp>
#include <QHash>
#include <QVector>
#include <QSet>
#include <QString>
#include <QStringView>
#include <memory>
namespace biglybtc::ui {
enum class TagChipKind {All,Category,Uncategorised,Manual};
struct TagChip {
 QString key,name;TagChipKind kind=TagChipKind::Manual;std::optional<QString> group;
 std::optional<std::array<int,3>> colour;bool visible=true,filter=false,membershipKnown=true;
 std::optional<qint32> tagId;QSet<QString> hashes;
};
struct TagStripData {
 quint64 session=0,libraryRevision=0,tagRevision=0;QVector<TagChip> chips;QHash<QString,qsizetype> byKey;QSet<QString> categories;
 std::shared_ptr<const core::ManualTagMembership> membership;
};
using TagStripPresentation=std::shared_ptr<const TagStripData>;
enum class TagStripDataError {Source,Metadata,Limit};
std::expected<TagStripPresentation,TagStripDataError> makeTagStripData(quint64 session,quint64 libraryRevision,quint64 tagRevision,const core::SavedDownloadTags&,const core::SavedCategories&,std::span<const core::TorrentHash>);
int originalTagNameCompare(QStringView,QStringView);
bool tagChipShown(const TagChip&,const TagStripSettings&,bool completed);
// Selection is view-local; Inclusive is shared original configuration. The
// original default is All even when the categories buttons are hidden.
inline const QString AllTagChip=QStringLiteral("category:all");
class TagStripSelection final {
 TagStripPresentation data_;QSet<QString> selected_{AllTagChip};bool inclusive_=true;
public:
 void setData(TagStripPresentation data){data_=std::move(data);}
 void setInclusive(bool value){inclusive_=value;}
 void click(const QString&,bool control);
 const QSet<QString>& selected()const noexcept{return selected_;}
 void setSelected(QSet<QString> value){selected_=std::move(value);}
 bool accepts(const QString& hash,const QString& category)const;
};
}
