#include "ui/Art.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "data/Assets.h"
#include "render/Dds.h"

namespace dova::editor::ui
{
	namespace
	{
		struct Slot
		{
			const char*                             path;
			std::atomic<ID3D11ShaderResourceView*>  srv{ nullptr };
			std::atomic<unsigned>                   width{ 0 };
			std::atomic<unsigned>                   height{ 0 };
		};

		std::array<Slot, 1> g_slots{ { { "interface\\cc_background.dds" } } };

		struct Loader
		{
			std::mutex  mutex;
			std::thread       thread;
			bool              started{ false };
			std::atomic<bool> loading{ false };

			~Loader() { Release(); }

			void Release()
			{
				std::lock_guard lock(mutex);
				if (thread.joinable()) {
					thread.join();
				}
				for (Slot& s : g_slots) {
					if (ID3D11ShaderResourceView* srv = s.srv.exchange(nullptr)) {
						srv->Release();
					}
					s.width = 0;
					s.height = 0;
				}
				started = false;
			}
		};

		Loader& L()
		{
			static Loader s;
			return s;
		}

		void Load(Assets& a_assets, ID3D11Device* a_device)
		{
			for (Slot& s : g_slots) {
				const auto bytes = a_assets.Read(s.path);
				if (!bytes) {
					continue;
				}
				render::DdsInfo info;
				std::string     error;
				// ID3D11Device is free-threaded: creating the texture here is safe while the UI renders.
				if (ID3D11ShaderResourceView* srv = render::LoadDds(a_device, *bytes, 2048, info, error)) {
					s.width = info.width;
					s.height = info.height;
					s.srv.store(srv);
				}
			}
		}

		void Start(std::shared_ptr<Assets> a_keep, Assets& a_assets, ID3D11Device* a_device)
		{
			Loader&         l = L();
			std::lock_guard lock(l.mutex);
			if (l.started || !a_device) {
				return;
			}
			l.started = true;
			l.loading = true;
			a_device->AddRef();
			l.thread = std::thread([keep = std::move(a_keep), assets = &a_assets, a_device, &l] {
				Load(*assets, a_device);
				a_device->Release();
				l.loading = false;
			});
		}
	}

	ImTextureID ArtTexture(Art a_art)
	{
		const auto i = static_cast<std::size_t>(a_art);
		return i < g_slots.size() ? static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(g_slots[i].srv.load())) : ImTextureID{};
	}

	ImVec2 ArtSize(Art a_art)
	{
		const auto i = static_cast<std::size_t>(a_art);
		if (i >= g_slots.size() || !g_slots[i].srv.load()) {
			return ImVec2(0, 0);
		}
		return ImVec2(static_cast<float>(g_slots[i].width.load()), static_cast<float>(g_slots[i].height.load()));
	}

	ImU32 ArtTint(Art a_art)
	{
		switch (a_art) {
		case Art::Paper:
		default:
			return IM_COL32(255, 236, 205, 51);  // parchment-warm, 20 %
		}
	}

	bool ArtPending()
	{
		return L().loading.load();
	}

	void LoadArt(std::shared_ptr<Assets> a_assets, ID3D11Device* a_device)
	{
		if (a_assets) {
			Assets& ref = *a_assets;
			Start(std::move(a_assets), ref, a_device);
		}
	}

	void LoadArt(Assets& a_assets, ID3D11Device* a_device)
	{
		Start(nullptr, a_assets, a_device);
	}

	void ReleaseArt()
	{
		L().Release();
	}
}
