#pragma once

// Compiled Papyrus scripts (.pex, Skyrim: big-endian, version 3.x): what Dova Stash's plugin asks of a
// script at run time to recognise a display (src/plugin/Game/Displays.cpp): its variables (auto properties'
// backing "::name_var" included, as bare names), the functions and events it defines in any state, and the
// script it extends. The editor reads the same .pex files the game runs (loose files over archives), so
// its displays are the game's displays. Code is skipped, not decoded.

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dova::editor::esp
{
	struct PexScript
	{
		struct Variable
		{
			std::string name;  // bare: "::isPlaced_var" -> "isPlaced"
			std::string type;  // "bool", "int", "ObjectReference"...
		};

		std::string              name;
		std::string              parent;     // the script it extends ("" for none)
		std::vector<Variable>    variables;
		std::vector<std::string> functions;  // every state's, events included ("OnItemAdded")
	};

	[[nodiscard]] std::optional<PexScript> ParsePex(std::span<const std::byte> a_data);

	// Scripts by name, read on demand through `a_read` ("scripts\\<name>.pex") and kept. Thread-safe.
	class ScriptLibrary
	{
	public:
		using ReadFile = std::function<std::optional<std::vector<std::byte>>(std::string_view a_path)>;

		explicit ScriptLibrary(ReadFile a_read) :
			_read(std::move(a_read))
		{}

		// nullptr when the script has no readable .pex (the game could not run it either).
		[[nodiscard]] const PexScript* Find(std::string_view a_name);

		// Over the script and the scripts it extends, as the plugin's checks see them.
		[[nodiscard]] bool HasPlacedFlag(std::string_view a_name);  // a bool or int variable named "*placed*"
		[[nodiscard]] bool Handles(std::string_view a_name, std::string_view a_function);
		// The Creation Club's display plaques and copies of them: a keyword variable LinkedMarkerKeyword (the
		// marker the item is shown on) and a DisplayItem function.
		[[nodiscard]] bool IsMarkerDisplay(std::string_view a_name);

	private:
		template <class F>
		bool AnyInChain(std::string_view a_name, F&& a_test);

		ReadFile                                                           _read;
		std::mutex                                                         _mutex;
		std::unordered_map<std::string, std::unique_ptr<const PexScript>> _scripts;  // lower-case name -> script (null: none)
	};
}
