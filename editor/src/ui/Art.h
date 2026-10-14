#pragma once

// Art read from the player's own install (docs/EDITOR-UX.md §3.4), optional by design: every function
// returns 0 until the texture is loaded, and when the install does not have it, so callers draw the flat
// theme and add the art when it is there. Nothing is copied into the repo or the zip; SWF is never read.
//
// Paper: interface\cc_background.dds (Skyrim - Interface.bsa, SE/AE): a soft, blurred mountain mist,
// 1280 x 720, meant behind the start panel's title at ~20 % opacity with ArtTint (warms its blue-grey).

#include <memory>

#include <imgui.h>

struct ID3D11Device;

namespace dova::editor
{
	class Assets;
}

namespace dova::editor::ui
{
	enum class Art
	{
		Paper
	};

	[[nodiscard]] ImTextureID ArtTexture(Art a_art);  // 0 until loaded
	[[nodiscard]] ImVec2      ArtSize(Art a_art);     // pixels; (0, 0) until loaded
	[[nodiscard]] ImU32       ArtTint(Art a_art);     // the suggested AddImage tint (colour and opacity)

	// Starts loading every art texture on a background thread (once; later calls do nothing until
	// ReleaseArt). The shared_ptr overload keeps the Assets alive for the load; with a reference the Assets
	// must outlive it (ReleaseArt waits for it).
	void LoadArt(std::shared_ptr<Assets> a_assets, ID3D11Device* a_device);
	void LoadArt(Assets& a_assets, ID3D11Device* a_device);
	// Waits for the load and releases the textures (before the device goes away, or to load again).
	void ReleaseArt();
}
