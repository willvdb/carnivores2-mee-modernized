// Compile the production config implementation, including its private entry points.
// Linker section GC discards gameplay/graphics not used by this asset-free harness.
#include "../Hunt/Game/EngineInit.cpp"
void SessionTestCreateConfig() { CreateDefaultConfig(); }
void SessionTestLoadConfig() { LoadConfig(); }
