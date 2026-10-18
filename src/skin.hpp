#pragma once
#include "resources.hpp"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <commdlg.h>
#include <wincodec.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>
inline bool skinSlim = false;
struct ComRelease {
  template <class T> void operator()(T *p) const {
    if (p)
      p->Release();
  }
};
template <class T> using ComPtr = std::unique_ptr<T, ComRelease>;
inline std::vector<unsigned char> skinPixels(const std::filesystem::path &path) {
  struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() {
      if (SUCCEEDED(result))
        CoUninitialize();
    }
  } apartment;
  auto check = [](HRESULT r) {
    if (FAILED(r))
      throw std::runtime_error("Cannot read skin PNG");
  };
  IWICImagingFactory *f{};
  check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)));
  ComPtr<IWICImagingFactory> factory(f);
  IWICBitmapDecoder *d{};
  check(resourceDecoder(f, path, &d));
  ComPtr<IWICBitmapDecoder> decoder(d);
  GUID format{};
  check(d->GetContainerFormat(&format));
  if (!IsEqualGUID(format, GUID_ContainerFormatPng))
    throw std::runtime_error("Select a PNG skin");
  IWICBitmapFrameDecode *b{};
  check(d->GetFrame(0, &b));
  ComPtr<IWICBitmapFrameDecode> frame(b);
  UINT w{}, h{};
  check(b->GetSize(&w, &h));
  if (w != 64 || (h != 64 && h != 32))
    throw std::runtime_error("Skin must be 64x64 or 64x32 pixels");
  IWICFormatConverter *c{};
  check(f->CreateFormatConverter(&c));
  ComPtr<IWICFormatConverter> converter(c);
  check(c->Initialize(b, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                      WICBitmapPaletteTypeCustom));
  std::vector<unsigned char> rgba(64 * 64 * 4, 0);
  check(c->CopyPixels(nullptr, 256, 64 * h * 4, rgba.data()));
  skinSlim = false;
  if (h == 64) {
    skinSlim = true;
    for (int y = 20; y < 32; y++)
      skinSlim &= rgba[(y * 64 + 54) * 4 + 3] == 0 && rgba[(y * 64 + 55) * 4 + 3] == 0;
  }
  if (h == 32) {
    auto mirror = [&](int sx, int sy, int dx, int dy, int w, int tall) {
      for (int y = 0; y < tall; y++)
        for (int x = 0; x < w; x++)
          for (int c = 0; c < 4; c++)
            rgba[((dy + y) * 64 + dx + x) * 4 + c] = rgba[((sy + y) * 64 + sx + w - 1 - x) * 4 + c];
    };
    for (int offset : {0, 40}) {
      int dest = offset == 0 ? 16 : 32;
      mirror(offset + 4, 16, dest + 4, 48, 4, 4);
      mirror(offset + 8, 16, dest + 8, 48, 4, 4);
      mirror(offset, 20, dest + 8, 52, 4, 12);
      mirror(offset + 4, 20, dest + 4, 52, 4, 12);
      mirror(offset + 8, 20, dest, 52, 4, 12);
      mirror(offset + 12, 20, dest + 12, 52, 4, 12);
    }
  }
  return rgba;
}
inline bool selectSkin(GLFWwindow *window) {
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = glfwGetWin32Window(window);
  dialog.lpstrFilter = L"Minecraft skin PNG\0*.png\0\0";
  dialog.lpstrFile = file;
  dialog.nMaxFile = 32768;
  dialog.lpstrTitle = L"Select Minecraft skin (64x64 or 64x32 PNG)";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog))
    return false;
  skinPixels(file);
  std::error_code error;
  if (std::filesystem::equivalent(std::filesystem::path(file),
                                  std::filesystem::absolute("player-skin.png"), error))
    return true;
  std::filesystem::copy_file(file, "player-skin.png",
                             std::filesystem::copy_options::overwrite_existing);
  return true;
}
