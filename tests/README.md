# Regression tests

## Native component tests (Windows)

The existing MVTF runner loads a **Debug** component DLL and discovers tests
embedded in the source translation units. From a Visual Studio 2022 developer
command prompt, with the Windows SDK, ATL, and CMake installed:

```bat
msbuild /r /m /p:Configuration=Debug /p:Platform=x64 build\foo_openlyrics.sln
build\x64\Debug\test_foo_openlyrics.exe build\x64\Debug\foo_openlyrics.dll
msbuild /r /m /p:Configuration=Release /p:Platform=x64 build\foo_openlyrics.sln
```

For x86, use `/p:Platform=x86` and `build\Debug\` instead of
`build\x64\Debug\`. The existing GitHub Actions workflow runs both architectures,
including release builds.

### Source coverage

Every registered source has offline fetch/parse tests in its own `.cpp` file:

| Source | Success-path coverage | Failure/edge coverage |
| --- | --- | --- |
| AZLyrics | URL, metadata, HTML entities, line breaks, returned lyrics | Changed layout, transport failure, HTTP errors, cancellation |
| Bandcamp | First-page lyrics retained when the next page returns 404 | Missing markup, transport failure, HTTP errors, cancellation |
| DarkLyrics | Matching album track only; stops before the next song | Changed layout, transport failure, HTTP errors, cancellation |
| Genius | API search metadata followed by lyric lookup | Malformed search/lookup, transport failure, HTTP errors, cancellation |
| Letras | Page-reported metadata, HTML entities, line breaks | Missing required elements, transport failure, HTTP errors, cancellation |
| Local files | Search `.lrc`/`.txt`, metadata, lookup bytes | Missing files, empty search path, UTF-16 embedded NULs |
| LRCLIB | Search and duration-specific get; synced and plain variants | Malformed/empty tracks, result limit, transport failure, HTTP errors, cancellation |
| LyricFind | Embedded JSON, metadata, duration field, returned lyrics | Missing page schema, transport failure, HTTP errors, cancellation |
| Lyricsify | LRC text and synced classification | Missing lyric element, transport failure, HTTP errors, cancellation |
| Metadata tags | Multiple fields/values, synced classification, UTF-8 | Missing metadata, empty fields; invalid evaluated save-tag names |
| Metal Archives | Search HTML fragments/IDs and HTTP/2 lyric lookup | Malformed search, unavailable lyrics, transport failure, HTTP errors, cancellation |
| Musixmatch | Search produces synced/plain variants; both lookup methods | Malformed search/lookup, transport failure, HTTP errors, cancellation |
| NetEase | POST search, metadata and LRC lookup | Malformed search/lookup, transport failure, HTTP errors, cancellation |
| QQ Music | Search metadata and base64-decoded LRC lookup | Malformed search/lookup, transport failure, HTTP errors, cancellation |
| SongLyrics | Non-placeholder lyrics, HTML entities, line breaks | Missing lyric element, transport failure, HTTP errors, cancellation |

`source_io::FixtureIO` is a scoped, thread-local fixture transport. The source's
actual URL-building, fetch, and parsing functions run against queued responses
or in-memory files. SDK headers and `http_request`/`file` interfaces are retained;
only the external I/O boundary is replaced. The HTTP/2 path uses the same fixture
queue. Test networking cannot fall through to the real SDK or libcurl.

Fixtures are minimal synthetic examples of the response shapes the component
expects, not downloaded copyrighted lyrics. No real tokens or host processes are
needed. This suite checks deterministic regressions, **not** current third-party
website availability, authentication, rate limiting, or whether their markup
has changed since the fixtures were written. Those require separate opt-in live
integration testing. File tests exercise the SDK stream-reading path against
memory-backed files, not real disk permissions or filesystem drivers.

## Portable Unicode tests

These tests use the production `unicode_text_style.h`, without the foobar2000
SDK or Win32. The expected alphabets were independently obtained from Unicode
character names (including Letterlike Symbols exceptions).

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined tests/test_unicode_text_style.cpp \
    -o /tmp/test_unicode_text_style
/tmp/test_unicode_text_style
```

