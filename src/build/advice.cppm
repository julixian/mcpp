// mcpp.build.advice — what a build edge has to say when it succeeds.
//
// A BUILD PROGRAM HAS A CHANNEL FOR "SUCCESS, WITH SOMETHING TO SAY" (its
// `tag`); A NINJA EDGE HAD NONE. mcpp shows an edge's output only when the
// build fails or under `-v`, so a placement edge that found a difference worth
// stating -- `place-dlls` comparing a declared DLL with a search directory's
// copy, or choosing between two directories that offer one name -- said it to
// nobody on an ordinary build (review 2026-09-28 §2.4). The ad hoc answer was a
// second statement of the same fact at planning time, which could not see a
// directory a `prepare` action fills during the build.
//
// The channel (the 2026-09-28 design, WS3): an edge writes its advisories to
// `<build dir>/.mcpp-advice/<its stamp, flattened>.advice`, one per line as
// `note<TAB>text` or `warning<TAB>text`. After a successful build, mcpp reports
// the advisories of the edges that ran this time through mcpp.diag -- once per
// fact per process -- and deletes the files, so an edge that did not run says
// nothing again. ONE function reads them, and both the full build path and the
// fast path (`run_ninja_fast`) call it: two paths that report one thing in two
// places is the shape execute.cppm already warns about.

export module mcpp.build.advice;

import std;
import mcpp.diag;

export namespace mcpp::build::advice {

inline constexpr std::string_view kDir = ".mcpp-advice";

enum class Kind { Note, Warning };
struct Line {
    Kind        kind = Kind::Note;
    std::string text;   // one line; a newline in it is replaced by a space
};

// Write the advisories of the edge whose declared output is `stamp` (as ninja
// names it, relative to the build directory, which is the edge's working
// directory). An empty list removes a previous run's file.
void write(const std::filesystem::path& stamp, std::span<const Line> lines);

// Report every advisory under `buildDir` once through mcpp.diag, then delete
// the files. Returns how many were reported.
std::size_t report_and_clear(const std::filesystem::path& buildDir);

} // namespace mcpp::build::advice

// ── implementation ──────────────────────────────────────────────────────────

namespace mcpp::build::advice {

namespace {

std::filesystem::path file_for(const std::filesystem::path& stamp) {
    std::string name = stamp.generic_string();
    for (auto& c : name)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    return std::filesystem::path(std::string(kDir)) / (name + ".advice");
}

} // namespace

void write(const std::filesystem::path& stamp, std::span<const Line> lines) {
    const auto path = file_for(stamp);
    std::error_code ec;
    if (lines.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    for (auto const& l : lines) {
        std::string text = l.text;
        std::ranges::replace(text, '\n', ' ');
        out << (l.kind == Kind::Warning ? "warning" : "note") << '\t' << text << '\n';
    }
}

std::size_t report_and_clear(const std::filesystem::path& buildDir) {
    const auto dir = buildDir / std::string(kDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    std::vector<std::filesystem::path> files;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".advice")
            files.push_back(e.path());
    std::ranges::sort(files);
    std::size_t reported = 0;
    for (auto const& f : files) {
        std::ifstream in(f);
        for (std::string line; std::getline(in, line);) {
            if (line.empty()) continue;
            const auto tab = line.find('\t');
            const auto kind = tab == std::string::npos ? std::string_view("note")
                                                       : std::string_view(line).substr(0, tab);
            const auto text = tab == std::string::npos ? line : line.substr(tab + 1);
            if (kind == "warning") mcpp::diag::warning("build/edge-advice", text);
            else                   mcpp::diag::note("build/edge-advice", text);
            ++reported;
        }
        in.close();
        std::filesystem::remove(f, ec);
    }
    return reported;
}

} // namespace mcpp::build::advice
