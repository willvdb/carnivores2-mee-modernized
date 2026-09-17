// Phase 3c supports single-player only. No sockets, threads, or wire changes.
#include "Hunt.h"
void StartupServerCommsThread() { DoHalt("Multiplayer is not available in the Linux build."); }
void StartupClientCommsThread() { DoHalt("Multiplayer is not available in the Linux build."); }
void ShutDownServer() {}
void ShutDownClient() {}