This checks every ASCII letter and digit in all 13 styles (806 mappings), exact
UTF-16 output, unsupported characters/emoji, empty strings, invalid styles, and
repeated formatting. GitHub Actions runs it on Linux as well as the native
Windows component jobs.

## Host/UI acceptance checks

The standalone MVTF runner does not host foobar2000's title-format compiler or
create its dialogs. Verify these integration points in foobar2000:

1. Set both metadata save fields to:
   ```text
   $if($stricmp($ext(%path%),mp3),UNSYNCED LYRICS,$if($stricmp($ext(%path%),m4a),LYRICS,UNSYNCEDLYRICS))
   ```
   Save synced and unsynced lyrics for MP3, M4A, FLAC, Ogg, and Opus files. Check
   the actual field written and that the search-tag list includes those fields.
   Repeat while a *different* track is playing; evaluation must use the saved
   track. Test the unchanged literal defaults, invalid syntax, and an empty
   `$if(0,LYRICS,)` result (neither invalid case should write metadata).
2. Open bulk search in light and dark mode. Toggle the theme while it is open;
   check title bar, dialog, list body/header/selection/scrollbars, status text,
   progress bar, and Cancel/Close. Add more tracks while results are arriving and
   after completion. Columns and status updates must still work.
3. Select a substring in the editor and click **Format selection...**. Check all
   13 menu items, surrounding-text preservation, emoji/non-Latin text, selection
   after replacement, Ctrl+Z, Apply, and saved/reloaded text. Cancel the popup and
   try an empty selection. Avoid styling LRC timestamps: styled digits are not
   ASCII timestamp syntax. Unicode glyph availability depends on installed fonts.

## Validation in the implementation sandbox

The portable executable was built with GCC and AddressSanitizer/UBSan and passed.
MSBuild is not installed in the Linux implementation sandbox; Windows compilation
and native MVTF execution are verified by the x86/x64 GitHub Actions jobs instead.
Title-format host integration and visual acceptance checks still require manual
validation in foobar2000.

The portable CI job also tests version-header generation with mocked Git output:
release tags, commits after a release tag, an untagged fork, invalid tags, and a
missing HEAD. Untagged builds use `0.0-alpha-g<commit>`; official v-prefixed tags
retain their existing version strings.


## Downloadable Actions artifacts

`Run tests` builds and packages artifacts for pushes to `main` and `arena/**`,
pull requests, manual runs (**Run workflow**, after the workflow is present on
the default branch), and calls from the existing tagged-release workflow.
The packaging job runs only after both Windows jobs and the portable tests pass;
it uses the tested Release outputs without rebuilding or publishing a release.

Open the completed workflow run and scroll to **Artifacts**:

- **foo_openlyrics-component**: extract the downloaded Actions ZIP to obtain
  `foo_openlyrics-<commit>.fb2k-component`, then install that file in foobar2000.
  It contains the x86 DLL at the root and the x64 DLL under `x64/`.
- **foo_openlyrics-debug-symbols**: an archive containing matching Release DLLs
  and PDBs for both architectures, separated into `x86/` and `x64/`.
- **foo_openlyrics-release-x86** / **foo_openlyrics-release-x64**: the individual
  Release DLL/PDB outputs (retained for 7 days).

The installable component and combined symbols are retained for 14 days. GitHub
requires sign-in to download workflow artifacts. Existing tagged GitHub Releases
are unchanged. Every package filename includes the first 12 characters of the
built commit hash (the merge commit for `pull_request` runs).

Packaging can be tested without Windows or compiled binaries:

```sh
python -m unittest discover -s tests -p test_package_component.py -v
```

These tests verify archive layout and bytes, matching DLL/PDB placement, invalid
revision rejection, and fail-fast handling of missing or empty build outputs.
