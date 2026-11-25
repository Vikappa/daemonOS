#include "daemon/framework/application.hpp"

namespace daemonos {
bool ApplicationRegistry::add(Application& app) {
    if (get(app.id())) return false;
    for (auto& entry : apps_) if (!entry) { entry = &app; return true; }
    return false;
}
Application* ApplicationRegistry::get(AppId id) const {
    for (auto* entry : apps_) if (entry && entry->id() == id) return entry;
    return nullptr;
}
Navigation::~Navigation() {
    transitioning_ = true;
    if (auto* app = foreground(); app && !suspended_) app->pause();
    while (size_) stack_[--size_]->stop();
}
bool Navigation::push(AppId id) {
    if (transitioning_) return false;
    auto* app = registry_.get(id);
    if (!app || size_ == stack_.size()) return false;
    for (std::size_t i = 0; i < size_; ++i) if (stack_[i] == app) return false;
    transitioning_ = true;
    if (auto* current = foreground(); current && !suspended_) current->pause();
    stack_[size_++] = app;
    app->start(); if (!suspended_) app->resume();
    transitioning_ = false;
    return true;
}
bool Navigation::back() {
    if (transitioning_ || size_ <= 1) return false;
    auto* next = stack_[size_ - 2];
    if (!next) return false;
    transitioning_ = true;
    if (!suspended_) stack_[size_ - 1]->pause();
    stack_[--size_]->stop();
    if (!suspended_) next->resume();
    transitioning_ = false;
    return true;
}
void Navigation::home() { while (back()) {} }
Application* Navigation::foreground() const { return size_ ? stack_[size_ - 1] : nullptr; }
void Navigation::suspend(bool suspended) {
    if (suspended_==suspended || transitioning_) return;
    transitioning_=true; suspended_=suspended;
    if (auto* app=foreground()) { if (suspended) app->pause(); else app->resume(); }
    transitioning_=false;
}
}
