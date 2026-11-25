#pragma once
#include "daemon/hal/interfaces.hpp"
#include "daemon/services/voice_types.hpp"

namespace daemonos {
Status validate_voice_profile(const VoiceProfile& profile);
class VoiceDirectory {
public:
    VoiceDirectory(hal::Clock& clock,hal::Random& random,IdentityStore& store) : clock_(clock),random_(random),store_(store) {}
    void adapters(LocalIdentityAuthenticator* auth,VoiceprintProtector* protector) { auth_=auth; protector_=protector; }
    Status load();
    bool enforced() const { return initialized_ || !healthy_; }
    Status bootstrap(std::string_view id,std::string_view name,const UnlockEvidence& evidence,UserSession& session);
    Status unlock(std::string_view id,const UnlockEvidence& evidence,UserSession& session);
    Status configure(const UserSession& owner,const VoicePolicy& policy);
    Status save_profile(const UserSession& owner,VoiceProfile profile);
    Status enroll(const UserSession& owner,std::string_view id,unsigned slot,const VoiceObservation& observation);
    Status forget_voice(const UserSession& owner,std::string_view id);
    void lock(UserSession& session);
    Status observe(const VoiceObservation& observation,VoiceMatch& match);
    Status context(const UserSession& session,SpeakerContext& context) const;
    Status authorize(const UserSession& session,const ToolCall& call) const;
    Status owner(const UserSession& session,SpeakerContext& context) const;
    Status delegated(std::string_view actor,ToolMask snapshot,const ToolCall* call = nullptr,ToolMask* current = nullptr) const;
    Status approval(std::string_view owner,std::uint64_t revision) const;
    Status profiles(const UserSession& owner,std::size_t offset,VoiceProfilePage& page);
    Status encounters(const UserSession& owner,std::string_view id,std::size_t offset,VoiceEncounterPage& page);
private:
    struct SessionRecord { Text<40> id{}; std::uint64_t revision{}; IdentityStrength strength{}; std::int64_t expires_ms{}; std::uint32_t generation{}; };
    Status issue(const VoiceProfile& profile,IdentityStrength strength,UserSession& session);
    Status normalized(const VoiceObservation& observation,VoiceEmbedding& embedding) const;
    static bool binding_allowed(const VoiceProfile& profile,const ToolCall& call);
    hal::Clock& clock_;
    hal::Random& random_;
    IdentityStore& store_;
    LocalIdentityAuthenticator* auth_{};
    VoiceprintProtector* protector_{};
    VoicePolicy policy_{};
    std::array<SessionRecord,budget::voice_sessions> sessions_{};
    std::uint32_t generation_{};
    bool initialized_{}, healthy_{true};
};
}
