#include "render/Dds.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

namespace dova::editor::render
{
	namespace
	{
		constexpr std::uint32_t FourCC(char a, char b, char c, char d)
		{
			return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
		}

		std::uint32_t U32(std::span<const std::byte> a_data, std::size_t a_offset)
		{
			std::uint32_t v = 0;
			if (a_offset + 4 <= a_data.size()) {
				std::memcpy(&v, a_data.data() + a_offset, 4);
			}
			return v;
		}

		unsigned BlockBytes(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_BC1_UNORM:
			case DXGI_FORMAT_BC1_UNORM_SRGB:
			case DXGI_FORMAT_BC4_UNORM:
			case DXGI_FORMAT_BC4_SNORM:
				return 8;
			case DXGI_FORMAT_BC2_UNORM:
			case DXGI_FORMAT_BC2_UNORM_SRGB:
			case DXGI_FORMAT_BC3_UNORM:
			case DXGI_FORMAT_BC3_UNORM_SRGB:
			case DXGI_FORMAT_BC5_UNORM:
			case DXGI_FORMAT_BC5_SNORM:
			case DXGI_FORMAT_BC6H_UF16:
			case DXGI_FORMAT_BC6H_SF16:
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_UNORM_SRGB:
				return 16;
			default:
				return 0;
			}
		}

