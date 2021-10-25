#include "include/pasteboard/pasteboard_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <atlimage.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <memory>
#include "strconv.h"

namespace {

PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp) {
  BITMAP bmp;
  PBITMAPINFO pbmi;
  WORD cClrBits;

  // Retrieve the bitmap color format, width, and height.
  assert(GetObject(hBmp, sizeof(BITMAP), (LPSTR) &bmp));

  // Convert the color format to a count of bits.
  cClrBits = (WORD) (bmp.bmPlanes * bmp.bmBitsPixel);
  if (cClrBits == 1)
    cClrBits = 1;
  else if (cClrBits <= 4)
    cClrBits = 4;
  else if (cClrBits <= 8)
    cClrBits = 8;
  else if (cClrBits <= 16)
    cClrBits = 16;
  else if (cClrBits <= 24)
    cClrBits = 24;
  else cClrBits = 32;

  // Allocate memory for the BITMAPINFO structure. (This structure
  // contains a BITMAPINFOHEADER structure and an array of RGBQUAD
  // data structures.)

  if (cClrBits < 24)
    pbmi = (PBITMAPINFO) LocalAlloc(LPTR,
                                    sizeof(BITMAPINFOHEADER) +
                                        sizeof(RGBQUAD) * int(1 << cClrBits));

    // There is no RGBQUAD array for these formats: 24-bit-per-pixel or 32-bit-per-pixel

  else
    pbmi = (PBITMAPINFO) LocalAlloc(LPTR,
                                    sizeof(BITMAPINFOHEADER));

  // Initialize the fields in the BITMAPINFO structure.

  pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  pbmi->bmiHeader.biWidth = bmp.bmWidth;
  pbmi->bmiHeader.biHeight = bmp.bmHeight;
  pbmi->bmiHeader.biPlanes = bmp.bmPlanes;
  pbmi->bmiHeader.biBitCount = bmp.bmBitsPixel;
  if (cClrBits < 24)
    pbmi->bmiHeader.biClrUsed = (1 << cClrBits);

  // If the bitmap is not compressed, set the BI_RGB flag.
  pbmi->bmiHeader.biCompression = BI_RGB;

  // Compute the number of bytes in the array of color
  // indices and store the result in biSizeImage.
  // The width must be DWORD aligned unless the bitmap is RLE
  // compressed.
  pbmi->bmiHeader.biSizeImage = ((pbmi->bmiHeader.biWidth * cClrBits + 31) & ~31) / 8
      * pbmi->bmiHeader.biHeight;
  // Set biClrImportant to 0, indicating that all of the
  // device colors are important.
  pbmi->bmiHeader.biClrImportant = 0;
  return pbmi;
}

