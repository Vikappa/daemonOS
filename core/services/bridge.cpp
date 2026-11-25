#include "daemon/services/bridge.hpp"

namespace daemonos {
Status BridgeServiceCatalog::add(const ServiceBinding& binding) {
    if(!valid_identifier(binding.id.view()) || binding.label.view().empty() || !binding.operations ||
        (binding.operations & ~bridge_tools)) return Status::fail(Error::invalid,"invalid service binding");
    for(const auto& existing:bindings()) if(existing.id==binding.id) return Status::fail(Error::denied,"duplicate service binding");
    if(size_==items_.size()) return Status::fail(Error::full,"service catalog full");
    items_[size_++]=binding; return {};
}
bool BridgeServiceCatalog::supports(ToolName name, std::string_view binding) const {
    for(const auto& service:bindings()) if((binding.empty() || service.id.view()==binding) && (service.operations & tool_bit(name))) return true;
    return false;
}
Status validate_service_result(const ToolResult& result) {
    if(!valid_identifier(result.id.view()) || result.source_count>result.sources.size() || result.error>ServiceError::unknown ||
        (result.success && result.error!=ServiceError::none)) return Status::fail(Error::invalid,"invalid service result");
    for(std::size_t i=0;i<result.source_count;++i) {
        const auto url=result.sources[i].url.view();
        if((!url.starts_with("https://") && !url.starts_with("http://")) || url.ends_with("://")) return Status::fail(Error::invalid,"invalid result source URL");
        for(const unsigned char c:url) if(c<=0x20 || c==0x7f) return Status::fail(Error::invalid,"control character in result source URL");
    }
    return {};
}
}