		unsigned PixelBytes(DXGI_FORMAT a_format)
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				return 4;
			case DXGI_FORMAT_R8_UNORM:
			case DXGI_FORMAT_A8_UNORM:
				return 1;
			case DXGI_FORMAT_R8G8_UNORM:
				return 2;
			default:
				return 0;
			}
		}

		// Channel value from a masked pixel, scaled to 0..255.
		std::uint8_t Channel(std::uint32_t a_pixel, std::uint32_t a_mask, std::uint8_t a_default)
		{
			if (a_mask == 0) {
				return a_default;
			}
			const int           shift = std::countr_zero(a_mask);
			const std::uint32_t v = (a_pixel & a_mask) >> shift;
			const std::uint32_t max = a_mask >> shift;
			return static_cast<std::uint8_t>((v * 255 + max / 2) / max);
		}
	}

	ID3D11ShaderResourceView* LoadDds(ID3D11Device* a_device, std::span<const std::byte> a_data, unsigned a_maxSize, DdsInfo& a_info, std::string& a_error)
	{
		if (a_data.size() < 128 || U32(a_data, 0) != FourCC('D', 'D', 'S', ' ') || U32(a_data, 4) != 124) {
			a_error = "not a DDS file";
			return nullptr;
		}
		const std::uint32_t height = U32(a_data, 4 + 8);
		const std::uint32_t width = U32(a_data, 4 + 12);
		std::uint32_t       mips = std::max<std::uint32_t>(1, U32(a_data, 4 + 24));
		const std::uint32_t pfFlags = U32(a_data, 4 + 76);
		const std::uint32_t fourCC = U32(a_data, 4 + 80);
		const std::uint32_t bits = U32(a_data, 4 + 84);
		const std::uint32_t masks[4] = { U32(a_data, 4 + 88), U32(a_data, 4 + 92), U32(a_data, 4 + 96), U32(a_data, 4 + 100) };
		const std::uint32_t caps2 = U32(a_data, 4 + 108);
		if (width == 0 || height == 0 || width > 16384 || height > 16384) {
			a_error = std::format("bad size {}x{}", width, height);
			return nullptr;
		}
		if (caps2 & 0x200) {
			a_error = "cube map";
			return nullptr;
		}
		std::size_t offset = 128;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		bool        convert = false;  // uncompressed with masks: converted to RGBA8
		if (pfFlags & 0x4) {
			switch (fourCC) {
			case FourCC('D', 'X', 'T', '1'):
				format = DXGI_FORMAT_BC1_UNORM;
				break;
			case FourCC('D', 'X', 'T', '2'):
			case FourCC('D', 'X', 'T', '3'):
				format = DXGI_FORMAT_BC2_UNORM;
				break;
			case FourCC('D', 'X', 'T', '4'):
			case FourCC('D', 'X', 'T', '5'):
				format = DXGI_FORMAT_BC3_UNORM;
				break;
			case FourCC('A', 'T', 'I', '1'):
			case FourCC('B', 'C', '4', 'U'):
				format = DXGI_FORMAT_BC4_UNORM;
				break;
			case FourCC('B', 'C', '4', 'S'):
				format = DXGI_FORMAT_BC4_SNORM;
				break;
			case FourCC('A', 'T', 'I', '2'):
			case FourCC('B', 'C', '5', 'U'):
				format = DXGI_FORMAT_BC5_UNORM;
				break;
			case FourCC('B', 'C', '5', 'S'):
				format = DXGI_FORMAT_BC5_SNORM;
				break;
			case FourCC('D', 'X', '1', '0'): {
				if (a_data.size() < 148) {
					a_error = "truncated DX10 header";
					return nullptr;
				}
				format = static_cast<DXGI_FORMAT>(U32(a_data, 128));
				const auto dim = U32(a_data, 132);
				const auto misc = U32(a_data, 136);
				const auto arraySize = U32(a_data, 140);
				if (dim != 3 /* TEXTURE2D */ || (misc & 0x4) || arraySize > 1) {
					a_error = "not a plain 2D texture";
					return nullptr;
				}
				offset = 148;
				break;
			}
			default:
				a_error = std::format("unsupported FourCC 0x{:08X}", fourCC);
				return nullptr;
			}
		} else if ((pfFlags & 0x40) || (pfFlags & 0x20000) || (pfFlags & 0x2)) {
			if (bits == 32 && masks[0] == 0x000000FF && masks[1] == 0x0000FF00 && masks[2] == 0x00FF0000) {
				format = DXGI_FORMAT_R8G8B8A8_UNORM;
			} else if (bits == 32 && masks[0] == 0x00FF0000 && masks[1] == 0x0000FF00 && masks[2] == 0x000000FF) {
				format = masks[3] ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_B8G8R8X8_UNORM;
			} else if (bits == 8 || bits == 16 || bits == 24 || bits == 32) {
				convert = true;
				format = DXGI_FORMAT_R8G8B8A8_UNORM;
			} else {
				a_error = std::format("unsupported {}-bit pixel format", bits);
				return nullptr;
			}
		} else {
			a_error = "unknown pixel format";
			return nullptr;
		}

		const unsigned block = BlockBytes(format);
		const unsigned srcPixel = convert ? bits / 8 : PixelBytes(format);
		if (block == 0 && srcPixel == 0) {
			a_error = std::format("unsupported DXGI format {}", static_cast<int>(format));
			return nullptr;
		}

		// Mip chain: where each level starts in the file.
		struct Level
		{
			std::size_t offset, size, pitch;
			unsigned    w, h;
		};
		std::vector<Level> levels;
		std::size_t        pos = offset;
		unsigned           w = width, h = height;
		for (std::uint32_t m = 0; m < mips && m < 16; ++m) {
			std::size_t pitch, size;
			if (block) {
				pitch = static_cast<std::size_t>(std::max(1u, (w + 3) / 4)) * block;
				size = pitch * std::max(1u, (h + 3) / 4);
			} else {
				pitch = static_cast<std::size_t>(w) * srcPixel;
				size = pitch * h;
			}
			if (pos + size > a_data.size()) {
				break;
			}
			levels.push_back({ pos, size, pitch, w, h });
			pos += size;
			w = std::max(1u, w / 2);
			h = std::max(1u, h / 2);
		}
		if (levels.empty()) {
			a_error = "truncated pixel data";
			return nullptr;
		}
		// Skip levels above the size limit (BC levels must stay multiples of 4).
		std::size_t first = 0;
		while (first + 1 < levels.size() && std::max(levels[first].w, levels[first].h) > a_maxSize) {
			const auto& next = levels[first + 1];
			if (block && (next.w % 4 != 0 || next.h % 4 != 0)) {
				break;
			}
			++first;
		}
		if (block && (levels[first].w % 4 != 0 || levels[first].h % 4 != 0)) {
			a_error = std::format("block-compressed size {}x{} is not a multiple of 4", levels[first].w, levels[first].h);
			return nullptr;
		}

		std::vector<std::vector<std::uint8_t>> converted;
		std::vector<D3D11_SUBRESOURCE_DATA>    init;
		for (std::size_t l = first; l < levels.size(); ++l) {
			const auto&            lv = levels[l];
			D3D11_SUBRESOURCE_DATA d{};
			if (convert) {
				auto& out = converted.emplace_back(static_cast<std::size_t>(lv.w) * lv.h * 4);
				const auto* src = reinterpret_cast<const std::uint8_t*>(a_data.data() + lv.offset);
				const bool  lum = (pfFlags & 0x20000) != 0;
				for (std::size_t p = 0; p < static_cast<std::size_t>(lv.w) * lv.h; ++p) {
					std::uint32_t px = 0;
					std::memcpy(&px, src + p * srcPixel, srcPixel);
					std::uint8_t* o = out.data() + p * 4;
					if (lum) {
						const auto l8 = Channel(px, masks[0] ? masks[0] : 0xFF, 255);
						o[0] = o[1] = o[2] = l8;
						o[3] = Channel(px, masks[3], 255);
					} else if (pfFlags & 0x2 && !(pfFlags & 0x40)) {  // alpha only
						o[0] = o[1] = o[2] = 255;
						o[3] = Channel(px, masks[3] ? masks[3] : 0xFF, 255);
					} else {
						o[0] = Channel(px, masks[0], 0);
						o[1] = Channel(px, masks[1], 0);
						o[2] = Channel(px, masks[2], 0);
						o[3] = Channel(px, masks[3], 255);
					}
				}
				d.pSysMem = out.data();
				d.SysMemPitch = lv.w * 4;
			} else {
				d.pSysMem = a_data.data() + lv.offset;
				d.SysMemPitch = static_cast<UINT>(lv.pitch);
			}
			d.SysMemSlicePitch = 0;
			init.push_back(d);
		}

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = levels[first].w;
		desc.Height = levels[first].h;
		desc.MipLevels = static_cast<UINT>(init.size());
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		ID3D11Texture2D* tex = nullptr;
		if (FAILED(a_device->CreateTexture2D(&desc, init.data(), &tex)) || !tex) {
			a_error = std::format("Direct3D refused the texture ({}x{}, format {}, {} mips)", desc.Width, desc.Height, static_cast<int>(format), desc.MipLevels);
			return nullptr;
		}
		ID3D11ShaderResourceView* srv = nullptr;
		a_device->CreateShaderResourceView(tex, nullptr, &srv);
		tex->Release();
		if (!srv) {
			a_error = "no shader resource view";
			return nullptr;
		}
		a_info.width = desc.Width;
		a_info.height = desc.Height;
		a_info.mips = desc.MipLevels;
		a_info.format = format;
		a_info.hasAlpha = format != DXGI_FORMAT_BC1_UNORM && format != DXGI_FORMAT_B8G8R8X8_UNORM;
		return srv;
	}
}
