#pragma once

// Saving images: PNG through the Windows Imaging Component (no extra library), BMP as a fallback for
// paths ending in .bmp. Pixels are RGBA8, top row first.

#include <cstdint>
#include <filesystem>
#include <vector>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>

namespace dova::editor::render
{
	[[nodiscard]] bool SaveImage(const std::filesystem::path& a_path, unsigned a_width, unsigned a_height, const std::vector<std::uint8_t>& a_rgba);

	// Copies a 2D RGBA8 texture back to the CPU (top row first). Empty on failure.
	[[nodiscard]] std::vector<std::uint8_t> ReadTexture(ID3D11Device* a_device, ID3D11DeviceContext* a_context, ID3D11Texture2D* a_texture, unsigned& a_width,
		unsigned& a_height);
}
