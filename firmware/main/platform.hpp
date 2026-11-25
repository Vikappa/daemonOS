#pragma once
#include "daemon/framework/runtime.hpp"
#include "daemon/presentation/watch_view.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "nvs.h"
#include <ctime>
#include <cstring>

namespace daemonos::embedded {
class Clock final : public hal::Clock {
public:
    std::int64_t monotonic_ms() const override { return esp_timer_get_time()/1000; }
    std::int64_t wall_seconds() const override { return std::max<std::int64_t>(0,static_cast<std::int64_t>(std::time(nullptr))); }
};
class Random final : public hal::Random { public: std::uint32_t next() override { return esp_random(); } };
class Allocator final : public MemoryAllocator {
public:
    void* allocate(std::size_t bytes,MemoryPool pool) override {
        return heap_caps_aligned_alloc(32,bytes,MALLOC_CAP_8BIT|(pool==MemoryPool::external ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL));
    }
    void release(void* memory) override { heap_caps_free(memory); }
};
// NVS A/B minimal records have an explicit ABI/version/size and checksum gate.
// No power-loss or wear claim is made until physical testing.
class NvsCheckpoint final : public CheckpointStore {
public:
    NvsCheckpoint() { ready_=nvs_open("daemon",NVS_READWRITE,&handle_)==ESP_OK; }
    ~NvsCheckpoint() override { if (ready_) nvs_close(handle_); }
    Status load(CreatureState& state) override {
        if (!ready_) return Status::fail(Error::io,"NVS unavailable");
        Record a{},b{}; bool found=false; const bool av=read("identity_a",a,found),bv=read("identity_b",b,found);
        if (!av && !bv) return Status::fail(found ? Error::corrupt : Error::unavailable,"no valid NVS identity record");
        const auto& chosen=av && (!bv || a.sequence>=b.sequence) ? a : b;
        sequence_=chosen.sequence; state=chosen.state; settings_=chosen.settings; return {};
    }
    Status save(const CreatureState& state) override {
        if (!ready_ || !valid(state)) return Status::fail(Error::io,"cannot save NVS identity");
        Record record{}; record.version=2; record.size=sizeof(CreatureState); record.sequence=++sequence_; record.state=state; record.settings=settings_;
        record.checksum=checksum(record);
        const auto result=nvs_set_blob(handle_,sequence_%2 ? "identity_a" : "identity_b",&record,sizeof(record));
        return result==ESP_OK && nvs_commit(handle_)==ESP_OK ? Status{} : Status::fail(Error::io,"NVS commit failed");
    }
    const Settings& settings() const { return settings_; }
    void settings(const Settings& settings) { settings_=settings; }
private:
    struct Record { std::uint32_t version{},size{},sequence{},checksum{}; CreatureState state{}; Settings settings{}; };
    static std::uint32_t checksum(const Record& record) {
        auto crc=esp_rom_crc32_le(0,reinterpret_cast<const std::uint8_t*>(&record.version),3*sizeof(std::uint32_t));
        crc=esp_rom_crc32_le(crc,reinterpret_cast<const std::uint8_t*>(&record.state),sizeof(record.state));
        return esp_rom_crc32_le(crc,reinterpret_cast<const std::uint8_t*>(&record.settings),sizeof(record.settings));
    }
    bool read(const char* key,Record& record,bool& found) {
        std::size_t bytes=sizeof(record); const auto result=nvs_get_blob(handle_,key,&record,&bytes);
        if (result!=ESP_ERR_NVS_NOT_FOUND) found=true;
        return result==ESP_OK && bytes==sizeof(record) && record.version==2 && record.size==sizeof(CreatureState) && valid(record.state) && record.settings.brightness>=10 && record.settings.brightness<=100 && record.checksum==checksum(record);
    }
    nvs_handle_t handle_{}; std::uint32_t sequence_{}; bool ready_{};
    Settings settings_{};
};
// Essential local state uses NVS until a real SD VFS is qualified. History,
// queries and exports deliberately return unsupported rather than inventing data.
class CheckpointStateStore final : public StateStore {
public:
    std::string_view backend_name() const override { return "NVS / minimal local state"; }
    explicit CheckpointStateStore(NvsCheckpoint& checkpoint) : checkpoint_(checkpoint) {}
    Status load(CreatureState& state,Settings& settings) override { auto status=checkpoint_.load(state); if (status) settings=checkpoint_.settings(); return status; }
    Status save(const CreatureState& state,const Settings& settings,const Memory* memory=nullptr) override {
        if (memory) return Status::fail(Error::unsupported,"SD memory persistence is not implemented");
        checkpoint_.settings(settings);
        return checkpoint_.save(state);
    }
    Status memories(std::string_view,std::size_t,MemoryPage& page) override { page.size=0; return Status::fail(Error::unsupported,"SD memory retrieval is not implemented"); }
    Status backup(std::string_view) override { return Status::fail(Error::unsupported,"SD export is not implemented"); }
    void available(bool) override {}
private: NvsCheckpoint& checkpoint_;
};
class UnsupportedDisplay final : public hal::Display {
public:
    Status flush(int,int,int,int,std::span<const std::uint32_t>) override { return Status::fail(Error::unsupported,"CO5300 driver not implemented"); }
    void brightness(int) override {}
};
class DeviceGate {
public:
    explicit DeviceGate(const char* name) : name_(name) {}
    static Status apply(void* pointer,bool enabled) {
        auto& self=*static_cast<DeviceGate*>(pointer);
        ESP_LOGI("peripheral","%s requested=%d, driver=unsupported",self.name_,enabled);
        return Status::fail(Error::unsupported,"physical peripheral driver not implemented");
    }
private: const char* name_;
};
}
