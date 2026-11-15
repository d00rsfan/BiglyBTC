// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QCoreApplication>
#include <QString>
namespace biglybtc::ui {
// TagBase.getTagName(true): only original built-in tag.* names are translated.
// These default strings come from MessagesBundle at cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// The source/raw name remains unchanged in profile stores and editor commands.
inline QString originalTagDisplayName(const QString& raw){
 if(raw==QStringLiteral("tag.type.ds")){return QCoreApplication::translate("OriginalTagNames","Download State");}
 if(raw==QStringLiteral("tag.type.ds.init")){return QCoreApplication::translate("OriginalTagNames","Initializing");}
 if(raw==QStringLiteral("tag.type.ds.down")){return QCoreApplication::translate("OriginalTagNames","Downloading");}
 if(raw==QStringLiteral("tag.type.ds.seed")){return QCoreApplication::translate("OriginalTagNames","Seeding");}
 if(raw==QStringLiteral("tag.type.ds.qford")){return QCoreApplication::translate("OriginalTagNames","Queued for Download");}
 if(raw==QStringLiteral("tag.type.ds.qfors")){return QCoreApplication::translate("OriginalTagNames","Queued for Seeding");}
 if(raw==QStringLiteral("tag.type.ds.stop")){return QCoreApplication::translate("OriginalTagNames","Stopped");}
 if(raw==QStringLiteral("tag.type.ds.err")){return QCoreApplication::translate("OriginalTagNames","Error");}
 if(raw==QStringLiteral("tag.type.ds.act")){return QCoreApplication::translate("OriginalTagNames","Active");}
 if(raw==QStringLiteral("tag.type.ds.pau")){return QCoreApplication::translate("OriginalTagNames","Paused");}
 if(raw==QStringLiteral("tag.type.ds.inact")){return QCoreApplication::translate("OriginalTagNames","Inactive");}
 if(raw==QStringLiteral("tag.type.ds.comp")){return QCoreApplication::translate("OriginalTagNames","Complete");}
 if(raw==QStringLiteral("tag.type.ds.incomp")){return QCoreApplication::translate("OriginalTagNames","Incomplete");}
 if(raw==QStringLiteral("tag.type.man.vhdn")){return QCoreApplication::translate("OriginalTagNames","VHDN");}
 if(raw==QStringLiteral("tag.type.man.featcon")){return QCoreApplication::translate("OriginalTagNames","Featured Content");}
 if(raw==QStringLiteral("tag.type.man")){return QCoreApplication::translate("OriginalTagNames","Manual");}
 if(raw==QStringLiteral("tag.type.ipset")){return QCoreApplication::translate("OriginalTagNames","Peer Set");}
 if(raw==QStringLiteral("tag.type.internal")){return QCoreApplication::translate("OriginalTagNames","Internal");}
 if(raw==QStringLiteral("tag.type.ds.mov")){return QCoreApplication::translate("OriginalTagNames","Moving");}
 if(raw==QStringLiteral("tag.type.ds.chk")){return QCoreApplication::translate("OriginalTagNames","Checking");}
 if(raw==QStringLiteral("tag.type.ds.del")){return QCoreApplication::translate("OriginalTagNames","Deleting");}
 if(raw==QStringLiteral("tag.type.swarm")){return QCoreApplication::translate("OriginalTagNames","Swarm Tag");}
 if(raw==QStringLiteral("tag.name.product.updates")){return QCoreApplication::translate("OriginalTagNames","BiglyBT Updates");}
 if(raw==QStringLiteral("tag.type.category")){return QCoreApplication::translate("OriginalTagNames","Category");}
 return raw;
}
}
