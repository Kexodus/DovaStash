#include "Game/FormUtil.h"

namespace dova::game
{
	namespace
	{
		// The engine's file names are in the ANSI code page (Skyrim is a non-Unicode application); FormRefs,
		// JSON files and our file names are UTF-8.
		std::string AnsiToUtf8(std::string_view a_text)
		{
			if (std::ranges::all_of(a_text, [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
				return std::string(a_text);
			}
			const int wide = MultiByteToWideChar(CP_ACP, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			if (wide <= 0) {
				return std::string(a_text);
			}
			std::wstring w(static_cast<std::size_t>(wide), L'\0');
			MultiByteToWideChar(CP_ACP, 0, a_text.data(), static_cast<int>(a_text.size()), w.data(), wide);
			const int narrow = WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, nullptr, 0, nullptr, nullptr);
			if (narrow <= 0) {
				return std::string(a_text);
			}
			std::string out(static_cast<std::size_t>(narrow), '\0');
			WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, out.data(), narrow, nullptr, nullptr);
			return out;
		}

		struct FileTables
		{
			std::array<const RE::TESFile*, 256>                  full{};
			std::vector<const RE::TESFile*>                      light;   // index = smallFileCompileIndex
			std::unordered_map<std::string, const RE::TESFile*> byName;  // lower-cased UTF-8 file name
			std::vector<std::string>                             names;   // UTF-8
			std::unordered_map<const RE::TESFile*, std::string>  utf8;    // file -> UTF-8 name
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
			auto utf8 = AnsiToUtf8(name);
			a_tables.byName.emplace(core::ToLower(utf8), a_file);
			a_tables.names.emplace_back(utf8);
			a_tables.utf8.emplace(a_file, std::move(utf8));
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
		const auto it = t.utf8.find(file);
		return core::FormRef{ it != t.utf8.end() ? it->second : AnsiToUtf8(file->GetFilename()), local };
	}

	std::optional<RE::FormID> FormIdOf(const core::FormRef& a_ref)
	{
		if (!a_ref.IsValid()) {
			return std::nullopt;
		}
		const auto& t = Tables();
		const auto  it = t.byName.find(core::ToLower(a_ref.plugin));
		if (it == t.byName.end()) {
			return std::nullopt;
		}
		const auto* file = it->second;
		if (IsLightLoaded(file)) {
			if (a_ref.localId > 0xFFF) {
				return std::nullopt;  // not addressable in a light plugin; never guess
			}
			return 0xFE000000u | (static_cast<std::uint32_t>(file->smallFileCompileIndex) << 12) | a_ref.localId;
		}
		return (static_cast<std::uint32_t>(file->compileIndex) << 24) | (a_ref.localId & 0x00FFFFFF);
	}

	RE::TESForm* LookupForm(const core::FormRef& a_ref)
	{
		const auto id = FormIdOf(a_ref);
		return id ? RE::TESForm::LookupByID(*id) : nullptr;
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
		if (const auto* ref = a_form->AsReference(); ref && name.empty()) {
			// References rarely have a name of their own: "Chest (ref)" from the base object.
			if (const auto* base = ref->GetBaseObject()) {
				name = base->GetName();
				if (name.empty()) {
					name = base->GetFormEditorID();
				}
				if (!name.empty()) {
					name += " (ref)";
				}
			}
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
