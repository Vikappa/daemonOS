#pragma once
#include <array>

namespace daemonos {
// Non-owning registry. Services and this registry have the same runtime lifetime.
class ServiceRegistry {
public:
    template<class T> bool add(T& service) {
        if (get<T>()) return false;
        for (auto& slot : slots_) if (!slot.value) { slot = {token<T>(), &service}; return true; }
        return false;
    }
    template<class T> T* get() const {
        for (const auto& slot : slots_) if (slot.key == token<T>()) return static_cast<T*>(slot.value);
        return nullptr;
    }
private:
    template<class T> static const void* token() { static const char tag{}; return &tag; }
    struct Entry { const void* key{}; void* value{}; };
    std::array<Entry, 16> slots_{};
};
}
