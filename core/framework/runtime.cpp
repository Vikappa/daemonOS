#include "daemon/framework/runtime.hpp"

namespace daemonos {
Runtime::Runtime(hal::Clock& clock, hal::Random& random, StateStore& store, CheckpointStore& checkpoint, InferenceProvider& provider)
    : clock_(clock), random_(random), store_(store), checkpoint_(checkpoint), provider_(provider) {
    services_.add<hal::Clock>(clock_); services_.add<StateStore>(store_);
    services_.add<EventBus>(events_); services_.add<PowerManager>(power_);
    applications_.add(launcher_); applications_.add(clock_app_);
    applications_.add(settings_app_); applications_.add(creature_app_);
    subscription_ = events_.subscribe(on_event, this);
}
Runtime::~Runtime() { if (booted_) { provider_.cancel(); } }
Status Runtime::boot() {
    if (booted_) return Status::fail(Error::busy, "runtime already booted");
    auto status = store_.load(creature_, settings_);
    if (!status) {
        // Never overwrite a schema from a newer release or a locked database.
        if (status.code == Error::incompatible || status.code == Error::busy) return status;
        auto checkpoint_status = checkpoint_.load(creature_);
        if (!checkpoint_status) {
            if (status.code != Error::unavailable || checkpoint_status.code != Error::unavailable)
                return Status::fail(Error::corrupt, "state inaccessible and no valid identity checkpoint; refusing to replace creature");
            creature_ = CreatureEngine::create(clock_, random_);
        }
        storage_status_.assign(checkpoint_status ? "Recovered minimal checkpoint" : "New local identity");
    } else storage_status_.assign("SQLite / local authority");
    CreatureEngine::advance(creature_, CreatureEngine::offline_elapsed(clock_.wall_seconds(), creature_.last_wall) * 1000);
    creature_.last_wall = std::max<std::int64_t>(0, clock_.wall_seconds());
    last_mono_ = last_input_ = last_save_ = last_checkpoint_ = clock_.monotonic_ms();
    navigation_.push(AppId::launcher); booted_ = true;
    // Initial identity must survive a first-run storage fault.
    const auto checkpoint_status = checkpoint_.save(creature_);
    const auto save_status = flush();
    if (!checkpoint_status && !save_status) return Status::fail(Error::io, "cannot persist initial identity");
    return {};
}
void Runtime::tick() {
    if (!booted_) return;
    const auto now = clock_.monotonic_ms();
    const auto elapsed = std::max<std::int64_t>(0, now - last_mono_);
    last_mono_ = now;
    CreatureEngine::advance(creature_, elapsed);
    creature_.last_wall = std::max<std::int64_t>(0, clock_.wall_seconds());
    events_.dispatch();
    poll_cognition();
    power_.update(environment_, std::max<std::int64_t>(0, now - last_input_), cognition_.active());
    if (now - last_save_ >= budget::save_interval_ms) { (void)flush(); last_save_ = now; }
    if (now - last_checkpoint_ >= budget::checkpoint_interval_ms) {
        if (checkpoint_.save(creature_)) last_checkpoint_ = now;
    }
}
void Runtime::handle(const Event& event) {
    const auto now = clock_.monotonic_ms();
    if (event.type == EventType::feed || event.type == EventType::rest || event.type == EventType::pet ||
        event.type == EventType::open_app || event.type == EventType::home || event.type == EventType::back ||
        event.type == EventType::think || event.type == EventType::wake || event.type == EventType::brightness)
        last_input_ = now;
    if (CreatureEngine::interact(creature_, event.type)) {
        switch (event.type) {
        case EventType::feed: message_.assign("Warm sparks. My favorite snack."); remember("My keeper fed me warm sparks."); break;
        case EventType::rest: message_.assign(creature_.sleeping ? "Recharging my inner fire..." : "Awake. What shall we discover?"); remember(creature_.sleeping ? "I settled down to rest." : "I woke up with my keeper."); break;
        case EventType::pet: message_.assign("A small kindness. I will remember."); remember("My keeper showed me affection."); break;
        case EventType::move:
            environment_.steps += static_cast<std::uint32_t>(std::clamp(event.value, 0, 10000));
            message_.assign("Movement! There is a world out there."); remember("We moved and explored together."); break;
        default: break;
        }
        // Debounced on the persistence cadence, keeping touch bursts off storage.
    }
    switch (event.type) {
    case EventType::battery: environment_.battery = std::clamp(event.value, 0, 100); break;
    case EventType::charging: environment_.charging = event.value != 0; break;
    case EventType::connectivity: environment_.connected = event.value != 0; break;
    case EventType::storage:
        environment_.storage = event.value != 0; store_.available(environment_.storage);
        if (environment_.storage) (void)flush();
        else storage_status_.assign("Storage fault / RAM + checkpoint");
        break;
    case EventType::open_app:
        if (event.value >= 0 && event.value <= 3) navigation_.push(static_cast<AppId>(event.value));
        break;
    case EventType::back: navigation_.back(); break;
    case EventType::home: navigation_.home(); break;
    case EventType::think: think(); break;
    case EventType::toggle_cognition: settings_.cognition = event.value != 0; break;
    case EventType::brightness: settings_.brightness = std::clamp(event.value, 10, 100); break;
    case EventType::suspend: last_input_ = now - 60'000; break;
    default: break;
    }
    if (auto* foreground = navigation_.foreground()) foreground->event(event);
}
void Runtime::remember(std::string_view content, std::string_view provenance) {
    if (pending_count_ == pending_.size()) { ++lost_memories_; return; }
    auto& memory = pending_[pending_count_++];
    memory = {}; memory.timestamp = creature_.last_wall;
    memory.content.assign(content); memory.provenance.assign(provenance);
    memory.tags.assign("keeper,interaction"); memory.entities.assign("keeper,Ember");
}
Status Runtime::flush(bool force_checkpoint) {
    if (!booted_) return Status::fail(Error::unavailable, "runtime not booted");
    Status result{};
    // State and each associated experience commit together. Failed memories stay queued.
    if (pending_count_ == 0) result = store_.save(creature_, settings_);
    while (pending_count_ && result) {
        result = store_.save(creature_, settings_, &pending_[0]);
        if (result) {
            for (std::size_t i = 1; i < pending_count_; ++i) pending_[i - 1] = pending_[i];
            --pending_count_;
        }
    }
    storage_status_.assign(result ? "SQLite / local authority" : "Storage unavailable / RAM retained");
    if (force_checkpoint) {
        const auto status = checkpoint_.save(creature_);
        if (!status) return status;
        last_checkpoint_ = clock_.monotonic_ms();
    }
    last_save_ = clock_.monotonic_ms();
    return result;
}
void Runtime::think() {
    auto status = cognition_.begin(clock_.monotonic_ms(), environment_, settings_.cognition);
    if (!status) { message_.assign(status.message.view()); return; }
    MemoryPage page;
    MemoryRetriever retriever(store_);
    (void)retriever.retrieve({}, page);
    status = context_.build(creature_, page);
    if (status) status = provider_.start(context_.context(), cognition_.limits().max_tokens);
    if (!status) { cognition_.complete(clock_.monotonic_ms(), false); message_.assign(status.message.view()); return; }
    parser_.reset(); message_.assign("Thinking / mock coprocessor...");
}
void Runtime::poll_cognition() {
    if (!cognition_.active()) return;
    const auto now = clock_.monotonic_ms();
    if (!environment_.connected || !settings_.cognition || (environment_.battery < 15 && !environment_.charging) || cognition_.timed_out(now)) {
        provider_.cancel(); cognition_.complete(now, false);
        message_.assign("Cognition cancelled. Instincts remain awake."); return;
    }
    std::array<char, 128> chunk{}; std::size_t size = 0; bool done = false;
    auto status = provider_.poll(chunk, size, done);
    if (size > chunk.size()) status = Status::fail(Error::invalid, "provider returned invalid chunk size");
    if (status && !parser_.append({chunk.data(), size})) status = Status::fail(Error::full, "response budget exceeded");
    if (!status) { provider_.cancel(); cognition_.complete(now, false); message_.assign(status.message.view()); return; }
    if (done) {
        CognitiveResponse response;
        status = parser_.finish(response);
        if (status) status = executor_.execute(response, creature_app_.capabilities());
        if (status) {
            message_ = executor_.speech();
            for (std::size_t i = 0; i < response.action_count; ++i)
                if (response.actions[i] == Capability::remember) remember(response.speech.view().substr(0, 383), "model-proposal");
        } else message_.assign(status.message.view());
        cognition_.complete(now, static_cast<bool>(status));
    }
}
}
