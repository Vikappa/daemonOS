#include "daemon/apps/modules.hpp"
namespace daemonos {
AppId LauncherApp::id() const { return AppId::launcher; }
std::string_view LauncherApp::title() const { return "DaeMON OS"; }
}
