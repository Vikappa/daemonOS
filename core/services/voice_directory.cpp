#include "daemon/services/voice_directory.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

namespace daemonos {
namespace {
Status denied() { return Status::fail(Error::denied,"identity authorization required"); }
void describe(const VoiceProfile& p,IdentityStrength strength,SpeakerContext& c) {
    c={}; c.id=p.id; c.name=p.name; c.role=p.role; c.strength=strength;
    c.grants=strength==IdentityStrength::unlocked ? p.grants : p.voice_grants;
    c.first_seen=p.first_seen; c.last_seen=p.last_seen; c.visits=p.visits; c.contexts=p.contexts; c.name_confirmed=p.name_confirmed; c.revision=p.revision;
}
bool normalize(VoiceEmbedding& e) {
    double norm=0; for(const float v:e) { if(!std::isfinite(v)) return false; norm+=static_cast<double>(v)*v; }
    if(!std::isfinite(norm) || norm<1e-12) return false;
    norm=std::sqrt(norm); for(auto& v:e) v=static_cast<float>(v/norm); return true;
}
}
Status validate_voice_profile(const VoiceProfile& p) {
    if(!valid_identifier(p.id.view()) || p.name.view().empty() || static_cast<unsigned>(p.role)>2 ||
        (p.grants & ~all_tools) || (p.voice_grants & ~(p.grants & safe_voice_tools)) ||
        (p.role==UserRole::guest && (p.grants || p.voice_grants)) ||
        p.revision==0 || p.revision>1'000'000'000 || p.binding_count>p.bindings.size())
        return Status::fail(Error::invalid,"invalid voice profile or grants");
    for(std::size_t i=0;i<p.binding_count;++i) {
        if(!valid_identifier(p.bindings[i].view())) return Status::fail(Error::invalid,"invalid identity binding");
        for(std::size_t j=0;j<i;++j) if(p.bindings[j]==p.bindings[i]) return Status::fail(Error::invalid,"duplicate identity binding");
    }
    return {};
}
Status VoiceDirectory::load() {
    sessions_={}; VoiceProfile p;
    auto status=store_.identity_owner(p);
    if(!status && status.code==Error::unavailable) { initialized_=false; healthy_=true; return {}; }
    healthy_=static_cast<bool>(status); initialized_=healthy_;
    if(status) { status=store_.identity_guest_tracking(policy_.track_guests); healthy_=static_cast<bool>(status); }
    return status;
}
Status VoiceDirectory::issue(const VoiceProfile& p,IdentityStrength strength,UserSession& session) {
    if(generation_==std::numeric_limits<std::uint32_t>::max()) return Status::fail(Error::full,"session generation exhausted");
    const auto now=clock_.monotonic_ms(); std::size_t chosen=sessions_.size();
    for(std::size_t i=0;i<sessions_.size();++i)
        if(sessions_[i].id==p.id && sessions_[i].strength==strength) { chosen=i; break; }
    if(chosen==sessions_.size()) for(std::size_t i=0;i<sessions_.size();++i)
        if(sessions_[i].expires_ms<=now) { chosen=i; break; }
    // A flood of guest captures must not prevent a real owner from unlocking.
    if(chosen==sessions_.size()) for(std::size_t i=0;i<sessions_.size();++i)
        if(sessions_[i].strength==IdentityStrength::voice_match &&
            (chosen==sessions_.size() || sessions_[i].expires_ms<sessions_[chosen].expires_ms)) chosen=i;
    if(chosen==sessions_.size()) return Status::fail(Error::full,"identity session slots full");
    sessions_[chosen]={p.id,p.revision,strength,now+300'000,++generation_};
    session.issuer_=this; session.slot_=static_cast<std::uint32_t>(chosen); session.generation_=generation_; return {};
}
void VoiceDirectory::lock(UserSession& session) {
    if(session.issuer_==this && session.slot_<sessions_.size() && sessions_[session.slot_].generation==session.generation_)
        sessions_[session.slot_].expires_ms=0;
    session={};
}
Status VoiceDirectory::forget_voice(const UserSession& session,std::string_view id) {
    SpeakerContext c; if(auto s=owner(session,c); !s) return s;
    return store_.identity_forget(id);
}

Status VoiceDirectory::bootstrap(std::string_view id,std::string_view name,const UnlockEvidence& evidence,UserSession& session) {
    if(enforced() || !auth_) return denied();
    VoiceProfile p; if(!p.id.assign(id) || !p.name.assign(name)) return Status::fail(Error::invalid,"owner label budget");
    p.role=UserRole::owner; p.name_confirmed=true; p.grants=all_tools; p.voice_grants=safe_voice_tools;
    if(auto s=validate_voice_profile(p); !s) return s;
    if(auto s=auth_->verify_setup(evidence); !s) return s;
    if(auto s=store_.identity_bootstrap(p); !s) return s;
    initialized_=true; return issue(p,IdentityStrength::unlocked,session);
}
Status VoiceDirectory::unlock(std::string_view id,const UnlockEvidence& evidence,UserSession& session) {
    if(!healthy_ || !initialized_ || !auth_) return denied();
    VoiceProfile p; if(auto s=store_.identity_profile(id,p); !s) return s;
    if(!p.enabled || p.role==UserRole::guest) return denied();
    if(auto s=auth_->verify(id,evidence); !s) return s;
    return issue(p,IdentityStrength::unlocked,session);
}
Status VoiceDirectory::context(const UserSession& session,SpeakerContext& c) const {
    c={};
    if(!healthy_ || !initialized_ || session.issuer_!=this || session.slot_>=sessions_.size() || !session.generation_) return denied();
    const auto& r=sessions_[session.slot_];
    if(r.generation!=session.generation_ || r.expires_ms<=clock_.monotonic_ms()) return denied();
    VoiceProfile p; if(auto s=store_.identity_profile(r.id.view(),p); !s) return s;
    if(!p.enabled || p.revision!=r.revision) return denied();
    describe(p,r.strength,c); return {};
}
Status VoiceDirectory::owner(const UserSession& session,SpeakerContext& c) const {
    if(auto s=context(session,c); !s) return s;
    return c.role==UserRole::owner && c.strength==IdentityStrength::unlocked ? Status{} : denied();
}
Status VoiceDirectory::configure(const UserSession& session,const VoicePolicy& p) {
    SpeakerContext c; if(auto s=owner(session,c); !s) return s;
    if(!std::isfinite(p.match_threshold) || !std::isfinite(p.ambiguity_margin) || !std::isfinite(p.minimum_quality) ||
        p.match_threshold<0.5F || p.match_threshold>1 || p.ambiguity_margin<=0 || p.ambiguity_margin>0.4F ||
        p.minimum_quality<0 || p.minimum_quality>1 || p.minimum_voiced_ms<500 || p.minimum_voiced_ms>30'000)
        return Status::fail(Error::invalid,"invalid voice matching policy");
    if(auto s=store_.identity_set_guest_tracking(p.track_guests); !s) return s;
    policy_=p; return {};
}
Status VoiceDirectory::save_profile(const UserSession& session,VoiceProfile p) {
    SpeakerContext c; if(auto s=owner(session,c); !s) return s;
    VoiceProfile old; const auto found=store_.identity_profile(p.id.view(),old);
    if(!found && found.code!=Error::unavailable) return found;
    if((p.role==UserRole::owner && (!found || old.role!=UserRole::owner)) ||
        (found && old.role==UserRole::owner && (p.role!=UserRole::owner || !p.enabled))) return denied();
    if(found && p.revision!=old.revision) return Status::fail(Error::busy,"profile changed; reload before editing");
    const auto expected=found ? old.revision : 0;
    p.revision=expected+1;
    if(auto s=validate_voice_profile(p); !s) return s;
    return store_.identity_update(p,expected);
}
Status VoiceDirectory::normalized(const VoiceObservation& o,VoiceEmbedding& e) const {
    if(!valid_identifier(o.capture_id.view()) || !valid_identifier(o.context_id.view()) || !valid_identifier(o.encoder_id.view()) ||
        !std::isfinite(o.quality) || o.quality<policy_.minimum_quality || o.quality>1 || o.overlapping || o.voiced_ms<policy_.minimum_voiced_ms)
        return Status::fail(Error::invalid,"insufficient or overlapping voice sample");
    e=o.embedding; return normalize(e) ? Status{} : Status::fail(Error::invalid,"invalid voice vector");
}
Status VoiceDirectory::enroll(const UserSession& session,std::string_view id,unsigned slot,const VoiceObservation& o) {
    SpeakerContext c; if(auto s=owner(session,c); !s) return s;
    if(!protector_ || slot>=budget::voice_templates) return Status::fail(Error::invalid,"voice protector or template slot unavailable");
    VoiceProfile p; if(auto s=store_.identity_profile(id,p); !s) return s;
    if(!p.enabled) return denied();
    VoiceEmbedding e; if(auto s=normalized(o,e); !s) return s;
    VoiceTemplate t; t.profile_id=p.id; t.encoder_id=o.encoder_id; t.slot=slot;
    if(auto s=protector_->seal(id,o.encoder_id.view(),e,t.sealed); !s) return s;
    return store_.identity_enroll(t);
}
bool VoiceDirectory::binding_allowed(const VoiceProfile& p,const ToolCall& call) {
    if(p.role==UserRole::owner || !phone_tool(call.name)) return true;
    if(call.binding.view().empty()) return false; // No default account inherited from the owner.
    for(std::size_t i=0;i<p.binding_count;++i) if(p.bindings[i]==call.binding) return true;
    return false;
}
Status VoiceDirectory::authorize(const UserSession& session,const ToolCall& call) const {
    if(tool_name(call.name).empty()) return Status::fail(Error::invalid,"unknown identity operation");
    SpeakerContext c; if(auto s=context(session,c); !s) return s;
    if(!(c.grants & tool_bit(call.name))) return denied();
    VoiceProfile p; if(auto s=store_.identity_profile(c.id.view(),p); !s) return s;
    return binding_allowed(p,call) ? Status{} : denied();
}
Status VoiceDirectory::delegated(std::string_view actor,ToolMask snapshot,const ToolCall* call,ToolMask* current) const {
    if(!healthy_ || !initialized_ || actor.empty() || (call && tool_name(call->name).empty())) return denied();
    VoiceProfile p; if(auto s=store_.identity_profile(actor,p); !s) return s;
    const auto grants=p.grants & snapshot;
    if(current) *current=grants;
    if(!p.enabled || p.role==UserRole::guest || (snapshot & ~grants)) return denied();
    return !call || ((grants & tool_bit(call->name)) && binding_allowed(p,*call)) ? Status{} : denied();
}
Status VoiceDirectory::approval(std::string_view id,std::uint64_t revision) const {
    VoiceProfile p; if(auto s=store_.identity_profile(id,p); !s) return s;
    return healthy_ && initialized_ && p.enabled && p.role==UserRole::owner && p.revision==revision ? Status{} : denied();
}
Status VoiceDirectory::profiles(const UserSession& session,std::size_t offset,VoiceProfilePage& page) {
    page={}; SpeakerContext c; if(auto s=owner(session,c); !s) return s; return store_.identity_profiles(offset,page);
}
Status VoiceDirectory::encounters(const UserSession& session,std::string_view id,std::size_t offset,VoiceEncounterPage& page) {
    page={}; SpeakerContext c; if(auto s=owner(session,c); !s) return s; return store_.identity_encounters(id,offset,page);
}
Status VoiceDirectory::observe(const VoiceObservation& o,VoiceMatch& match) {
    match={}; if(!healthy_ || !initialized_ || !protector_) return denied();
    VoiceEmbedding sample; if(auto s=normalized(o,sample); !s) { match.state=VoiceMatchState::insufficient; return s; }
    bool seen=false; if(auto s=store_.identity_capture_seen(o.capture_id.view(),seen); !s) return s;
    if(seen) return Status::fail(Error::denied,"capture already consumed");
    float best=-1,runner=-1; VoiceProfile selected;
    for(std::size_t offset=0;offset<budget::voice_profiles;offset+=4) {
        VoiceProfilePage page; if(auto s=store_.identity_profiles(offset,page); !s) return s;
        for(std::size_t i=0;i<page.size;++i) {
            float score=-1;
            for(unsigned slot=0;slot<budget::voice_templates;++slot) {
                VoiceTemplate t; const auto loaded=store_.identity_template(page.items[i].id.view(),slot,t);
                if(!loaded) { if(loaded.code==Error::unavailable) continue; return loaded; }
                if(t.encoder_id!=o.encoder_id) continue;
                VoiceEmbedding reference; if(auto s=protector_->open(t.profile_id.view(),t.encoder_id.view(),t.sealed,reference); !s) return s;
                if(!normalize(reference)) return Status::fail(Error::corrupt,"invalid stored voice template");
                double dot=0; for(std::size_t d=0;d<sample.size();++d) dot+=static_cast<double>(sample[d])*reference[d];
                score=std::max(score,static_cast<float>(std::clamp(dot,-1.0,1.0)));
            }
            if(score>best) { runner=best; best=score; selected=page.items[i]; }
            else runner=std::max(runner,score);
        }
        if(page.size<page.items.size()) break;
    }
    match.similarity=best; match.runner_up=runner;
    if(best>=policy_.match_threshold && best-runner<policy_.ambiguity_margin) { match.state=VoiceMatchState::ambiguous; return {}; }
    VoiceTemplate t; bool create=false;
    if(best<policy_.match_threshold) {
        if(!policy_.track_guests || best>=policy_.match_threshold-policy_.ambiguity_margin) return {};
        selected={}; std::array<char,40> id{};
        const auto first=random_.next(),second=random_.next();
        std::snprintf(id.data(),id.size(),"guest-%08x-%08x",static_cast<unsigned>(first),static_cast<unsigned>(second));
        selected.id.assign(id.data()); selected.name.assign(id.data());
        t.profile_id=selected.id; t.encoder_id=o.encoder_id;
        if(auto s=protector_->seal(selected.id.view(),o.encoder_id.view(),sample,t.sealed); !s) return s;
        create=true;
    }
    if(!selected.enabled) { match.state=VoiceMatchState::blocked; return {}; }
    VoiceEncounter encounter; encounter.profile_id=selected.id; encounter.capture_id=o.capture_id; encounter.context_id=o.context_id;
    encounter.claimed_name=o.claimed_name; encounter.timestamp=clock_.wall_seconds(); encounter.similarity=create ? 0 : best;
    if(auto s=store_.identity_observe(encounter,create ? &selected : nullptr,create ? &t : nullptr); !s) return s;
    if(auto s=store_.identity_profile(selected.id.view(),selected); !s) return s;
    if(auto s=issue(selected,IdentityStrength::voice_match,match.session); !s) return s;
    describe(selected,IdentityStrength::voice_match,match.speaker);
    match.state=create ? VoiceMatchState::new_guest : VoiceMatchState::matched; return {};
}
}
