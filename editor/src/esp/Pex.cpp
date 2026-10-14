#include "esp/Pex.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "dova/core/FormRef.h"

namespace dova::editor::esp
{
	namespace
	{
		constexpr std::uint32_t kMagic = 0xFA57C0DE;

		// Arguments per opcode (Skyrim's 36); call* take a counted list after these.
		constexpr std::array<std::uint8_t, 36> kArgs{ 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 1, 2, 2, 3, 2, 3, 1, 3, 3, 3, 2, 2, 3, 3, 4, 4 };
		constexpr std::uint8_t kCallMethod = 23, kCallParent = 24, kCallStatic = 25;

		// Big-endian.
		struct Reader
		{
			std::span<const std::byte> d;
			std::size_t                p{ 0 };
			bool                       ok{ true };

			std::uint64_t Take(std::size_t a_bytes)
			{
				if (!ok || p + a_bytes > d.size()) {
					ok = false;
					p = d.size();
					return 0;
				}
				std::uint64_t v = 0;
				for (std::size_t i = 0; i < a_bytes; ++i) {
					v = (v << 8) | static_cast<std::uint8_t>(d[p + i]);
				}
				p += a_bytes;
				return v;
			}
			std::uint8_t  U8() { return static_cast<std::uint8_t>(Take(1)); }
			std::uint16_t U16() { return static_cast<std::uint16_t>(Take(2)); }
			std::uint32_t U32() { return static_cast<std::uint32_t>(Take(4)); }

			std::string WString()
			{
				const auto len = U16();
				if (!ok || p + len > d.size()) {
					ok = false;
					return {};
				}
				std::string s(reinterpret_cast<const char*>(d.data() + p), len);
				p += len;
				return s;
			}

			void SkipData()
			{
				switch (U8()) {
				case 0:
					break;
				case 1:
				case 2:
					Take(2);
					break;
				case 3:
				case 4:
					Take(4);
					break;
				case 5:
					Take(1);
					break;
				default:
					ok = false;
					break;
				}
			}

			// A counted argument list: an integer variable data holding the count, then the arguments.
			void SkipVarArgs()
			{
				if (U8() != 3) {
					ok = false;
					return;
				}
				const auto n = static_cast<std::int32_t>(U32());
				for (std::int32_t i = 0; i < n && ok; ++i) {
					SkipData();
				}
			}

			void SkipFunction()
			{
				Take(2 + 2 + 4 + 1);  // return type, docstring, user flags, flags
				const auto params = U16();
				Take(4u * params);
				const auto locals = U16();
				Take(4u * locals);
				const auto instructions = U16();
				for (std::uint16_t i = 0; i < instructions && ok; ++i) {
					const auto op = U8();
					if (op >= kArgs.size()) {
						ok = false;
						return;
					}
					for (std::uint8_t a = 0; a < kArgs[op] && ok; ++a) {
						SkipData();
					}
					if (op == kCallMethod || op == kCallParent || op == kCallStatic) {
						SkipVarArgs();
					}
				}
			}
		};

		// "::isPlaced_var" -> "isPlaced"
		std::string BareName(std::string_view a_name)
		{
			if (a_name.starts_with("::")) {
				a_name.remove_prefix(2);
			}
			if (a_name.ends_with("_var")) {
				a_name.remove_suffix(4);
			}
			return std::string(a_name);
		}
	}

