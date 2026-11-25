#pragma once
#include "daemon/types.hpp"
#include <span>
#include <cstddef>

namespace daemonos {
enum class UserRole : std::uint8_t { owner, authorized, guest };
enum class IdentityStrength : std::uint8_t { voice_match, unlocked };
inline constexpr ToolMask safe_voice_tools=tool_bit(ToolName::save_note) | tool_bit(ToolName::set_reminder) |
    tool_bit(ToolName::web_search) | tool_bit(ToolName::schedule_agent_task);
struct VoiceProfile {
    Text<40> id{};
    Text<64> name{}; // Only the owner sets this label; guests begin with an opaque label.
    UserRole role{UserRole::guest};
    bool enabled{true}, name_confirmed{};
    ToolMask grants{}, voice_grants{};
    std::array<Text<32>,4> bindings{};
    std::size_t binding_count{};
    std::uint64_t revision{1};
    std::int64_t serial{}, first_seen{}, last_seen{};
    std::size_t observations{}, visits{}, contexts{};
};
struct VoiceProfilePage { std::array<VoiceProfile,4> items{}; std::size_t size{}; };
using VoiceEmbedding=std::array<float,budget::voice_dimensions>;
struct VoiceObservation {
    Text<40> capture_id{}, context_id{};
    Text<48> encoder_id{}; // Includes model/preprocessing version; never compare different encoders.
    VoiceEmbedding embedding{};
    std::uint32_t voiced_ms{};
    float quality{};
    bool overlapping{};
    Text<64> claimed_name{}; // Unverified utterance content, never identity authority.
};
struct VoiceEncounter {
    Text<40> profile_id{}, capture_id{}, context_id{};
    Text<64> claimed_name{};
    std::int64_t timestamp{};
    float similarity{}; // Similarity score, not an identity probability.
};
struct VoiceEncounterPage { std::array<VoiceEncounter,8> items{}; std::size_t size{}; };
struct SealedVoiceprint {
    Text<32> scheme{};
    std::array<std::byte,1024> bytes{};
    std::size_t size{};
};
struct VoiceTemplate {
    Text<40> profile_id{};
    Text<48> encoder_id{};
    unsigned slot{};
    SealedVoiceprint sealed{};
};
// The adapter implements authenticated encryption/key storage; no plaintext default.
struct VoiceprintProtector {
    virtual ~VoiceprintProtector() = default;
    virtual Status seal(std::string_view profile,std::string_view encoder,const VoiceEmbedding& embedding,SealedVoiceprint& sealed) = 0;
    virtual Status open(std::string_view profile,std::string_view encoder,const SealedVoiceprint& sealed,VoiceEmbedding& embedding) = 0;
};
// Audio-to-embedding is supplied by an APK/device adapter; the core matches vectors.
struct SpeakerEncoder {
    virtual ~SpeakerEncoder() = default;
    virtual Status encode(std::span<const std::int16_t> pcm,std::uint32_t sample_rate,VoiceObservation& output) = 0;
};
struct UnlockEvidence { Text<128> opaque{}; };
// Trusted adapter must validate/consume a fresh local biometric/PIN proof for this profile.
// This is not a model tool or an unauthenticated wire endpoint.
struct LocalIdentityAuthenticator {
    virtual ~LocalIdentityAuthenticator() = default;
    virtual Status verify_setup(const UnlockEvidence& evidence) = 0;
    virtual Status verify(std::string_view profile,const UnlockEvidence& evidence) = 0;
};
class VoiceDirectory;
class UserSession {
public:
    UserSession() = default;
private:
    friend class VoiceDirectory;
    const VoiceDirectory* issuer_{};
    std::uint32_t slot_{}, generation_{};
};
struct SpeakerContext {
    Text<40> id{};
    Text<64> name{};
    UserRole role{UserRole::guest};
    IdentityStrength strength{IdentityStrength::voice_match};
    ToolMask grants{};
    std::uint64_t revision{};
    std::int64_t first_seen{}, last_seen{};
    std::size_t visits{}, contexts{};
    bool name_confirmed{};
};
enum class VoiceMatchState : std::uint8_t { matched, new_guest, unknown, ambiguous, insufficient, blocked };
struct VoiceMatch {
    VoiceMatchState state{VoiceMatchState::unknown};
    SpeakerContext speaker{};
    float similarity{}, runner_up{};
    UserSession session{};
};
struct VoicePolicy {
    bool track_guests{}; // Explicit setup choice; never turns on ambient recording.
    float match_threshold{0.86F}, ambiguity_margin{0.08F}, minimum_quality{0.6F};
    std::uint32_t minimum_voiced_ms{1200};
};
inline std::string_view role_name(UserRole role) {
    switch(role) { case UserRole::owner: return "owner"; case UserRole::authorized: return "authorized"; case UserRole::guest: return "guest"; }
    return "unknown";
}
struct IdentityStore {
    virtual ~IdentityStore() = default;
    virtual Status identity_owner(VoiceProfile& profile) = 0;
    virtual Status identity_profile(std::string_view id,VoiceProfile& profile) = 0;
    virtual Status identity_profiles(std::size_t offset,VoiceProfilePage& page) = 0;
    virtual Status identity_bootstrap(const VoiceProfile& profile) = 0;
    virtual Status identity_update(const VoiceProfile& profile,std::uint64_t expected_revision) = 0;
    virtual Status identity_enroll(const VoiceTemplate& voice) = 0;
    virtual Status identity_template(std::string_view id,unsigned slot,VoiceTemplate& voice) = 0;
    virtual Status identity_observe(const VoiceEncounter& encounter,const VoiceProfile* guest,const VoiceTemplate* voice) = 0;
    virtual Status identity_forget(std::string_view id) = 0;
    virtual Status identity_encounters(std::string_view id,std::size_t offset,VoiceEncounterPage& page) = 0;
    virtual Status identity_capture_seen(std::string_view capture,bool& seen) = 0;
    virtual Status identity_set_guest_tracking(bool enabled) = 0;
    virtual Status identity_guest_tracking(bool& enabled) = 0;
};
}
