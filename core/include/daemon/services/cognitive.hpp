#pragma once
#include "daemon/framework/application.hpp"
#include "daemon/services/store.hpp"
#include <optional>
#include <span>

namespace daemonos {
struct AgentConfiguration {
    Text<1024> instructions{std::string_view{"You are a curious, kind digital familiar embodied in this watch. Respect your keeper and local capabilities."}};
};
struct MemoryCandidate { Text<192> content{}; double importance{0.5},valence{}; };
struct PersonalityProposal { Text<24> trait{}; double delta{}; };
struct IntentionProposal { Text<96> description{}; std::uint32_t after_seconds{}; };
struct VoiceParameters { double rate{1},pitch{1},volume{0.7}; bool present{}; };
struct CognitiveResponse {
    std::uint32_t schema_version{1};
    Text<512> speech{};
    Text<24> emotion{std::string_view{"calm"}};
    Text<24> animation{std::string_view{"idle"}};
    double intensity{};
    std::array<Capability, 4> actions{};
    std::size_t action_count{};
    Text<24> facial_expression{std::string_view{"neutral"}};
    VoiceParameters voice{};
    std::array<MemoryCandidate,2> memory_candidates{};
    std::array<PersonalityProposal,2> personality_adjustments{};
    std::array<IntentionProposal,2> intentions{};
    std::size_t memory_count{},personality_count{},intention_count{};
};
class ResponseParser {
public:
    bool append(std::string_view chunk);
    Status finish(CognitiveResponse& response) const;
    void reset() { size_ = 0; overflow_ = false; }
private:
    std::array<char, budget::response_bytes> buffer_{};
    std::size_t size_{};
    bool overflow_{};
};
class MemoryRetriever {
public:
    explicit MemoryRetriever(StateStore& store) : store_(store) {}
    Status retrieve(std::string_view query, MemoryPage& result) { return store_.memories(query, 0, result); }
private: StateStore& store_;
};
class ContextBuilder {
public:
    Status build(const CreatureState& state, const MemoryPage& memories,const AgentConfiguration& agent={},const Environment& environment={},Event trigger={EventType::think});
    std::string_view context() const { return {buffer_.data(), size_}; }
private:
    std::array<char, budget::context_bytes> buffer_{};
    std::size_t size_{};
};
struct InferenceProvider {
    virtual ~InferenceProvider() = default;
    virtual Status start(std::string_view context, std::uint32_t max_tokens) = 0;
    virtual Status poll(std::span<char> chunk, std::size_t& size, bool& done) = 0;
    virtual void cancel() = 0;
};
class MockProvider final : public InferenceProvider {
public:
    Status start(std::string_view context, std::uint32_t max_tokens) override;
    Status poll(std::span<char> chunk, std::size_t& size, bool& done) override;
    void cancel() override { active_ = false; }
private: std::size_t offset_{}; bool active_{};
};
struct CognitiveBudget { std::int64_t interval_ms{60'000}; std::int64_t timeout_ms{10'000}; std::uint32_t max_tokens{256}; };
class CognitiveScheduler {
public:
    explicit CognitiveScheduler(CognitiveBudget budget = {}) : budget_(budget) {}
    Status begin(std::int64_t now, const Environment& environment, bool enabled);
    bool timed_out(std::int64_t now) const;
    void complete(std::int64_t now, bool success);
    bool active() const { return active_; }
    const CognitiveBudget& limits() const { return budget_; }
private:
    CognitiveBudget budget_{};
    std::int64_t started_{}, next_{};
    unsigned failures_{};
    bool active_{};
};
class ActionValidator {
public: static Status validate(const CognitiveResponse& response, Capabilities allowed);
};
// A validated proposal may affect only these bounded presentation fields.
class CapabilityExecutor {
public:
    Status execute(const CognitiveResponse& response, Capabilities allowed);
    const Text<512>& speech() const { return speech_; }
    const Text<24>& animation() const { return animation_; }
    std::optional<CreatureFace> face_cue() const { return face_cue_; }
private: Text<512> speech_{}; Text<24> animation_{std::string_view{"idle"}};
    std::optional<CreatureFace> face_cue_{};
};
}
