// ==========================================================================
// SoundLoader.cpp
// ==========================================================================

#include "Hunt.h"
#include "LoadValidate.h"
#include "AudioIO.h"

void LoadWav(char* FName, TSFX &sfx)
{

  Platform::FileHandle hfile = Platform::OpenFile(FName, Platform::FileMode::Read);
  if( hfile==Platform::InvalidFile )
  {
    char sz[512];
    snprintf(sz, sizeof(sz), "Error opening file\n%s.", FName );
    DoHalt(sz);
  }

  // Phase 5B.1: sfx.lpData is now std::vector<short int>, so the previous
  // manual _HeapFree is replaced by the vector's own destructor (handled
  // implicitly when the vector is reassigned/resized below). The nullptr
  // reset is also unnecessary.
  // Bound the chunk search by the real file size. A truncated file with no
  // 'data' chunk previously spun here forever: at EOF ReadFile fails with
  // l=0 but the loop never checked, re-reading nothing endlessly.
  const std::int64_t fileSize = Platform::FileSize(hfile);
  if (fileSize == -1)
    DoHalt("Sound loading error: cannot stat WAV file.");
  std::int64_t pos = Platform::SeekFile(hfile, 36, Platform::SeekOrigin::Begin);
  if (pos == -1)
    DoHalt("Sound loading error: truncated WAV header.");

  char c[5];
  c[4] = 0;

  for ( ; ; )
  {
    if (pos >= fileSize)
      DoHalt("Sound loading error: WAV has no data chunk (truncated file).");
    if (!ReadExact(hfile, c, 1))
      DoHalt("Sound loading error: truncated WAV chunk scan.");
    pos += 1;
    if( c[0] == 'd' )
    {
      if (!ReadExact(hfile, &c[1], 3))
        DoHalt("Sound loading error: truncated WAV chunk header.");
      pos += 3;
      if( !lstrcmp( c, "data" ) ) break;
      else {
        Platform::SeekFile(hfile, -3, Platform::SeekOrigin::Current);
        pos -= 3;
      }
    }
  }

  std::uint8_t lengthBytes[4];
  std::uint32_t length=0;
  if (!ReadExact(hfile, lengthBytes, 4) ||
      !LegacyAudio::DecodeLength(lengthBytes, 4, length))
    DoHalt("Sound loading error: truncated WAV data length.");
  pos += 4;

  // sfx.length is in bytes; std::vector is element-counted. Bound it first
  // (corrupt values drove huge assigns) and round the allocation UP: an odd
  // length previously overflowed the floor(length/2) buffer by one byte.
  // assign() value-initializes to zero, matching the old HEAP_ZERO_MEMORY
  // behavior. Reject a short payload instead of silently accepting a
  // partially initialized sound.
  if (length > (16u << 20))
    DoHalt("Sound loading error: WAV data length out of range.");
  sfx.length = static_cast<int>(length);
  sfx.lpData.assign(WavAllocSamples(sfx.length), 0);
  if (!EngineAudio::ReadPCM16(hfile, sfx.lpData.data(), sfx.lpData.size(), length))
    DoHalt("Sound loading error: truncated WAV data.");
  Platform::CloseFile(hfile);
}