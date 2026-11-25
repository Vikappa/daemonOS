#include "daemon/apps/modules.hpp"
namespace daemonos {
AppId SettingsApp::id() const { return AppId::settings; }
std::string_view SettingsApp::title() const { return "Settings"; }
}
