#include "platform.hpp"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "sqlite3.h"
#include "../boards/t-watch-ultra/pins.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <new>

namespace {
using namespace daemonos;
static_assert(board::t_watch_ultra::portrait_width==410 && board::t_watch_ultra::portrait_height==502);
extern const char agent_start[] asm("_binary_agent_md_start");
extern const char agent_end[] asm("_binary_agent_md_end");
alignas(Runtime) EXT_RAM_BSS_ATTR std::byte runtime_storage[sizeof(Runtime)];
StaticQueue_t event_queue_control;
std::uint8_t event_queue_data[budget::events*sizeof(Event)];
void flush(lv_display_t* display,const lv_area_t* area,std::uint8_t* pixels) {
    auto* backend=static_cast<embedded::UnsupportedDisplay*>(lv_display_get_user_data(display));
    (void)backend->flush(area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1,
        {reinterpret_cast<const std::uint32_t*>(pixels),static_cast<std::size_t>((area->x2-area->x1+1)*(area->y2-area->y1+1))});
    lv_display_flush_ready(display);
}
int sqlite_probe(void* heap,std::size_t bytes) {
    if (sqlite3_config(SQLITE_CONFIG_HEAP,heap,static_cast<int>(bytes),32)!=SQLITE_OK) return SQLITE_MISUSE;
    sqlite3* database=nullptr;
    int result=sqlite3_open(":memory:",&database);
    if (result==SQLITE_OK) result=sqlite3_exec(database,"CREATE TABLE readiness(id INTEGER); BEGIN; INSERT INTO readiness VALUES(1); COMMIT;",nullptr,nullptr,nullptr);
    sqlite3_close(database); database=nullptr;
    const int disk=sqlite3_open("/sdcard/unsupported.sqlite",&database); sqlite3_close(database);
    if (disk!=SQLITE_CANTOPEN) return SQLITE_ERROR;
    return result;
}
}
extern "C" void app_main() {
    using namespace daemonos;
    ESP_LOGI("habitat","DaeMON OS v0.2.0: shared C++20 core/presentation; physical peripherals unsupported");
    const auto nvs=nvs_flash_init();
    if (nvs!=ESP_OK) { ESP_LOGE("habitat","NVS init failed (%d); existing partition will not be erased",nvs); return; }
    esp_pm_config_t pm{.max_freq_mhz=240,.min_freq_mhz=80,.light_sleep_enable=true};
    if (esp_pm_configure(&pm)!=ESP_OK) ESP_LOGW("habitat","automatic light sleep unavailable");
    embedded::Allocator allocator; ResourceManager resources({},&allocator);
    auto task_memory=resources.reserve(MemoryCategory::runtime,MemoryPool::internal,32*1024+sizeof(event_queue_data)+sizeof(event_queue_control));
    auto graphics=resources.reserve(MemoryCategory::graphics,MemoryPool::external,512*1024);
    auto draw=resources.allocate(MemoryCategory::graphics,MemoryPool::external,410*40*4);
    auto database_heap=resources.allocate(MemoryCategory::database,MemoryPool::external,192*1024);
    if (!task_memory || !graphics || !draw || !database_heap) { ESP_LOGE("habitat","application memory budgets cannot be allocated"); return; }
    ESP_LOGI("habitat","SQLite %s readiness probe result=%d (persistent VFS unsupported)",sqlite3_libversion(),sqlite_probe(database_heap.data(),database_heap.bytes().size()));
    embedded::Clock clock; embedded::Random random; embedded::NvsCheckpoint checkpoint;
    embedded::CheckpointStateStore store(checkpoint); MockProvider provider;
    auto* runtime=::new(static_cast<void*>(runtime_storage)) Runtime(clock,random,store,checkpoint,provider,&resources);
    if (auto status=runtime->boot(); !status) { ESP_LOGE("habitat","boot refused: %s",status.message.c_str()); runtime->~Runtime(); return; }
    if (auto status=runtime->configure_agent({agent_start,static_cast<std::size_t>(agent_end-agent_start-1)}); !status)
        ESP_LOGE("habitat","agent.md rejected: %s",status.message.c_str());
    std::array<embedded::DeviceGate,6> gates{embedded::DeviceGate{"display"},embedded::DeviceGate{"audio"},embedded::DeviceGate{"BLE"},embedded::DeviceGate{"Wi-Fi"},embedded::DeviceGate{"sensors"},embedded::DeviceGate{"deep sleep"}};
    constexpr std::array ids{ServiceId::display,ServiceId::audio,ServiceId::bluetooth,ServiceId::wifi,ServiceId::sensors};
    for (std::size_t i=0;i<ids.size();++i) runtime->power_manager().bind(ids[i],embedded::DeviceGate::apply,&gates[i]);
    lv_init(); auto* display=lv_display_create(410,502);
    embedded::UnsupportedDisplay backend; lv_display_set_user_data(display,&backend);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display,draw.data(),nullptr,static_cast<std::uint32_t>(draw.bytes().size()),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    presentation::WatchController controller(*runtime,clock);
    presentation::WatchView view(controller,display,presentation::ember_sprite());
    QueueHandle_t queue=xQueueCreateStatic(budget::events,sizeof(Event),event_queue_data,&event_queue_control);
    esp_pm_lock_handle_t cpu_lock=nullptr;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"daemon-work",&cpu_lock));
    auto previous=clock.monotonic_ms(); auto power=runtime->power();
    ESP_LOGI("habitat","free internal=%u PSRAM=%u; display and touch drivers are unsupported",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    for (;;) {
        esp_pm_lock_acquire(cpu_lock);
        const auto now=clock.monotonic_ms(); lv_tick_inc(static_cast<std::uint32_t>(now-previous)); previous=now;
        runtime->tick(); view.update(lv_tick_get()); const auto lv_delay=lv_timer_handler();
        if (power!=runtime->power()) {
            power=runtime->power(); ESP_LOGI("power","state=%s locks=%u",power_name(power).data(),static_cast<unsigned>(runtime->power_manager().active_locks()));
            if (power==PowerState::deep_sleep) (void)embedded::DeviceGate::apply(&gates[5],true);
        }
        const auto wait=std::min({runtime->next_delay_ms(),static_cast<std::int64_t>(view.next_delay(lv_tick_get())),static_cast<std::int64_t>(lv_delay)});
        esp_pm_lock_release(cpu_lock);
        Event event{};
        const auto ticks=wait>=static_cast<std::int64_t>(portMAX_DELAY/2)*portTICK_PERIOD_MS ? portMAX_DELAY : std::max<TickType_t>(1,static_cast<TickType_t>((wait+portTICK_PERIOD_MS-1)/portTICK_PERIOD_MS));
        if (xQueueReceive(queue,&event,ticks)==pdTRUE) runtime->post(event);
    }
}
