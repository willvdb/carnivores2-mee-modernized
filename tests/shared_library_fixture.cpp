// An intentionally incomplete OpenAL library, plus an ordinary callable export.
#ifdef _WIN32
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" TEST_EXPORT int TestLibraryValue() { return 42; }
extern "C" TEST_EXPORT void* alcOpenDevice(const char*) { return nullptr; }
