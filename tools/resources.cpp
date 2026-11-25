#include "daemon/framework/runtime.hpp"
#include <iostream>
int main() {
    using namespace daemonos;
    std::cout << "Host C++ object sizes in bytes (not ESP32 ABI measurements):\n"
        << "Event " << sizeof(Event) << "\nEventBus " << sizeof(EventBus)
        << "\nCreatureState " << sizeof(CreatureState) << "\nMemory " << sizeof(Memory)
        << "\nMemoryPage " << sizeof(MemoryPage) << "\nRuntime " << sizeof(Runtime)
        << "\nAssistantEntry " << sizeof(AssistantEntry) << "\nAssistantPage " << sizeof(AssistantPage)
        << "\nToolCall " << sizeof(ToolCall) << "\nToolPage " << sizeof(ToolPage)
        << "\nVoiceDirectory " << sizeof(VoiceDirectory) << "\nVoiceProfile " << sizeof(VoiceProfile)
        << "\nVoiceObservation " << sizeof(VoiceObservation) << "\nVoiceTemplate " << sizeof(VoiceTemplate)
        << "\nSpeechMotion " << sizeof(SpeechMotion)
        << "\nContextBuilder " << sizeof(ContextBuilder) << "\nResponseParser " << sizeof(ResponseParser)
        << "\nScheduler " << sizeof(Scheduler) << '\n';
}
