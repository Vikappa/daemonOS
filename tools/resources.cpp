#include "daemon/framework/runtime.hpp"
#include <iostream>
int main() {
    using namespace daemonos;
    std::cout << "Host C++ object sizes in bytes (not ESP32 ABI measurements):\n"
        << "Event " << sizeof(Event) << "\nEventBus " << sizeof(EventBus)
        << "\nCreatureState " << sizeof(CreatureState) << "\nMemory " << sizeof(Memory)
        << "\nMemoryPage " << sizeof(MemoryPage) << "\nRuntime " << sizeof(Runtime)
        << "\nContextBuilder " << sizeof(ContextBuilder) << "\nResponseParser " << sizeof(ResponseParser)
        << "\nScheduler " << sizeof(Scheduler) << '\n';
}