void CreateBMPFile(LPCTSTR pszFile, HBITMAP hBMP) {
  HANDLE hf;                 // file handle
  BITMAPFILEHEADER hdr;       // bitmap file-header
  PBITMAPINFOHEADER pbih;     // bitmap info-header
  LPBYTE lpBits;              // memory pointer
  DWORD dwTotal;              // total count of bytes
  DWORD cb;                   // incremental count of bytes
  BYTE *hp;                   // byte pointer
  DWORD dwTmp;
  PBITMAPINFO pbi;
  HDC hDC;

  hDC = CreateCompatibleDC(GetWindowDC(GetDesktopWindow()));
  SelectObject(hDC, hBMP);

  pbi = CreateBitmapInfoStruct(hBMP);

  pbih = (PBITMAPINFOHEADER) pbi;
  lpBits = (LPBYTE) GlobalAlloc(GMEM_FIXED, pbih->biSizeImage);

  assert(lpBits);

  // Retrieve the color table (RGBQUAD array) and the bits
  // (array of palette indices) from the DIB.
  assert(GetDIBits(hDC, hBMP, 0, (WORD) pbih->biHeight, lpBits, pbi,
                   DIB_RGB_COLORS));

  // Create the .BMP file.
  hf = CreateFile(pszFile,
                  GENERIC_READ | GENERIC_WRITE,
                  (DWORD) 0,
                  NULL,
                  CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL,
                  (HANDLE) NULL);
  assert(hf != INVALID_HANDLE_VALUE);

  hdr.bfType = 0x4d42;        // 0x42 = "B" 0x4d = "M"
  // Compute the size of the entire file.
  hdr.bfSize = (DWORD) (sizeof(BITMAPFILEHEADER) +
      pbih->biSize + pbih->biClrUsed
      * sizeof(RGBQUAD) + pbih->biSizeImage);
  hdr.bfReserved1 = 0;
  hdr.bfReserved2 = 0;

  // Compute the offset to the array of color indices.
  hdr.bfOffBits = (DWORD) sizeof(BITMAPFILEHEADER) +
      pbih->biSize + pbih->biClrUsed
      * sizeof(RGBQUAD);

  // Copy the BITMAPFILEHEADER into the .BMP file.
  assert(WriteFile(hf, (LPVOID) &hdr, sizeof(BITMAPFILEHEADER),
                   (LPDWORD) &dwTmp, NULL));

  // Copy the BITMAPINFOHEADER and RGBQUAD array into the file.
  assert(WriteFile(hf, (LPVOID) pbih, sizeof(BITMAPINFOHEADER)
                       + pbih->biClrUsed * sizeof(RGBQUAD),
                   (LPDWORD) &dwTmp, (NULL)));

  // Copy the array of color indices into the .BMP file.
  dwTotal = cb = pbih->biSizeImage;
  hp = lpBits;
  assert(WriteFile(hf, (LPSTR) hp, (int) cb, (LPDWORD) &dwTmp, NULL));

  // Close the .BMP file.
  assert(CloseHandle(hf));

  // Free memory.
  GlobalFree((HGLOBAL) lpBits);
}

void CreateBitmapHeaderWithColorDepth(LONG width,
                                      LONG height,
                                      WORD color_depth,
                                      BITMAPINFOHEADER *hdr) {
  // These values are shared with gfx::PlatformDevice.
  hdr->biSize = sizeof(BITMAPINFOHEADER);
  hdr->biWidth = width;
  hdr->biHeight = -height;  // Minus means top-down bitmap.
  hdr->biPlanes = 1;
  hdr->biBitCount = color_depth;
  hdr->biCompression = BI_RGB;  // No compression.
  hdr->biSizeImage = 0;
  hdr->biXPelsPerMeter = 1;
  hdr->biYPelsPerMeter = 1;
  hdr->biClrUsed = 0;
  hdr->biClrImportant = 0;
}

HBITMAP CreateHBitmapXRGB8888(int width, int height, HANDLE shared_section, void **data) {
  if (width == 0 || height == 0) {
    width = 1;
    height = 1;
  }
  BITMAPINFOHEADER hdr = {0};
  CreateBitmapHeaderWithColorDepth(width, height, 32, &hdr);
  HBITMAP hbitmap = CreateDIBSection(nullptr, reinterpret_cast<const BITMAPINFO *>(&hdr), 0, data, shared_section, 0);
  return hbitmap;
}

// A scoper to manage acquiring and automatically releasing the clipboard.
class ScopedClipboard {
 public:
  ScopedClipboard() : opened_(false) {}

  ~ScopedClipboard() {
    if (opened_)
      Release();
  }

  bool Acquire(HWND owner) {
    const int kMaxAttemptsToOpenClipboard = 5;

    if (opened_) {
      return false;
    }

    for (int attempts = 0; attempts < kMaxAttemptsToOpenClipboard; ++attempts) {
      if (::OpenClipboard(owner)) {
        opened_ = true;
        return true;
      }

      // If we didn't manage to open the clipboard, sleep a bit and be hopeful.
      ::Sleep(5);
    }

    // We failed to acquire the clipboard.
    return false;
  }

  void Release() {
    if (opened_) {
      ::CloseClipboard();
      opened_ = false;
    }
  }

 private:
  bool opened_;
};

class PasteboardPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  PasteboardPlugin();

  virtual ~PasteboardPlugin();

 private:
  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
};

