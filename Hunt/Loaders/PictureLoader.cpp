// ==========================================================================
// PictureLoader.cpp
// ==========================================================================

#include "Hunt.h"
#include "LoadValidate.h"

static void PicLoadFail(const char* what, int value, int limit)
{
  char sz[256];
  sprintf_s(sz, sizeof(sz),
            "Picture loading error: %s (value=%d, limit=%d). File is corrupt or modded.",
            what, value, limit);
  DoHalt(sz);
}

int conv_xGx(int c)
{
  if (!NightVisionOn) return c;
  DWORD a = c;
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

void LoadPicture(TPicture &pic, LPSTR pname, MemoryTag tag)
{
  int C;
  byte fRGB[800][3];
  BITMAPFILEHEADER bmpFH;
  BITMAPINFOHEADER bmpIH;
  DWORD l;
  HANDLE hfile;

  hfile = CreateFile(pname, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if( hfile==INVALID_HANDLE_VALUE )
  {
    char sz[512];
    sprintf_s(sz, sizeof(sz), "Error opening file\n%s.", pname );
    DoHalt(sz);
  }

  if (!ReadExact(hfile, &bmpFH, sizeof(BITMAPFILEHEADER)) ||
      !ReadExact(hfile, &bmpIH, sizeof(BITMAPINFOHEADER)))
    DoHalt("Picture loading error: truncated BMP header.");
  l = sizeof(BITMAPINFOHEADER);

  pic.lpImage.reset();
  pic.lpImage = nullptr;

  pic.W = bmpIH.biWidth;
  pic.H = bmpIH.biHeight;
  // Rows land in byte fRGB[800][3] on the stack: width is structural.
  // Height is heap-checked; both must be positive (negative heights
  // previously wrapped the allocation size).
  size_t pxbytes = 0;
  if (!IsValidBmpWidth(pic.W))
    PicLoadFail("BMP width exceeds row buffer", pic.W, 800);
  if (pic.H <= 0 || !CheckedTransferBytes3((size_t)pic.W, (size_t)pic.H, 2, pxbytes))
    PicLoadFail("BMP dimensions out of range", pic.H, 0);
  pic.lpImage.reset(static_cast<WORD*>(_HeapAlloc(Heap, 0, (DWORD)pxbytes, tag)));

  for (int y=0; y<pic.H; y++)
  {
    if (!ReadExact(hfile, fRGB, (DWORD)(3 * pic.W)))
      DoHalt("Picture loading error: truncated BMP rows.");
    l = (DWORD)(3 * pic.W);
    for (int x=0; x<pic.W; x++)
    {
      C = (static_cast<int>(fRGB[x][2])/8<<10) + (static_cast<int>(fRGB[x][1])/8<< 5) + (static_cast<int>(fRGB[x][0])/8) ;
      *(pic.lpImage.get() + (pic.H-y-1)*pic.W+x) = C;
    }
  }

  CloseHandle( hfile );
}

void LoadPictureTGA(TPicture &pic, LPSTR pname, MemoryTag tag)
{
  WORD w = 0, h = 0;
  HANDLE hfile;

  hfile = CreateFile(pname, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if( hfile==INVALID_HANDLE_VALUE )
  {
    char sz[512];
    sprintf_s(sz, sizeof(sz), "Error opening file\n%s.", pname );
    DoHalt(sz);
  }

  if (SetFilePointer(hfile, 12, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
      !ReadExact(hfile, &w, 2) || !ReadExact(hfile, &h, 2))
    DoHalt("Picture loading error: truncated TGA header.");

  if (SetFilePointer(hfile, 18, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    DoHalt("Picture loading error: truncated TGA header.");

  pic.lpImage.reset();
  pic.lpImage = nullptr;

  pic.W = w;
  pic.H = h;
  // w/h are WORDs but their product still overflows 32-bit int arithmetic
  // (65535^2*2); compute checked. Reads go straight to the heap buffer,
  // so no stack width cap applies here.
  size_t tpxbytes = 0;
  if (pic.W <= 0 || pic.H <= 0 ||
      !CheckedTransferBytes3((size_t)pic.W, (size_t)pic.H, 2, tpxbytes))
    PicLoadFail("TGA dimensions out of range", pic.W, pic.H);
  pic.lpImage.reset(static_cast<WORD*>(_HeapAlloc(Heap, 0, (DWORD)tpxbytes, tag)));

  for (int y=0; y<pic.H; y++)
    if (!ReadExact(hfile,
                   (void*)(pic.lpImage.get() + (pic.H-y-1)*pic.W),
                   (DWORD)(2*pic.W)))
      DoHalt("Picture loading error: truncated TGA rows.");

  CloseHandle( hfile );
}