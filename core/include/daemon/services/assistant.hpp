#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/store.hpp"
#include "daemon/services/bridge.hpp"
#include "daemon/services/voice_directory.hpp"
#include <limits>

namespace daemonos {
struct ToolPolicy {
    bool notes{true}, reminders{true}, phone_alarms{}, phone_messages{};
    bool agent_tasks{true};
    ToolMask services{}; // Additional locally enabled bridge operations.
};
Status validate_tool(const ToolCall& call);
class AssistantService {
public:
    AssistantService(hal::Clock& clock, StateStore& store, VoiceDirectory* identities = nullptr) : clock_(clock), store_(store), identities_(identities) {}
    void phone(PhoneTools* phone, ToolPolicy policy) { phone_ = phone; policy_ = policy; storage_changed(); }
    ToolPolicy tools() const {
        auto result=policy_;
        result.phone_alarms = result.phone_alarms && phone_ && phone_->supports(ToolName::set_alarm);
        result.phone_messages = result.phone_messages && phone_ && phone_->supports(ToolName::send_message);
        for(unsigned i=0;i<static_cast<unsigned>(ToolName::count);++i) {
            const auto name=static_cast<ToolName>(i);
            if(!phone_ || !phone_->supports(name)) result.services &= ~tool_bit(name);
        }
        return result;
    }
    ToolMask tool_mask() const;
    std::span<const ServiceBinding> service_bindings() const { return phone_ ? phone_->services() : std::span<const ServiceBinding>{}; }
    bool phone_configured() const { return phone_!=nullptr; }
    LinkState phone_state() const { return phone_ ? phone_->link_state() : LinkState::offline; }
    bool waiting_for_service() const { return !active_id_.view().empty(); }
    Status validate_calls(std::span<const ToolCall> calls, ToolMask allowed) const;
    Status submit(std::span<const ToolCall> calls);
    Status submit(const UserSession& session,std::span<const ToolCall> calls);
    Status confirm(const UserSession& owner,std::string_view id);
    Status notes(const UserSession& session,std::string_view query,std::size_t offset,AssistantPage& page);
    Status confirmation(const UserSession& owner,ToolJob& job);
    Status job(const UserSession& session,std::string_view id,ToolJob& value);
    Status cancel(const UserSession& owner,std::string_view id);
    Status complete_reminder(const UserSession& owner,std::int64_t id,std::int64_t snooze_seconds = 0);
    Status confirm(std::string_view id); // Local UI authorization for the stored, immutable payload.
    Status confirmation(ToolJob& job) { return secured() ? Status::fail(Error::denied,"owner session required") : store_.tool_confirmation(job); }
    Status job(std::string_view id, ToolJob& value) { return secured() ? Status::fail(Error::denied,"identity session required") : store_.tool_job(id,value); }
    Status cancel(std::string_view id);
    Status complete_reminder(std::int64_t id, std::int64_t snooze_seconds = 0);
    Status notes(std::string_view query, std::size_t offset, AssistantPage& page) { page={}; return secured() ? Status::fail(Error::denied,"identity session required") : store_.entries(EntryKind::note, query, offset, page); }
    void tick();
    void storage_changed() { next_scan_ms_ = 0; pending_work_=true; }
    std::int64_t next_deadline_ms() const { return !active_id_.view().empty() ? std::min(next_scan_ms_,clock_.monotonic_ms()+20) : pending_work_ ? next_scan_ms_ : std::numeric_limits<std::int64_t>::max(); }
    std::string_view reply() const { return reply_.view(); }
    std::uint64_t revision() const { return revision_; }
    bool alerting() const { return alerting_; }
    const AssistantEntry& reminder() const { return secured() ? hidden_reminder_ : reminder_; }
private:
    bool secured() const { return identities_ && identities_->enforced(); }
    Status submit_impl(std::span<const ToolCall> calls, const SpeakerContext* actor);
    Status confirm_impl(std::string_view id,const SpeakerContext* owner);
    Status cancel_impl(std::string_view id);
    Status complete_reminder_impl(std::int64_t id,std::int64_t seconds);
    void tell(std::string_view value) { reply_.assign(value); ++revision_; }
    void execute(ToolJob& job);
    hal::Clock& clock_;
    StateStore& store_;
    VoiceDirectory* identities_{}; // Non-owning; directory outlives this service.
    AssistantEntry hidden_reminder_{};
    PhoneTools* phone_{}; // Non-owning; adapter must outlive this service.
    ToolPolicy policy_{};
    Text<512> reply_{};
    AssistantEntry reminder_{};
    std::int64_t next_scan_ms_{}, next_scan_wall_{}, dispatched_ms_{};
    Text<40> active_id_{};
    std::uint64_t revision_{};
    bool alerting_{},pending_work_{true};
};
}
