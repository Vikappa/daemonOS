#include "daemon/apps/modules.hpp"
namespace daemonos {
AppId ClockApp::id() const { return AppId::clock; }
std::string_view ClockApp::title() const { return "Clock"; }
}
