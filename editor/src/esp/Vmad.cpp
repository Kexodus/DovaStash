#include "esp/Vmad.h"

#include "Util.h"
#include "esp/Plugin.h"

namespace dova::editor::esp
{
	namespace
	{
		struct Reader
		{
			std::span<const std::byte> d;
			std::size_t                p{ 0 };
			bool                       ok{ true };

			template <class T>
			T Take()
			{
				if (p + sizeof(T) > d.size()) {
					ok = false;
					p = d.size();
					return T{};
				}
				T v = Read<T>(d.data() + p);
				p += sizeof(T);
				return v;
			}

			std::string WString()
			{
				const auto len = Take<std::uint16_t>();
				if (!ok || p + len > d.size()) {
					ok = false;
					return {};
				}
				std::string s = BytesToUtf8(std::string_view(reinterpret_cast<const char*>(d.data() + p), len));
				p += len;
				return s;
			}
		};
	}

	std::vector<VmadScript> DecodeVmad(std::span<const std::byte> a_vmad)
	{
		std::vector<VmadScript> out;
		if (a_vmad.size() < 6) {
			return out;
		}
		Reader     r{ a_vmad };
		const auto version = r.Take<std::int16_t>();
		const auto objFormat = r.Take<std::int16_t>();
		const auto count = r.Take<std::uint16_t>();

		auto object = [&]() -> std::uint32_t {
			// Object format 1: formid, alias, unused; format 2: unused, alias, formid (8 bytes either way).
			if (r.p + 8 > a_vmad.size()) {
				r.ok = false;
				return 0;
			}
			const auto id = Read<std::uint32_t>(a_vmad.data() + r.p + (objFormat == 1 ? 0 : 4));
			r.p += 8;
			return id;
		};

		for (std::uint16_t s = 0; s < count && r.ok; ++s) {
			VmadScript script;
			script.name = r.WString();
			if (version >= 4) {
				r.Take<std::uint8_t>();  // status
			}
			const auto nprops = r.Take<std::uint16_t>();
			for (std::uint16_t i = 0; i < nprops && r.ok; ++i) {
				VmadProperty prop;
				prop.name = r.WString();
				const auto ptype = r.Take<std::uint8_t>();
				if (version >= 4) {
					r.Take<std::uint8_t>();  // status
				}
				switch (ptype) {
				case 1:
					prop.type = VmadProperty::Type::Object;
					prop.objects.push_back(object());
					break;
				case 2:
					prop.type = VmadProperty::Type::String;
					prop.stringValue = r.WString();
					break;
				case 3:
					prop.type = VmadProperty::Type::Int;
					prop.intValue = r.Take<std::int32_t>();
					break;
				case 4:
					prop.type = VmadProperty::Type::Float;
					prop.floatValue = r.Take<float>();
					break;
				case 5:
					prop.type = VmadProperty::Type::Bool;
					prop.boolValue = r.Take<std::uint8_t>() != 0;
					break;
				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
					{
						prop.array = true;
						const auto n = r.Take<std::uint32_t>();
						for (std::uint32_t k = 0; k < n && r.ok; ++k) {
							switch (ptype) {
							case 11:
								prop.type = VmadProperty::Type::Object;
								prop.objects.push_back(object());
								break;
							case 12:
								prop.type = VmadProperty::Type::String;
								prop.stringValue = r.WString();
								break;
							case 13:
								prop.type = VmadProperty::Type::Int;
								prop.intValue = r.Take<std::int32_t>();
								break;
							case 14:
								prop.type = VmadProperty::Type::Float;
								prop.floatValue = r.Take<float>();
								break;
							default:
								prop.type = VmadProperty::Type::Bool;
								prop.boolValue = r.Take<std::uint8_t>() != 0;
								break;
							}
						}
						break;
					}
				default:
					// Unknown property type: stop, keep what we have (as esm_dump.py does).
					script.properties.push_back(std::move(prop));
					out.push_back(std::move(script));
					return out;
				}
				script.properties.push_back(std::move(prop));
			}
			out.push_back(std::move(script));
		}
		return out;
	}
}
