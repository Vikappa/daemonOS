#include "daemon/apps/modules.hpp"
namespace daemonos {
AppId CreatureApp::id() const { return AppId::creature; }
std::string_view CreatureApp::title() const { return "Familiar"; }
Capabilities CreatureApp::capabilities() const {
    return static_cast<Capabilities>(Capability::animate | Capability::speak) |
           static_cast<Capabilities>(Capability::remember);
}
}
