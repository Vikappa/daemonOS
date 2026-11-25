#pragma once
#include "daemon/types.hpp"
#include <span>

namespace daemonos {
// A configured, bounded catalog. The APK maps these opaque IDs to real providers/apps.
class BridgeServiceCatalog {
public:
    Status add(const ServiceBinding& binding);
    std::span<const ServiceBinding> bindings() const { return {items_.data(), size_}; }
    bool supports(ToolName name, std::string_view binding) const;
private:
    std::array<ServiceBinding, budget::service_bindings> items_{};
    std::size_t size_{};
};
// The adapter owns authentication, transport and durable phone-side deduplication.
// dispatch means acceptance, not completion. poll peeks until accept_result after commit.
struct PhoneTools {
    virtual ~PhoneTools() = default;
    virtual bool supports(ToolName name) const = 0;
    virtual std::span<const ServiceBinding> services() const { return {}; }
    virtual bool supports_binding(ToolName name, std::string_view binding) const {
        if(binding.empty()) return supports(name);
        for(const auto& service:services())
            if(service.id.view()==binding && (service.operations & tool_bit(name))) return true;
        return false;
    }
    virtual bool authenticated() const = 0;
    virtual LinkState link_state() const { return authenticated() ? LinkState::ready : LinkState::offline; }
    virtual Status dispatch(const ToolCall& call) = 0;
    virtual bool poll(ToolResult& result) = 0;
    virtual void accept_result(std::string_view id) = 0;
};
using BridgeServices = PhoneTools;
Status validate_service_result(const ToolResult& result);
}
