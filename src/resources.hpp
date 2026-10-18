#pragma once
#include <span>
#include <string_view>
#include <filesystem>
#include <windows.h>
#include <wincodec.h>
std::span<const unsigned char> embeddedFile(std::string_view path);
inline HRESULT resourceDecoder(IWICImagingFactory *factory, const std::filesystem::path &path,
                               IWICBitmapDecoder **decoder) {
  auto bytes = embeddedFile(path.generic_string());
  if (bytes.empty())
    return factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnLoad, decoder);
  IWICStream *stream{};
  HRESULT result = factory->CreateStream(&stream);
  if (FAILED(result))
    return result;
  result = stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), DWORD(bytes.size()));
  if (SUCCEEDED(result))
    result =
        factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, decoder);
  stream->Release();
  return result;
}
