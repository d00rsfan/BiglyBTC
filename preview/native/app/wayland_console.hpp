#pragma once
#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QtCore/qlogging.h>
#include <QtGlobal>
#include <string_view>

namespace biglybtc::app::detail {
inline bool redundantWaylandDisable(QtMsgType type,const QMessageLogContext& context,const QString& message){
  if(type!=QtWarningMsg||!context.category||std::string_view(context.category)!="qt.qpa.wayland.textinput"){return false;}
  const QStringView text(message);
  const QStringView prefix(u"virtual void QtWaylandClient::QWaylandTextInputv3::disableSurface(wl_surface*) Trying to disable 0x");
  const QStringView suffix(u" but 0x0 is focused");
  if(text.size()<prefix.size()+suffix.size()||!text.startsWith(prefix)||!text.endsWith(suffix)){return false;}
  const auto digits=text.sliced(prefix.size(),text.size()-prefix.size()-suffix.size());
  if(digits.isEmpty()||digits.size()>static_cast<qsizetype>(2*sizeof(void*))){return false;}
  bool nonzero=false;
  for(const auto digit:digits){const auto c=digit.unicode();
    if(!((c>=u'0'&&c<=u'9')||(c>=u'a'&&c<=u'f')||(c>=u'A'&&c<=u'F'))){return false;}
    nonzero=nonzero||c!=u'0';
  }
  return nonzero;
}
inline bool waylandConsoleFilterAllowed(){
  return std::string_view(qVersion())=="6.12.0"&&qgetenv("BIGLYBTC_WAYLAND_DIAGNOSTICS")!="1"
      &&!qEnvironmentVariableIsSet("QT_FATAL_WARNINGS")&&!qEnvironmentVariableIsSet("QT_MESSAGE_PATTERN")
      &&!qEnvironmentVariableIsSet("QT_LOGGING_RULES")&&!qEnvironmentVariableIsSet("QT_LOGGING_CONF");
}
inline void forwardWaylandConsole(QtMsgType type,const QMessageLogContext& context,const QString& message,QtMessageHandler next){
  if(!redundantWaylandDisable(type,context,message)){next(type,context,message);}
}
inline QtMessageHandler previousWaylandConsoleHandler=nullptr;
inline void waylandConsoleHandler(QtMsgType type,const QMessageLogContext& context,const QString& message){
  forwardWaylandConsole(type,context,message,previousWaylandConsoleHandler);
}
// Call once on the main thread BEFORE QApplication starts any Qt threads.
// Qt6.12 qInstallMessageHandler returns the real default handler when unset.
// Later explicit application handler installs supersede this wrapper normally.
inline void installWaylandConsoleFilter(){
  if(!waylandConsoleFilterAllowed()){return;}
  static const bool installed=[](){previousWaylandConsoleHandler=qInstallMessageHandler(waylandConsoleHandler);return true;}();
  (void)installed;
}
} // namespace biglybtc::app::detail
