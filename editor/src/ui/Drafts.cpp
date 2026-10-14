// Homes and drafts (docs/EDITOR.md §3.4, EDITOR-UX.md §2.4, §2.8): every player home of the active load order
// with its template status, a draft per home made by core::SuggestDraft (when the home is opened, or for all of
// them by "Draft all homes" on the worker), and the Draft panel: the open home's master with its reason, then per
// container the suggested categories as chips coloured by confidence (the reason in the tooltip), accepted or
// rejected straight on the template being made. Reviewed drafts stay in memory until saved ("Save N drafts...").

#include <algorithm>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <imgui.h>

#include "Util.h"
#include "dova/core/Template.h"
#include "home/TemplateFile.h"
#include "ui/App.h"
#include "ui/Common.h"
#include "ui/WidgetsExtra.h"

namespace fs = std::filesystem;

namespace dova::editor::ui
{
	using namespace detail;

	namespace
	{
		std::string Label(const core::DraftObject& a_o) { return a_o.name.empty() ? (a_o.editorId.empty() ? a_o.id : a_o.editorId) : a_o.name; }
	}

	std::string EditorApp::HomeStatus(const home::HomeEntry& a_home, bool* a_user) const
	{
		if (!_homes) {
			return {};
		}
		auto list = _homes->templates.ForCell(_homes->lo->ToFormRef(a_home.cell));
		for (const auto& e : a_home.extra) {
			for (const auto* t : _homes->templates.ForCell(_homes->lo->ToFormRef(e.cell))) {
				if (std::ranges::find(list, t) == list.end()) {
					list.push_back(t);
				}
			}
		}
		const bool user = std::ranges::any_of(list, [](const home::KnownTemplate* t) { return t->user; });
		const bool shipped = std::ranges::any_of(list, [](const home::KnownTemplate* t) { return !t->user; });
		if (a_user) {
			*a_user = user;
		}
		return user && shipped ? "yours + shipped" : user ? "yours" : shipped ? "shipped" : "";
	}

	bool EditorApp::UnsavedDrafts() const
	{
		if (_homes) {
			for (std::size_t i = 0; i < _homes->entries.size(); ++i) {
				if (_homes->entries[i].reviewed && _state.StageOf(_homes->homes[i].cellRef) != HomeStage::Saved) {
					return true;
				}
			}
		}
		return false;
	}

	bool EditorApp::AnyUnsaved() const
	{
		return (_cell && _cell->dirty) || UnsavedDrafts();
	}

