#include "home/TemplateFile.h"

#include <chrono>
#include <format>
#include <fstream>

#include "Util.h"
#include "dova/core/Template.h"
#include "dova/core/Version.h"

namespace fs = std::filesystem;

namespace dova::editor::home
{
	fs::path TemplatePath(const fs::path& a_base, const core::Layout& a_template)
	{
		const auto author = core::TemplateFileStem(a_template.author.empty() ? std::string("Unknown") : a_template.author);
		const auto file = core::TemplateFileStem(a_template.name) + ".json";
		return a_base / L"SKSE" / L"Plugins" / L"DovaStash" / L"Templates" / PathFromUtf8(author) / PathFromUtf8(file);
	}

	SaveResult SaveTemplateFile(const fs::path& a_base, core::Layout& a_template, bool a_modMeta)
	{
		SaveResult      r;
		std::error_code ec;
		r.path = TemplatePath(a_base, a_template);
		core::Layout out = a_template;
		out.requiredPlugins = core::ReferencedPlugins(out);
		if (out.version.empty()) {
			out.version = "1.0.0";
		}
		if (fs::exists(r.path, ec)) {
			// As the DLL does: back up the old file, write a version above it.
			core::Diagnostics diags;
			if (const auto old = core::LoadLayoutFile(r.path, "old", diags)) {
				if (core::CompareVersionStrings(out.version, old->version) != std::strong_ordering::greater) {
					out.version = core::NextPatchVersion(old->version);
				}
			}
			std::string stamp;
			try {
				stamp = std::format("{:%Y%m%d-%H%M%S}", std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) });
			} catch (const std::exception&) {  // no time zone data: UTC
				stamp = std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
			}
			r.backup = fs::path(r.path.native() + L"." + Widen(stamp) + L".bak");
			if (!fs::copy_file(r.path, r.backup, fs::copy_options::overwrite_existing, ec)) {
				r.error = "could not back up " + PathToUtf8(r.path) + ": " + ec.message();
				return r;
			}
			r.replaced = true;
		}
		if (a_modMeta && !fs::exists(a_base / L"meta.ini", ec)) {
			fs::create_directories(a_base, ec);
			std::ofstream meta(a_base / L"meta.ini", std::ios::binary);
			meta << "[General]\r\nmodid=0\r\nversion=1.0.0\r\ncomments=Templates made with Dova Stash Editor\r\n";
		}
		fs::create_directories(r.path.parent_path(), ec);
		if (!core::SaveLayoutFile(r.path, out)) {
			r.error = "could not write " + PathToUtf8(r.path);
			return r;
		}
		a_template.version = out.version;
		a_template.requiredPlugins = out.requiredPlugins;
		r.version = out.version;
		r.ok = true;
		return r;
	}
}