	std::optional<PexScript> ParsePex(std::span<const std::byte> a_data)
	{
		Reader r{ a_data };
		if (r.U32() != kMagic) {
			return std::nullopt;
		}
		r.Take(1 + 1 + 2 + 8);  // version, game, compilation time
		r.WString();            // source file
		r.WString();            // user
		r.WString();            // machine
		std::vector<std::string> strings(r.U16());
		for (auto& s : strings) {
			s = r.WString();
		}
		const auto str = [&](std::uint16_t a_index) { return a_index < strings.size() ? strings[a_index] : std::string{}; };
		if (r.U8() != 0) {  // debug info
			r.Take(8);
			const auto functions = r.U16();
			for (std::uint16_t i = 0; i < functions && r.ok; ++i) {
				r.Take(2 + 2 + 2 + 1);
				r.Take(2u * r.U16());
			}
		}
		r.Take(3u * r.U16());  // user flags: name, flag index
		const auto objects = r.U16();
		if (!r.ok || objects == 0) {
			return std::nullopt;
		}
		PexScript out;
		out.name = str(r.U16());
		r.Take(4);  // size
		out.parent = str(r.U16());
		r.Take(2 + 4 + 2);  // docstring, user flags, auto state
		const auto variables = r.U16();
		for (std::uint16_t i = 0; i < variables && r.ok; ++i) {
			PexScript::Variable v;
			v.name = BareName(str(r.U16()));
			v.type = str(r.U16());
			r.Take(4);
			r.SkipData();
			out.variables.push_back(std::move(v));
		}
		const auto properties = r.U16();
		for (std::uint16_t i = 0; i < properties && r.ok; ++i) {
			r.Take(2 + 2 + 2 + 4);  // name, type, docstring, user flags
			const auto flags = r.U8();
			if (flags & 4) {
				r.Take(2);  // auto variable
			} else {
				if (flags & 1) {
					r.SkipFunction();
				}
				if (flags & 2) {
					r.SkipFunction();
				}
			}
		}
		const auto states = r.U16();
		for (std::uint16_t s = 0; s < states && r.ok; ++s) {
			r.Take(2);  // state name
			const auto functions = r.U16();
			for (std::uint16_t f = 0; f < functions && r.ok; ++f) {
				out.functions.push_back(str(r.U16()));
				r.SkipFunction();
			}
		}
		if (!r.ok) {
			return std::nullopt;
		}
		return out;
	}

	const PexScript* ScriptLibrary::Find(std::string_view a_name)
	{
		const auto key = core::ToLower(a_name);
		{
			std::scoped_lock lock(_mutex);
			if (const auto it = _scripts.find(key); it != _scripts.end()) {
				return it->second.get();
			}
		}
		std::unique_ptr<const PexScript> parsed;
		if (const auto data = _read("scripts\\" + key + ".pex")) {
			if (auto s = ParsePex(*data)) {
				parsed = std::make_unique<const PexScript>(std::move(*s));
			}
		}
		std::scoped_lock lock(_mutex);
		return _scripts.try_emplace(key, std::move(parsed)).first->second.get();
	}

	template <class F>
	bool ScriptLibrary::AnyInChain(std::string_view a_name, F&& a_test)
	{
		std::string name(a_name);
		for (int depth = 0; depth < 16 && !name.empty(); ++depth) {
			const auto* s = Find(name);
			if (!s) {
				return false;
			}
			if (a_test(*s)) {
				return true;
			}
			name = s->parent;
		}
		return false;
	}

	bool ScriptLibrary::HasPlacedFlag(std::string_view a_name)
	{
		return AnyInChain(a_name, [](const PexScript& s) {
			return std::ranges::any_of(s.variables, [](const PexScript::Variable& v) {
				return core::ToLower(v.name).find("placed") != std::string::npos && (core::IEquals(v.type, "bool") || core::IEquals(v.type, "int"));
			});
		});
	}

	bool ScriptLibrary::IsMarkerDisplay(std::string_view a_name)
	{
		const bool marker = AnyInChain(a_name, [](const PexScript& s) {
			return std::ranges::any_of(s.variables, [](const PexScript::Variable& v) { return core::IEquals(v.name, "LinkedMarkerKeyword") && core::IEquals(v.type, "keyword"); });
		});
		return marker && Handles(a_name, "DisplayItem");
	}

	bool ScriptLibrary::Handles(std::string_view a_name, std::string_view a_function)
	{
		return AnyInChain(a_name, [&](const PexScript& s) { return std::ranges::any_of(s.functions, [&](const std::string& f) { return core::IEquals(f, a_function); }); });
	}
}
