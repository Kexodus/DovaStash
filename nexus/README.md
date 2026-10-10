# Nexus Mods page

Copy for the Dova Stash Nexus Mods page, and the script that turns it into text you can paste.

| File | What it is |
|---|---|
| `description.bbcode` | The long description (Nexus BBCode) |
| `summary.txt` | The "brief overview" field (309 characters; the limit is 350) |
| `file_description.txt` | Description of the main file (Files tab) |
| `changelog.txt` | Changelog for the Files tab / Logs tab |
| `sticky_post.bbcode` | The pinned bug-report post (Posts tab) |
| `nexus_fields.md` | Every upload-form field: category, tags, requirements, permissions, files, image order |
| `fill_urls.py` | Fills the `{IMG}` and `{SOURCE_URL}` placeholders and writes `out/` |
| `art/` | Page art (`art/out/*.png`), generated separately |
| `out/` | Generated, ready-to-paste copies. Don't edit them; edit the sources and run the script again |

## Why placeholders

Nexus BBCode `[img]` needs a **public** URL, and the GitHub repository is private. The sources
therefore say `{IMG}/banner_1920x480.png` and `{SOURCE_URL}`, and `fill_urls.py` swaps in the real
URLs once the images are hosted somewhere public.

## Workflow

1. **Generate the art.** Check that `art/out/` contains every file the page uses:

   ```
   python -I nexus/fill_urls.py --list-images
   ```

   Any file marked `(missing locally)` still needs to be generated.

2. **Host the embedded images** (banner, divider, features grid, `gallery_how_it_works`, and the 12
   section headers) somewhere public. Choose one:
   - **A public folder with stable file names** (recommended), for example a small public GitHub repo
     such as `raw.githubusercontent.com/<you>/dovastash-media/main/` or GitHub Pages. You only need
     `--base-url`.
   - **Nexus or imgur.** Both rename uploads, so create `nexus/image_urls.json` mapping each file
     name to its URL (right-click the uploaded image → copy image address):

     ```json
     { "banner_1920x480.png": "https://staticdelivery.nexusmods.com/mods/1704/images/.../....png" }
     ```

     Entries in `--map` override `--base-url`, so you can mix the two.

   Don't upload the section headers to the Nexus *gallery*, because they clutter it. See
   `nexus_fields.md` §6 for which images go in the gallery.

3. **Decide the source URL.** GPL-3.0 means downloaders must be able to get the source. Make the
   repository public, or upload a source archive (see `nexus_fields.md` §4) and use its link.

4. **Run the script:**

   ```
   python -I nexus/fill_urls.py --base-url https://<public-prefix> --source-url https://github.com/<you>/DovaStash
   # or
   python -I nexus/fill_urls.py --map nexus/image_urls.json --source-url <url>
   ```

   It writes `nexus/out/`. It also checks the summary length and that BBCode tags are balanced, and it
   refuses to write output while any placeholder is unresolved. `--allow-placeholders` overrides
   that check, for drafts only.

5. **Paste.** Fill in the Nexus form from `nexus_fields.md`, using the `out/` copies: summary,
   description, file description, changelog, permissions text. Post `out/sticky_post.bbcode` as the
   first comment and pin it.

6. **Preview.** The Nexus editor has a preview. Check that every image loads (a broken image means a
   URL that isn't public) and that the spoilers open.

## Keeping the copy honest

Every claim in the description is taken from `docs/DESIGN.md`, `docs/TESTING.md`,
`src/plugin/DovaStash.ini.default` and the shipped data. When features change, update the
**Features**, **Configuration**, **FAQ** and **Changelog** sections together, and keep
`summary.txt` under 350 characters. The script checks that limit.
