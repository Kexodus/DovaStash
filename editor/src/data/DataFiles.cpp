#include "data/DataFiles.h"

#include <algorithm>
#include <system_error>

#include "Util.h"
#include "dova/core/FormRef.h"

namespace fs = std::filesystem;

namespace dova::editor
{
	namespace
	{
		std::string Key(std::string_view a_relative)
		{
			std::string k = core::ToLower(a_relative);
			std::ranges::replace(k, '/', '\\');
			return k;
		}
	}

	void DataFiles::AddRoot(const fs::path& a_dir)
	{
		std::error_code ec;
		if (!fs::is_directory(a_dir, ec)) {
			return;
		}
		_roots.push_back(a_dir);
		for (const auto& e : fs::directory_iterator(a_dir, ec)) {
			const auto name = PathToUtf8(e.path().filename());
			if (e.is_regular_file(ec)) {
				_files[Key(name)] = e.path();
			} else if (e.is_directory(ec) && core::IEquals(name, "Strings")) {
				for (const auto& s : fs::directory_iterator(e.path(), ec)) {
					if (s.is_regular_file(ec)) {
						_files[Key("strings\\" + PathToUtf8(s.path().filename()))] = s.path();
					}
				}
			}
		}
	}

	std::optional<fs::path> DataFiles::Find(std::string_view a_relative) const
	{
		const auto it = _files.find(Key(a_relative));
		if (it == _files.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	std::vector<std::string> DataFiles::Plugins() const
	{
		std::vector<std::string> out;
		for (const auto& [key, path] : _files) {
			if (key.find('\\') != std::string::npos) {
				continue;
			}
			if (IEndsWith(key, ".esm") || IEndsWith(key, ".esp") || IEndsWith(key, ".esl")) {
				out.push_back(PathToUtf8(path.filename()));
			}
		}
		std::ranges::sort(out, [](const auto& a, const auto& b) { return core::ICompare(a, b) < 0; });
		return out;
	}
}