	int EditorApp::SaveableDrafts() const
	{
		int n = 0;
		if (_homes) {
			for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
				n += _homes->entries[i].draft && HomeStatus(_homes->homes[i]).empty() ? 1 : 0;
			}
		}
		return n;
	}

	void EditorApp::ScanHomes()
	{
		if (!_ws) {
			return;
		}
		if (Busy()) {
			_homesRequested = true;
			Queue([this] { ScanHomes(); });
			return;
		}
		_homesRequested = true;
		LoadCategories();
		_progress = std::make_shared<Progress>();
		const Workspace*         ws = _ws.get();
		const core::CategorySet* cats = _categorySet ? &*_categorySet : nullptr;
		auto                     progress = _progress;
		StartJob("Finding homes", [this, ws, cats, progress]() -> std::function<void()> {
			const auto start = std::chrono::steady_clock::now();
			auto       data = std::make_shared<HomesData>();
			auto       setStage = [&](std::string a_stage) {
                std::scoped_lock lock(progress->mutex);
                progress->stage = std::move(a_stage);
			};
			setStage("opening the load order");
			data->lo = std::shared_ptr<esp::LoadOrder>(ws->OpenLoadOrder(data->errors, [&](std::size_t a_done, std::size_t a_total) {
				progress->done = a_done;
				progress->total = a_total;
			}));
			data->homes = home::FindAllHomes(*data->lo, [&](std::size_t a_done, std::size_t a_total, const std::string& a_stage) {
				progress->done = a_done;
				progress->total = a_total;
				setStage(a_stage);
			});
			setStage("reading templates and items");
			progress->total = 0;
			data->templates = home::LoadTemplateIndex(*ws);
			data->catalog = std::make_shared<esp::ItemCatalog>(esp::BuildItemCatalog(*data->lo));
			if (cats) {
				data->itemNames = home::BuildItemNameIndex(*data->catalog, *cats);
			}
			data->entries.resize(data->homes.size());
			data->ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			return [this, data]() {
				_homes = data;
				GroupHomes();
				ResolvePendingCell();
				if (_pendingDrafts) {
					// --drafts: the bulk draft, the rail showing the homes without a template.
					_pendingDrafts = false;
					_onlyWithoutTemplate = true;
					Queue([this] { StartBulkDraft(); });
				}
				if (_pendingHome) {
					Queue([this] {
						const auto wanted = *std::exchange(_pendingHome, std::nullopt);
						for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
							if (IContains(_homes->homes[i].name, wanted) || IContains(_homes->homes[i].editorId, wanted)) {
								if (!_pendingPanel && _onlyWithoutTemplate) {
									_pendingPanel = Panel::Draft;  // --drafts --home: its review
								}
								OpenHome(static_cast<int>(i));
								return;
							}
						}
						SetStatus("No home matches " + wanted, true);
					});
				}
			};
		});
	}

	void EditorApp::Rescan()
	{
		if (!_ws) {
			return;
		}
		if (UnsavedDrafts() && !_confirmRescan) {
			_confirmRescan = true;  // DrawRescanPopup asks first
			return;
		}
		_confirmRescan = false;
		if (Busy()) {
			Queue([this] { Rescan(); });
			return;
		}
		SyncCellToDraft();
		_homes.reset();
		_homeGroups.clear();
		_cellDraft = -1;  // the open cell keeps its template (and its dirty flag)
		_scans.clear();
		_selectedMod = -2;
		_bulkDone = false;
		_categoriesLoaded = false;
		ScanHomes();
	}

	void EditorApp::DrawRescanPopup()
	{
		if (_confirmRescan && !ImGui::IsPopupOpen("Rescan?")) {
			ImGui::OpenPopup("Rescan?");
		}
		if (ImGui::BeginPopupModal("Rescan?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Reviewed drafts are not saved.");
			w::Muted("Read the load order again and drop them?");
			w::Spacer();
			if (ImGui::Button("Drop and rescan")) {
				if (_homes) {
					for (auto& e : _homes->entries) {
						e.reviewed = false;
					}
				}
				ImGui::CloseCurrentPopup();
				Rescan();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep them") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmRescan = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::StartBulkDraft()
	{
		if (!_ws) {
			return;
		}
		if (!_homes) {
			_pendingDrafts = true;  // after the scan
			if (!_homesRequested) {
				ScanHomes();
			}
			return;
		}
		if (Busy()) {
			Queue([this] { StartBulkDraft(); });
			return;
		}
		LoadCategories();
		SyncCellToDraft();
		// Every home without a draft, on the worker; the UI does not touch the load order meanwhile (one job at a time).
		std::vector<int> todo;
		for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
			if (!_homes->entries[i].draft) {
				todo.push_back(static_cast<int>(i));
			}
		}
		_progress = std::make_shared<Progress>();
		_progress->total = todo.size();
		auto                     homes = _homes;
		auto                     progress = _progress;
		const home::DraftContext ctx{ _categorySet ? &*_categorySet : nullptr, &_draftRules, &_homes->itemNames };
		const std::string        author = _templateAuthor;
		StartJob("Drafting all homes", [this, homes, progress, ctx, todo, author]() -> std::function<void()> {
			auto made = std::make_shared<std::vector<std::pair<int, std::unique_ptr<home::HomeDraft>>>>();
			for (const int i : todo) {
				auto d = std::make_unique<home::HomeDraft>(home::DraftHome(*homes->lo, homes->homes[static_cast<std::size_t>(i)], ctx));
				if (!author.empty()) {
					d->layout.author = author;
				}
				made->emplace_back(i, std::move(d));
				++progress->done;
			}
			return [this, homes, made]() {
				if (_homes != homes) {
					return;  // rescanned meanwhile
				}
				for (auto& [i, d] : *made) {
					auto& e = _homes->entries[static_cast<std::size_t>(i)];
					if (!e.draft) {
						e.problems = d->diagnostics;
						e.draft = std::move(d);
					}
				}
				_bulkDone = true;
				_bannerClosed = false;
				SetStatus(std::format("{} homes drafted", _homes->homes.size()));
			};
		});
	}

	EditorApp::DraftEntry& EditorApp::EnsureDraft(int a_index)
	{
		auto& e = _homes->entries[static_cast<std::size_t>(a_index)];
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		if (!e.draft) {
			const home::DraftContext ctx{ _categorySet ? &*_categorySet : nullptr, &_draftRules, &_homes->itemNames };
			e.draft = std::make_unique<home::HomeDraft>(home::DraftHome(*_homes->lo, h, ctx));
			if (!_templateAuthor.empty()) {
				e.draft->layout.author = _templateAuthor;
			}
			e.problems = e.draft->diagnostics;
		}
		_state.Advance(h.cellRef, h.name, HomeStage::Drafted);  // looked at
		return e;
	}

	home::HomeDraft* EditorApp::OpenDraft()
	{
		if (!_cell) {
			return nullptr;
		}
		if (_cellDraft >= 0 && _homes) {
			auto& e = _homes->entries[static_cast<std::size_t>(_cellDraft)];
			if (!e.draft && !Busy()) {
				EnsureDraft(_cellDraft);
			}
			return e.draft.get();
		}
		// Not a home of the rail (any cell of a mod, a template file, --cell): a draft of the open cell.
		if (_localDraftCell != _cell.get() && !Busy()) {
			LoadCategories();
			auto d = std::make_unique<home::HomeDraft>();
			d->cells = { _cell->info };
			const home::DraftContext ctx{ _categorySet ? &*_categorySet : nullptr, &_draftRules, _homes ? &_homes->itemNames : nullptr };
			d->objects = home::BuildDraftObjects(*_cell->lo, d->cells, ctx.categories, &d->where);
			d->plan = core::SuggestDraft(d->objects, _draftRules, home::MakeDraftOptions(_cell->lo->Format(_cell->info.key), ctx));
			d->layout = home::LayoutFromPlan(*_cell->lo, d->cells, d->objects, d->plan);
			d->diagnostics = home::ValidateDraft(d->layout, ctx.categories);
			_localDraft = std::move(d);
			_localDraftCell = _cell.get();
		}
		return _localDraftCell == _cell.get() ? _localDraft.get() : nullptr;
	}

	void EditorApp::DraftThisHome()
	{
		_state.ui.inspectorOpen = true;
		if (_cell) {
			_panel = Panel::Draft;
			_showStart = false;
			return;
		}
		if (_homes && _railCursor >= 0) {
			_pendingPanel = Panel::Draft;
			OpenHome(_railCursor);
		}
	}

	void EditorApp::SetSuggestionOn(OpenCell& a_cell, home::HomeDraft& a_draft, std::size_t a_container, std::size_t a_suggestion, bool a_on)
	{
		auto& c = a_draft.plan.containers[a_container];
		auto& s = c.suggestions[a_suggestion];
		s.accepted = a_on;
		const auto ref = core::FormRef::Parse(a_draft.objects[c.object].id);
		if (!ref || a_cell.layout.IsMaster(*ref)) {
			return;
		}
		auto* b = a_cell.layout.FindContainer(*ref);
		if (!b) {
			if (!a_on) {
				return;
			}
			core::ContainerBinding nb;
			nb.ref = *ref;
			nb.label = Label(a_draft.objects[c.object]);
			a_cell.layout.containers.push_back(std::move(nb));
			b = &a_cell.layout.containers.back();
		}
		const auto has = std::ranges::find(b->categories, s.pattern);
		if (a_on && has == b->categories.end()) {
			b->categories.push_back(s.pattern);
		} else if (!a_on && has != b->categories.end()) {
			b->categories.erase(has);
		}
	}

	void EditorApp::AcceptAll()
	{
		auto* d = OpenDraft();
		if (!d || !_cell) {
			return;
		}
		for (std::size_t ci = 0; ci < d->plan.containers.size(); ++ci) {
			for (std::size_t si = 0; si < d->plan.containers[ci].suggestions.size(); ++si) {
				if (d->plan.containers[ci].suggestions[si].lostTo.empty() && !d->plan.containers[ci].ignored) {
					SetSuggestionOn(*_cell, *d, ci, si, true);
				}
			}
		}
		_cell->dirty = true;
		Revalidate(*_cell);
		_panel = Panel::Draft;
	}

	void EditorApp::RejectWeak()
	{
		auto* d = OpenDraft();
		if (!d || !_cell) {
			return;
		}
		for (std::size_t ci = 0; ci < d->plan.containers.size(); ++ci) {
			for (std::size_t si = 0; si < d->plan.containers[ci].suggestions.size(); ++si) {
				if (d->plan.containers[ci].suggestions[si].confidence < 0.5f) {
					SetSuggestionOn(*_cell, *d, ci, si, false);
				}
			}
		}
		_cell->dirty = true;
		Revalidate(*_cell);
		_panel = Panel::Draft;
	}

	void EditorApp::DraftAgain()
	{
		if (!_cell || Busy()) {
			return;
		}
		if (_cell->dirty && !_confirmDraftAgain) {
			_confirmDraftAgain = true;  // DrawDraftAgainPopup asks first
			return;
		}
		_confirmDraftAgain = false;
		if (_cellDraft >= 0 && _homes) {
			auto& e = _homes->entries[static_cast<std::size_t>(_cellDraft)];
			e.draft.reset();
			e.reviewed = false;
			EnsureDraft(_cellDraft);
			_cell->layout = e.draft->layout;
		} else {
			_localDraftCell = nullptr;
			if (auto* d = OpenDraft()) {
				_cell->layout = d->layout;
			}
		}
		if (!_templateAuthor.empty()) {
			_cell->layout.author = _templateAuthor;
		}
		_cell->dirty = false;
		_cell->filePath.clear();
		Revalidate(*_cell);
		_panel = Panel::Draft;
	}

	void EditorApp::DrawDraftAgainPopup()
	{
		if (_confirmDraftAgain && !ImGui::IsPopupOpen("Draft again?")) {
			ImGui::OpenPopup("Draft again?");
		}
		if (ImGui::BeginPopupModal("Draft again?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The template has changes.");
			w::Muted("Make the draft again and lose them?");
			w::Spacer();
			if (ImGui::Button("Draft again")) {
				ImGui::CloseCurrentPopup();
				DraftAgain();  // _confirmDraftAgain is set: it goes ahead
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep my changes") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmDraftAgain = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::DrawQuitPopup()
	{
		if (_confirmQuit && !ImGui::IsPopupOpen("Quit?")) {
			ImGui::OpenPopup("Quit?");
		}
		if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The template or reviewed drafts have unsaved changes.");
			w::Muted("Quit and lose them?");
			w::Spacer();
			if (ImGui::Button("Quit without saving")) {
				_quit = true;
				_confirmQuit = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep editing") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmQuit = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::RequestClose()
	{
		SyncCellToDraft();
		if (AnyUnsaved()) {
			_confirmQuit = true;
		} else {
			_quit = true;
		}
	}

	void EditorApp::OpenTemplateFile()
	{
		if (!_ws) {
			return;
		}
		if (Busy()) {
			Queue([this] { OpenTemplateFile(); });
			return;
		}
		const auto file = PickJsonFile();
		if (!file) {
			return;
		}
		core::Diagnostics diags;
		auto              layout = core::LoadLayoutFile(PathFromUtf8(*file), *file, diags);
		if (!layout || !layout->cell) {
			SetStatus("Not a Dova Stash template or layout with a cell: " + *file, true);
			return;
		}
		const Workspace* ws = _ws.get();
		auto             lo = _homes ? _homes->lo : nullptr;
		const auto       cellRef = layout->cell->ToString();
		auto             shared = std::make_shared<core::Layout>(std::move(*layout));
		const auto       path = *file;
		auto             open = [this, ws, lo, cellRef, shared, path]() mutable {
            StartJob("Opening the template's home", [this, ws, lo, cellRef, shared, path]() mutable -> std::function<void()> {
                std::vector<std::string> errors;
                if (!lo) {
                    lo = std::shared_ptr<esp::LoadOrder>(ws->OpenLoadOrder(errors));
                }
                const auto key = lo->ParseFormRef(cellRef);
                if (!key || !lo->FindCell(*key)) {
                    return [this, cellRef] { SetStatus("The template's cell " + cellRef + " is not in the active load order", true); };
                }
                return [this, lo, k = *key, shared, path]() {
                    _pendingLayout = *shared;
                    _pendingLayoutDirty = false;
                    _pendingFilePath = path;
                    _cellDraft = -1;
                    _history.clear();
                    OpenCellAsync(lo, k, false);
                };
            });
		};
		if (_cell && _cell->dirty && _cellDraft < 0) {
			_pendingOpen = open;
			return;
		}
		SyncCellToDraft();
		open();
	}

	void EditorApp::SyncCellToDraft()
	{
		if (!_cell || !_homes || _cellDraft < 0 || static_cast<std::size_t>(_cellDraft) >= _homes->entries.size()) {
			return;
		}
		auto& e = _homes->entries[static_cast<std::size_t>(_cellDraft)];
		if (!e.draft || !_cell->layout.cell || !e.draft->layout.cell || *_cell->layout.cell != *e.draft->layout.cell) {
			return;
		}
		if (core::SerializeLayout(_cell->layout) != core::SerializeLayout(e.draft->layout)) {
			e.draft->layout = _cell->layout;
			e.reviewed = true;
			e.problems = home::ValidateDraft(e.draft->layout, _categorySet ? &*_categorySet : nullptr);
			const auto& h = _homes->homes[static_cast<std::size_t>(_cellDraft)];
			_state.Advance(h.cellRef, h.name, e.problems.empty() ? HomeStage::Validated : HomeStage::Reviewed);
		}
	}

	void EditorApp::SaveDraft(int a_index)
	{
		auto&      e = EnsureDraft(a_index);
		const auto& h = _homes->homes[static_cast<std::size_t>(a_index)];
		const bool ownMod = _ws && _ws->Instance() && _state.saveTarget.empty();
		if (!_templateAuthor.empty()) {
			e.draft->layout.author = _templateAuthor;
		}
		const auto r = home::SaveTemplateFile(SaveBase(), e.draft->layout, ownMod);
		if (r.ok) {
			e.saveMessage = std::format("Saved version {} to {}{}", r.version, PathToUtf8(r.path), r.replaced ? " (the old file is kept as .bak)" : "");
			e.reviewed = false;
			_state.Advance(h.cellRef, h.name, HomeStage::Saved, PathToUtf8(r.path));
			if (_cellDraft == a_index && _cell) {
				_cell->layout = e.draft->layout;
				_cell->dirty = false;
				_cell->savedPath = PathToUtf8(r.path);
				Revalidate(*_cell);
			}
		} else {
			e.saveMessage = "Not saved: " + r.error;
		}
	}

	void EditorApp::DrawSaveDraftsPopup()
	{
		if (_confirmSaveAll && !ImGui::IsPopupOpen("Save drafts")) {
			SyncCellToDraft();
			ImGui::OpenPopup("Save drafts");
		}
		if (ImGui::BeginPopupModal("Save drafts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (!_homes || !_ws) {
				_confirmSaveAll = false;
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
			const auto      base = SaveBase();
			int             fresh = 0, existing = 0;
			std::error_code ec;
			for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
				auto& e = _homes->entries[i];
				if (e.draft && HomeStatus(_homes->homes[i]).empty()) {
					auto copy = e.draft->layout;
					copy.author = _templateAuthor;
					(fs::exists(home::TemplatePath(base, copy), ec) ? existing : fresh) += 1;
				}
			}
			ImGui::Text("Save %d drafts of homes without a template into", fresh + existing);
			w::Mono(PathToUtf8(base).c_str());
			ImGui::Text("%d new files, %d that exist already (each backed up as .bak first, its version raised).", fresh, existing);
			if (_templateAuthor.empty()) {
				wx::Text(w::Kind::Warn, "No author name: the files go under \"Unknown\" (Setup).");
			}
			w::Spacer();
			if (w::PrimaryButton("Save them", fresh + existing > 0)) {
				int saved = 0, failed = 0;
				for (std::size_t i = 0; i < _homes->homes.size(); ++i) {
					if (_homes->entries[i].draft && HomeStatus(_homes->homes[i]).empty()) {
						SaveDraft(static_cast<int>(i));
						(_homes->entries[i].saveMessage.starts_with("Saved") ? saved : failed) += 1;
					}
				}
				_homes->templates = home::LoadTemplateIndex(*_ws);
				SetStatus(std::format("Saved {} drafts{}", saved, failed ? std::format(", {} not saved", failed) : std::string{}), failed > 0);
				_confirmSaveAll = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				_confirmSaveAll = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApp::DrawDraftPanel(OpenCell& a_cell)
	{
		const auto& t = w::Th();
		auto*       d = OpenDraft();
		if (!d) {
			w::EmptyState(Busy() ? "Drafting..." : "No draft", nullptr);
			return;
		}
		const bool busy = Busy();
		ImGui::BeginDisabled(busy);
		if (ImGui::Button("Accept all")) {
			AcceptAll();
		}
		w::Tip("Every suggestion no other container has a stronger claim on");
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::Button("Reject the weak ones")) {
			RejectWeak();
		}
		w::Tip("Every suggestion under 50%");
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::Button("Draft again")) {
			DraftAgain();
		}
		w::Tip("Make the draft again from the plugins (after editing DraftRules.json)");
		ImGui::EndDisabled();

		// The templates this home already has, and how far the draft agrees.
		if (_cellDraft >= 0 && _homes && _categorySet) {
			const auto& h = _homes->homes[static_cast<std::size_t>(_cellDraft)];
			for (const auto* tp : _homes->templates.ForCell(_homes->lo->ToFormRef(h.cell))) {
				if (!tp->layout.cell || !a_cell.layout.cell || *tp->layout.cell != *a_cell.layout.cell) {
					continue;
				}
				const auto cmp = home::CompareToTemplate(a_cell.layout, tp->layout, *_categorySet);
				ImGui::PushFont(t.smallFont);
				wx::Text(tp->user ? w::Kind::Ok : w::Kind::Info,
					std::format("{} template: {} of {} categories agree, master {}", tp->user ? "Your" : "A", cmp.sameDestination, cmp.categories, cmp.sameMaster ? "the same" : "differs").c_str());
				ImGui::PopFont();
				if (ImGui::BeginItemTooltip()) {
					ImGui::TextUnformatted(tp->source.c_str());
					w::Muted(std::format("{}, version {}", tp->origin, tp->layout.version).c_str());
					for (const auto& line : cmp.details) {
						ImGui::BulletText("%s", line.c_str());
					}
					ImGui::EndTooltip();
				}
			}
		}

		auto selectObject = [&](std::size_t a_object) {
			const auto& o = d->objects[a_object];
			if (const int i = RefIndexOf(a_cell, o.id); i >= 0) {
				Select(i, true);  // the panel stays: the review goes on
			} else if (_cellDraft >= 0 && a_object < d->where.size() && d->where[a_object].first < d->cells.size()) {
				_pendingSelect = o.id;  // in another cell of the home: open it, the template comes along
				OpenCellAsync(a_cell.lo, d->cells[d->where[a_object].first].key, true, true);
			}
		};

		// Master.
		w::SectionHead("Master");
		if (d->plan.master) {
			const auto& o = d->objects[*d->plan.master];
			ImGui::PushStyleColor(ImGuiCol_Text, t.accent);
			if (ImGui::Selectable((Label(o) + "##master").c_str(), RefIndexOf(a_cell, o.id) == _selectedRef && _selectedRef >= 0)) {
				selectObject(*d->plan.master);
			}
			ImGui::PopStyleColor();
			if (ImGui::BeginItemTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
				ImGui::TextUnformatted(d->plan.masterReason.c_str());
				if (!d->plan.masterRunnersUp.empty()) {
					w::Spacer();
					w::Muted("Next best");
					for (const auto& r : d->plan.masterRunnersUp) {
						ImGui::BulletText("%s", r.c_str());
					}
				}
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			// The reason in one line: its first clause.
			auto reason = d->plan.masterReason;
			if (const auto cut = reason.find(';'); cut != std::string::npos) {
				reason.resize(cut);
			}
			ImGui::PushFont(t.smallFont);
			ImGui::PushTextWrapPos(0.0f);
			w::MutedWrapped(reason.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopFont();
			if (const auto ref = core::FormRef::Parse(o.id); ref && !a_cell.layout.IsMaster(*ref)) {
				ImGui::PushFont(t.smallFont);
				w::Muted("The template uses another master.");
				ImGui::SameLine();
				if (w::LinkButton("Use this one")) {
					if (const int i = RefIndexOf(a_cell, o.id); i >= 0) {
						MakeMaster(a_cell, i);
					}
				}
				ImGui::PopFont();
			}
		} else {
			w::MutedWrapped(d->plan.masterReason.empty() ? "No usable container." : d->plan.masterReason.c_str());
		}

		// Containers and their suggestions, accepted or not on the template being made.
		int accepted = 0;
		for (const auto& c : d->plan.containers) {
			const auto  ref = core::FormRef::Parse(d->objects[c.object].id);
			const auto* b = ref ? a_cell.layout.FindContainer(*ref) : nullptr;
			accepted += b && !b->ignored && !b->categories.empty() ? 1 : 0;
		}
		w::SectionHead(std::format("Containers  {} of {}", accepted, d->plan.containers.size()).c_str());
		ImGui::PushFont(t.smallFont);
		wx::Text(w::Kind::Ok, "name");
		ImGui::SameLine(0.0f, t.gap);
		wx::Text(w::Kind::Warn, "nearby");
		ImGui::SameLine(0.0f, t.gap);
		wx::Text(w::Kind::Error, "hint");
		ImGui::SameLine(0.0f, t.gap);
		w::Muted("· click a chip to accept or reject");
		ImGui::PopFont();
		w::Spacer(2.0f);

		std::vector<std::size_t> order(d->plan.containers.size());
		for (std::size_t k = 0; k < order.size(); ++k) {
			order[k] = k;
		}
		auto rank = [&](std::size_t k) {
			const auto& c = d->plan.containers[k];
			if (d->plan.master && *d->plan.master == c.object) {
				return 0;
			}
			if (c.ignored) {
				return 3;
			}
			return c.suggestions.empty() ? 2 : 1;
		};
		std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
		bool changed = false;
		for (const auto ci : order) {
			auto&       c = d->plan.containers[ci];
			const auto& o = d->objects[c.object];
			const auto  ref = core::FormRef::Parse(o.id);
			const bool  master = d->plan.master && *d->plan.master == c.object;
			const int   index = RefIndexOf(a_cell, o.id);
			ImGui::PushID(static_cast<int>(ci));
			if (c.ignored) {
				ImGui::PushStyleColor(ImGuiCol_Text, t.muted);
			}
			const std::string where = index < 0 && o.cell != a_cell.cellText ? "  (other cell)" : "";
			if (ImGui::Selectable((Label(o) + where).c_str(), index >= 0 && IsSelected(index))) {
				selectObject(c.object);
			}
			if (c.ignored) {
				ImGui::PopStyleColor();
			}
			w::Tip((o.id + "\n" + o.editorId).c_str());
			ImGui::Indent(t.pad);
			const auto* b = ref ? a_cell.layout.FindContainer(*ref) : nullptr;
			if (master) {
				w::Chip("master", w::Kind::Accent);
			} else if (c.ignored) {
				ImGui::PushFont(t.smallFont);
				w::Muted(("ignored: " + c.note).c_str());
				ImGui::PopFont();
			} else if (c.suggestions.empty()) {
				ImGui::PushFont(t.smallFont);
				w::Muted("nothing: its things go to the master");
				ImGui::PopFont();
			}
			for (std::size_t si = 0; si < c.suggestions.size() && !c.ignored && !master; ++si) {
				auto&      s = c.suggestions[si];
				const bool on = b && std::ranges::find(b->categories, s.pattern) != b->categories.end();
				const auto text = std::format("{} {:.0f}%", s.pattern, s.confidence * 100.0f);
				ImGui::PushID(static_cast<int>(si));
				if (si > 0 && ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(text.c_str()).x + 30.0f * t.scale) {
					ImGui::SameLine(0.0f, 4.0f);
				}
				if (w::ChipButton(text.c_str(), wx::ConfidenceKind(s.confidence), on) && !busy) {
					SetSuggestionOn(a_cell, *d, ci, si, !on);
					changed = true;
				}
				std::string tip = std::format("{} ({}, {:.0f}%)\n\n{}", on ? "Accepted" : "Not accepted", core::DraftSourceName(s.source), s.confidence * 100.0f, s.reason);
				if (!s.lostTo.empty()) {
					tip += "\n\nAnother container has a stronger claim; accepting it here too is allowed (the first one listed wins in game).";
				}
				if (ImGui::BeginItemTooltip()) {
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
					ImGui::TextUnformatted(tip.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
			ImGui::Unindent(t.pad);
			w::Spacer(2.0f * t.scale);
			ImGui::PopID();
		}
		if (changed) {
			a_cell.dirty = true;
			Revalidate(a_cell);
		}

		// The displays the draft uses.
		if (ImGui::CollapsingHeader(std::format("Displays {}###draftdisplays", d->layout.displays.size()).c_str())) {
			for (const auto& disp : d->layout.displays) {
				std::string what = disp.kind ? std::string(core::DisplayKindName(*disp.kind)) : std::string("?");
				if (!disp.categories.empty()) {
					what += " for " + disp.categories.front();
				}
				if (!disp.items.empty()) {
					what += std::format(", {} items from its script", disp.items.size());
				}
				ImGui::BulletText("%s: %s", disp.label.c_str(), what.c_str());
			}
			if (d->layout.displays.empty()) {
				w::Muted("No racks, mannequins, plaques or cases.");
			}
		}
	}
}