// static
void PasteboardPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "pasteboard",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<PasteboardPlugin>();

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

PasteboardPlugin::PasteboardPlugin() {}

PasteboardPlugin::~PasteboardPlugin() {}

void PasteboardPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name() == "image") {
    if (!IsClipboardFormatAvailable(CF_DIB)) {
      result->Success();
      return;
    }
    ScopedClipboard clipboard;

    if (!clipboard.Acquire(nullptr)) {
      result->Success();
      return;
    }
    // We use a DIB rather than a DDB here since ::GetObject() with the
    // HBITMAP returned from ::GetClipboardData(CF_BITMAP) always reports a color
    // depth of 32bpp.
    auto *bitmap = static_cast<BITMAPINFO *>(::GetClipboardData(CF_DIB));
    if (!bitmap) {
      result->Success();
      return;
    }

    int color_table_length = 0;

    // For more information on BITMAPINFOHEADER and biBitCount definition,
    // see https://docs.microsoft.com/en-us/windows/win32/wmdm/-bitmapinfoheader
    switch (bitmap->bmiHeader.biBitCount) {
      case 1:
      case 4:
      case 8:
        color_table_length = bitmap->bmiHeader.biClrUsed
                             ? int(bitmap->bmiHeader.biClrUsed)
                             : int(1 << bitmap->bmiHeader.biBitCount);
        break;
      case 16:
      case 32:
        if (bitmap->bmiHeader.biCompression == BI_BITFIELDS)
          color_table_length = 3;
        break;
      case 24:break;
      default:result->Success();
        return;
    }

    const void *bitmap_bits = reinterpret_cast<const char *>(bitmap) +
        bitmap->bmiHeader.biSize +
        color_table_length * sizeof(RGBQUAD);

    void *dst_bits;
    auto dst_hbitmap = CreateHBitmapXRGB8888(bitmap->bmiHeader.biWidth, bitmap->bmiHeader.biHeight,
                                             nullptr, &dst_bits);

    auto hdc = CreateCompatibleDC(nullptr);
    auto old_hbitmap = static_cast<HBITMAP>(SelectObject(hdc, dst_hbitmap));
    ::SetDIBitsToDevice(hdc, 0, 0, bitmap->bmiHeader.biWidth,
                        bitmap->bmiHeader.biHeight, 0, 0, 0,
                        bitmap->bmiHeader.biHeight, bitmap_bits, bitmap,
                        DIB_RGB_COLORS);
    SelectObject(hdc, old_hbitmap);
    DeleteDC(hdc);

    TCHAR path[MAX_PATH];
    GetTempPath(MAX_PATH, path);
    TCHAR name[MAX_PATH];
    GetTempFileName(path, L"pasteboard", false, name);
    CreateBMPFile(name, dst_hbitmap);

    DeleteObject(dst_hbitmap);

    result->Success(flutter::EncodableValue(wide_to_utf8(name)));
  } else if (method_call.method_name() == "files") {
    if (!IsClipboardFormatAvailable(CF_HDROP)) {
      result->Success();
      return;
    }
    if (!OpenClipboard(nullptr)) {
      result->Error("0", "open clipboard failed");
      return;
    }
    auto handle = GetClipboardData(CF_HDROP);
    flutter::EncodableList file_list;
    if (handle) {
      auto data = reinterpret_cast<HDROP>(GlobalLock(handle));
      if (data) {
        auto files = DragQueryFile(data, 0xFFFFFFFF, nullptr, 0);
        for (unsigned int i = 0; i < files; ++i) {
          TCHAR filename[MAX_PATH];
          DragQueryFile(data, i, filename, sizeof(TCHAR) * MAX_PATH);
          std::wstring wide_filename(filename);
          file_list.emplace_back(wide_to_utf8(wide_filename));
        }
      }
    }
    CloseClipboard();
    result->Success(flutter::EncodableValue(file_list));
  } else {
    result->NotImplemented();
  }
}

}  // namespace

void PasteboardPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  PasteboardPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}