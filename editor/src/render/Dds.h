#pragma once

// DDS textures for the 3D view: the formats Skyrim ships (BC1/BC2/BC3/BC4/BC5/BC7, 32-bit BGRA/RGBA, legacy
// 24-bit RGB, 8-bit luminance) straight into a Direct3D 11 shader resource view. Mip levels larger than
// `a_maxSize` are skipped (4K texture packs would otherwise fill the GPU for a preview). Cube maps and
// volume textures are not needed and rejected.

#include <cstddef>
#include <span>
#include <string>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>

namespace dova::editor::render
{
	struct DdsInfo
	{
		unsigned    width{ 0 };
		unsigned    height{ 0 };
		unsigned    mips{ 0 };
		DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
		bool        hasAlpha{ false };
	};

	// nullptr (and `a_error`) when the file is not a usable 2D texture. The caller owns the view.
	[[nodiscard]] ID3D11ShaderResourceView* LoadDds(ID3D11Device* a_device, std::span<const std::byte> a_data, unsigned a_maxSize, DdsInfo& a_info,
		std::string& a_error);
}
