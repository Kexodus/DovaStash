#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		struct FileTables
		{
			std::array<const RE::TESFile*, 256>                  full{};
			std::vector<const RE::TESFile*>                      light;   // index = smallFileCompileIndex
			std::unordered_map<std::string, const RE::TESFile*> byName;  // lower-cased file name
			std::vector<std::string>                             names;
			bool                                                 ready{ false };
		};

		FileTables& Tables()
		{
			static FileTables tables;
			return tables;
		}

		void AddFile(FileTables& a_tables, const RE::TESFile* a_file)
		{
			const auto name = a_file->GetFilename();
			if (name.empty()) {
				return;
			}
			a_tables.byName.emplace(core::ToLower(name), a_file);
			a_tables.names.emplace_back(name);
		}

		bool IsLightLoaded(const RE::TESFile* a_file) noexcept
		{
			return a_file->compileIndex == 0xFE;
		}
	}

	void InitFormUtil()
	{
		auto& t = Tables();
		t = FileTables{};

		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			logger::error("FormUtil: TESDataHandler unavailable");
			return;
		}

		const auto fullCount = dh->GetLoadedModCount();
		if (auto** mods = dh->GetLoadedMods()) {
			for (std::uint32_t i = 0; i < fullCount; ++i) {
				const auto* file = mods[i];
				if (!file) {
					continue;
				}
				t.full[file->compileIndex] = file;
				AddFile(t, file);
			}
		}

		const auto lightCount = dh->GetLoadedLightModCount();
		if (auto** mods = dh->GetLoadedLightMods()) {
			for (std::uint32_t i = 0; i < lightCount; ++i) {
				const auto* file = mods[i];
				if (!file) {
					continue;
				}
				const auto idx = file->smallFileCompileIndex;
				if (idx >= t.light.size()) {
					t.light.resize(static_cast<std::size_t>(idx) + 1, nullptr);
				}
				t.light[idx] = file;
				AddFile(t, file);
			}
		}

		t.ready = true;
		logger::info("FormUtil: {} full + {} light plugins loaded", fullCount, lightCount);
	}

	bool IsFormUtilReady() noexcept
	{
		return Tables().ready;
	}

	std::optional<core::FormRef> ToFormRef(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return std::nullopt;
		}
		const auto id = a_form->GetFormID();
		const auto hi = id >> 24;
		if (hi == 0xFF) {
			return std::nullopt;  // created at runtime: no stable identity
		}

		const auto&        t = Tables();
		const RE::TESFile* file = nullptr;
		std::uint32_t      local = 0;
		if (hi == 0xFE) {
			const auto idx = (id >> 12) & 0xFFF;
			if (idx < t.light.size()) {
				file = t.light[idx];
			}
			local = id & 0xFFF;
		} else {
			file = t.full[hi];
			local = id & 0x00FFFFFF;
		}

		if (!file) {
			// Should not happen once InitFormUtil ran; fall back to the defining file.
			file = a_form->GetFile(0);
			if (!file) {
				return std::nullopt;
			}
			local = a_form->GetLocalFormID() & core::FormRef::kMaxLocalId;
		}
		return core::FormRef{ std::string(file->GetFilename()), local };
	}

	RE::TESForm* LookupForm(const core::FormRef& a_ref)
	{
		if (!a_ref.IsValid()) {
			return nullptr;
		}
		const auto& t = Tables();
		const auto  it = t.byName.find(core::ToLower(a_ref.plugin));
		if (it == t.byName.end()) {
			return nullptr;
		}
		const auto* file = it->second;

		RE::FormID id = 0;
		if (IsLightLoaded(file)) {
			if (a_ref.localId > 0xFFF) {
				return nullptr;  // not addressable in a light plugin; never guess
			}
			id = 0xFE000000u | (static_cast<std::uint32_t>(file->smallFileCompileIndex) << 12) | a_ref.localId;
		} else {
			id = (static_cast<std::uint32_t>(file->compileIndex) << 24) | (a_ref.localId & 0x00FFFFFF);
		}
		return RE::TESForm::LookupByID(id);
	}

	RE::TESObjectREFR* LookupRef(const core::FormRef& a_ref)
	{
		auto* form = LookupForm(a_ref);
		return form ? form->AsReference() : nullptr;
	}

	const std::vector<std::string>& LoadedPlugins()
	{
		return Tables().names;
	}

	RE::PlayerCharacter* Player()
	{
		return RE::PlayerCharacter::GetSingleton();
	}

	std::string Describe(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "<none>";
		}
		std::string name = a_form->GetName();
		if (name.empty()) {
			name = a_form->GetFormEditorID();
		}
		const auto ref = ToFormRef(a_form);
		return std::format("{} [{}]", name.empty() ? "?" : name,
			ref ? ref->ToString() : std::format("dynamic {:08X}", a_form->GetFormID()));
	}

	std::string RefName(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return {};
		}
		if (const char* name = a_ref->GetDisplayFullName(); name && *name) {
			return name;
		}
		if (const auto* base = a_ref->GetBaseObject()) {
			if (const char* name = base->GetName(); name && *name) {
				return name;
			}
			if (const char* edid = base->GetFormEditorID(); edid && *edid) {
				return edid;
			}
		}
		return std::format("{:08X}", a_ref->GetFormID());
	}

	std::string CellName(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return {};
		}
		if (const char* name = a_cell->GetName(); name && *name) {
			return name;
		}
		if (const char* edid = a_cell->GetFormEditorID(); edid && *edid) {
			return edid;
		}
		return std::format("cell {:08X}", a_cell->GetFormID());
	}

	RE::NiPointer<RE::TESObjectREFR> RefFromHandle(RE::RefHandle a_handle)
	{
		if (a_handle == 0) {
			return {};
		}
		return RE::TESObjectREFR::LookupByHandle(a_handle);
	}
}
