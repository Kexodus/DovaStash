#include "render/Image.h"

#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "Util.h"

namespace dova::editor::render
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		bool SaveBmp(const std::filesystem::path& a_path, unsigned a_w, unsigned a_h, const std::vector<std::uint8_t>& a_rgba)
		{
			std::vector<char> pixels(static_cast<std::size_t>(a_w) * a_h * 4);
			for (unsigned y = 0; y < a_h; ++y) {
				const auto* src = a_rgba.data() + static_cast<std::size_t>(a_h - 1 - y) * a_w * 4;
				char*       dst = pixels.data() + static_cast<std::size_t>(y) * a_w * 4;
				for (unsigned x = 0; x < a_w; ++x) {
					dst[x * 4 + 0] = static_cast<char>(src[x * 4 + 2]);
					dst[x * 4 + 1] = static_cast<char>(src[x * 4 + 1]);
					dst[x * 4 + 2] = static_cast<char>(src[x * 4 + 0]);
					dst[x * 4 + 3] = static_cast<char>(255);
				}
			}
			BITMAPFILEHEADER fh{};
			BITMAPINFOHEADER ih{};
			ih.biSize = sizeof(ih);
			ih.biWidth = static_cast<LONG>(a_w);
			ih.biHeight = static_cast<LONG>(a_h);
			ih.biPlanes = 1;
			ih.biBitCount = 32;
			ih.biCompression = BI_RGB;
			fh.bfType = 0x4D42;
			fh.bfOffBits = sizeof(fh) + sizeof(ih);
			fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
			FILE* f = nullptr;
			if (_wfopen_s(&f, a_path.c_str(), L"wb") != 0 || !f) {
				return false;
			}
			std::fwrite(&fh, sizeof(fh), 1, f);
			std::fwrite(&ih, sizeof(ih), 1, f);
			std::fwrite(pixels.data(), 1, pixels.size(), f);
			std::fclose(f);
			return true;
		}
	}

	bool SaveImage(const std::filesystem::path& a_path, unsigned a_w, unsigned a_h, const std::vector<std::uint8_t>& a_rgba)
	{
		if (a_rgba.size() < static_cast<std::size_t>(a_w) * a_h * 4) {
			return false;
		}
		if (IEndsWith(PathToUtf8(a_path), ".bmp")) {
			return SaveBmp(a_path, a_w, a_h, a_rgba);
		}
		const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		bool          ok = false;
		{
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICStream>         stream;
			ComPtr<IWICBitmapEncoder>  encoder;
			ComPtr<IWICBitmapFrameEncode> frame;
			if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
				SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(a_path.c_str(), GENERIC_WRITE)) &&
				SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
				SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
				SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(a_w, a_h))) {
				WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
				frame->SetPixelFormat(&format);
				const bool bgr24 = IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) != 0;
				const UINT bpp = bgr24 ? 3 : 4;
				std::vector<std::uint8_t> px(static_cast<std::size_t>(a_w) * a_h * bpp);
				for (std::size_t i = 0; i < static_cast<std::size_t>(a_w) * a_h; ++i) {
					px[i * bpp + 0] = a_rgba[i * 4 + 2];
					px[i * bpp + 1] = a_rgba[i * 4 + 1];
					px[i * bpp + 2] = a_rgba[i * 4 + 0];
					if (!bgr24) {
						px[i * bpp + 3] = 255;
					}
				}
				ok = SUCCEEDED(frame->WritePixels(a_h, a_w * bpp, static_cast<UINT>(px.size()), px.data())) && SUCCEEDED(frame->Commit()) &&
				     SUCCEEDED(encoder->Commit());
			}
		}
		if (SUCCEEDED(co)) {
			::CoUninitialize();
		}
		return ok;
	}

	std::vector<std::uint8_t> ReadTexture(ID3D11Device* a_device, ID3D11DeviceContext* a_context, ID3D11Texture2D* a_texture, unsigned& a_width, unsigned& a_height)
	{
		std::vector<std::uint8_t> out;
		if (!a_texture) {
			return out;
		}
		D3D11_TEXTURE2D_DESC desc{};
		a_texture->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		desc.SampleDesc.Count = 1;
		ComPtr<ID3D11Texture2D> staging;
		if (FAILED(a_device->CreateTexture2D(&desc, nullptr, &staging))) {
			return out;
		}
		a_context->CopyResource(staging.Get(), a_texture);
		D3D11_MAPPED_SUBRESOURCE m{};
		if (FAILED(a_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
			return out;
		}
		a_width = desc.Width;
		a_height = desc.Height;
		out.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
		const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
		for (UINT y = 0; y < desc.Height; ++y) {
			const auto* src = static_cast<const std::uint8_t*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch;
			auto*       dst = out.data() + static_cast<std::size_t>(y) * desc.Width * 4;
			std::memcpy(dst, src, static_cast<std::size_t>(desc.Width) * 4);
			if (bgra) {
				for (UINT x = 0; x < desc.Width; ++x) {
					std::swap(dst[x * 4], dst[x * 4 + 2]);
				}
			}
		}
		a_context->Unmap(staging.Get(), 0);
		return out;
	}
}
