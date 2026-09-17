// ==========================================================================
// PictureLoader.cpp
// ==========================================================================

#include "Hunt.h"
#include "LoadValidate.h"
#include "ImageIO.h"

static void PicLoadFail(const char* what, int value, int limit)
{
  char sz[256];
  snprintf(sz, sizeof(sz),
            "Picture loading error: %s (value=%d, limit=%d). File is corrupt or modded.",
            what, value, limit);
  DoHalt(sz);
}

int conv_xGx(int c)
{
  if (!NightVisionOn) return c;
  std::uint32_t a = c;
  int r = ((c>> 0) & 0xFF);
  int g = ((c>> 8) & 0xFF);
  int b = ((c>>16) & 0xFF);
  c = MAX(r,g);
  c = MAX(c,b);
  return (c<<8) + (a & 0xFF000000);
}

void conv_pic(TPicture &pic)
{
  if (!HARD3D) return;
  for (int y=0; y<pic.H; y++)
    for (int x=0; x<pic.W; x++)
      *(pic.lpImage.get() + x + y*pic.W) = conv_565(*(pic.lpImage.get() + x + y*pic.W));
}

void LoadPicture(TPicture &pic, const char* pname, MemoryTag tag)
{
  int C;
  std::uint8_t fRGB[800][3];
  std::array<std::uint8_t,LegacyImage::BmpHeaderSize> headerBytes;
  LegacyImage::BmpHeader header;
  Platform::FileHandle hfile;

  hfile = Platform::OpenFile(pname, Platform::FileMode::Read, false);
  if( hfile==Platform::InvalidFile )
  {
    char sz[512];
    snprintf(sz, sizeof(sz), "Error opening file\n%s.", pname );
    DoHalt(sz);
  }

  if (!ReadExact(hfile, headerBytes.data(), static_cast<std::uint32_t>(headerBytes.size())) ||
      !LegacyImage::DecodeBmpHeader(headerBytes.data(), headerBytes.size(), header))
    DoHalt("Picture loading error: truncated BMP header.");

  pic.lpImage.reset();
  pic.lpImage = nullptr;

  pic.W = header.width;
  pic.H = header.height;
  // Rows land in byte fRGB[800][3] on the stack: width is structural.
  // Height is heap-checked; both must be positive (negative heights
  // previously wrapped the allocation size).
  size_t pxbytes = 0;
  if (!IsValidBmpWidth(pic.W))
    PicLoadFail("BMP width exceeds row buffer", pic.W, 800);
  if (!CheckedPictureBytes(pic.W, pic.H, pxbytes))
    PicLoadFail("BMP dimensions out of range", pic.H, 0);
  pic.lpImage.reset(static_cast<std::uint16_t*>(_HeapAlloc(Heap, 0, pxbytes, tag)));

  for (int y=0; y<pic.H; y++)
  {
    if (!ReadExact(hfile, fRGB, (std::uint32_t)(3 * pic.W)))
      DoHalt("Picture loading error: truncated BMP rows.");
    for (int x=0; x<pic.W; x++)
    {
      C = (static_cast<int>(fRGB[x][2])/8<<10) + (static_cast<int>(fRGB[x][1])/8<< 5) + (static_cast<int>(fRGB[x][0])/8) ;
      *(pic.lpImage.get() + (pic.H-y-1)*pic.W+x) = C;
    }
  }

  Platform::CloseFile( hfile );
}

void LoadPictureTGA(TPicture &pic, const char* pname, MemoryTag tag)
{
  std::array<std::uint8_t,LegacyImage::TgaHeaderSize> headerBytes;
  LegacyImage::TgaHeader header;
  Platform::FileHandle hfile;

  hfile = Platform::OpenFile(pname, Platform::FileMode::Read, false);
  if( hfile==Platform::InvalidFile )
  {
    char sz[512];
    snprintf(sz, sizeof(sz), "Error opening file\n%s.", pname );
    DoHalt(sz);
  }

  if (!ReadExact(hfile, headerBytes.data(), static_cast<std::uint32_t>(headerBytes.size())) ||
      !LegacyImage::DecodeTgaHeader(headerBytes.data(), headerBytes.size(), header))
    DoHalt("Picture loading error: truncated TGA header.");

  pic.lpImage.reset();
  pic.lpImage = nullptr;

  pic.W = header.width;
  pic.H = header.height;
  // Serialized dimensions are uint16 values but their product still overflows 32-bit int arithmetic
  // (65535^2*2); compute checked. Decoded rows land in the heap buffer,
  // so no stack width cap applies here.
  size_t tpxbytes = 0;
  if (!CheckedPictureBytes(pic.W, pic.H, tpxbytes))
    PicLoadFail("TGA dimensions out of range", pic.W, pic.H);
  pic.lpImage.reset(static_cast<std::uint16_t*>(_HeapAlloc(Heap, 0, tpxbytes, tag)));

  for (int y=0; y<pic.H; y++)
    if (!EngineImage::ReadPixels(hfile,
                   pic.lpImage.get() + (pic.H-y-1)*pic.W, pic.W, pic.W))
      DoHalt("Picture loading error: truncated TGA rows.");

  Platform::CloseFile( hfile );
}