#pragma once

// Papyrus scripts attached to a record (VMAD), with the properties set on it. Ported from
// tools/esm_dump.py's vmad_decode: object properties keep the file-local raw form id (resolve them
// against the plugin that owns the record). Fragments are ignored.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dova::editor::esp
{
	struct VmadProperty
	{
		enum class Type : std::uint8_t
		{
			None,
			Object,
			String,
			Int,
			Float,
			Bool
		};

		std::string                name;
		Type                       type{ Type::None };
		bool                       array{ false };
		std::vector<std::uint32_t> objects;  // raw form ids (Object / Object array)
		std::int32_t               intValue{ 0 };
		float                      floatValue{ 0.0f };
		bool                       boolValue{ false };
		std::string                stringValue;
	};

	struct VmadScript
	{
		std::string               name;
		std::vector<VmadProperty> properties;
	};

	[[nodiscard]] std::vector<VmadScript> DecodeVmad(std::span<const std::byte> a_vmad);
}
