// mcpp.build.progress — what a build reports while it runs, and how
// (.agents/docs/2026-09-29-build-progress-display-design.md).
//
// Four sources feed one model, and mcpp.ui draws it:
//
//   - mcpp's own steps (build programs, the phases), reported by the code that
//     performs them;
//   - ninja's status lines, one per finished step (`NINJA_STATUS`), which give
//     the counts and pace the reading;
//   - ninja's log, which states which step finished, when it started and when
//     it ended;
//   - the start file, where the engine's action wrapper writes the start of a
//     `check` or `prepare` action, the only steps whose start is observable.
//
// The step record, written beside build.ninja, names the package each step
// belongs to. A package is complete when every step the record assigns to it
// has finished in this build (§3.2): the rule can state completion late,
// never early.
//
// LOCKS. The model has one mutex. mcpp.ui asks the model for its frame while
// holding its own lock, so the model never calls mcpp.ui while holding the
// model's: every line the model writes is collected under the lock and written
// after it is released.

module;
#include <cstdio>

export module mcpp.build.progress;

import std;
import mcpp.ui;
import mcpp.log;
import mcpp.platform;

export namespace mcpp::build::progress {

// ─── The step record (design §6.3) ───────────────────────────────────────

struct PackageInfo {
    std::string name;              // qualified name: what the emitter records
    bool        requested   = false;
    std::string subject;           // as the package's line shows it
    std::size_t cachedUnits = 0;   // units staged from the global cache
    std::size_t steps       = 0;   // steps the graph assigns to it
};

struct Record {
    std::vector<PackageInfo> packages;
    // An output path, normalised, to its step's package in `packages`.
    std::unordered_map<std::string, std::size_t> owner;
    // An output path, normalised, to its step: the outputs of one step share
    // it, so a step whose log entries are read in two pieces counts once.
    std::unordered_map<std::string, std::size_t> step;
    // The first output of a check or prepare action, normalised, to its label.
    std::unordered_map<std::string, std::string> actions;
    std::size_t steps = 0;         // every step of the graph
};

inline constexpr std::string_view kRecordFile = "steps.tsv";

// A path as the record and the readers compare it: forward slashes, no `.`
// segments. ninja's log writes paths as build.ninja spelled them; a stamp the
// action wrapper reports was spelled by the same plan.
std::string normalise(std::string_view path);

std::string format_record(const Record& record);
Record parse_record(std::string_view text);
void write_record(const std::filesystem::path& buildDir, const Record& record);
std::optional<Record> read_record(const std::filesystem::path& buildDir);

// What the emitter states about each step as it writes it: the package whose
// unit, link, action or staged file the statement is for. `owner("")` marks
// the statements that follow as the build's own.
class Attribution {
public:
    struct Step {
        std::vector<std::string> outputs;   // normalised
        std::string              rule;
        std::string              owner;
    };
    void owner(std::string_view package);
    // Text appended to build.ninja: each `build` statement in it is recorded
    // with the current owner. Phony statements are not steps.
    void statement(std::string_view text);
    // A check or prepare action's first output and label.
    void action(std::string_view firstOutput, std::string_view label);
    const std::vector<Step>& steps() const { return steps_; }
    // The record: `declared` states how the packages are shown; an owner that
    // no entry declares is shown by its qualified name, as a dependency.
    Record record(const std::vector<PackageInfo>& declared) const;

private:
    std::string owner_;
    std::vector<Step> steps_;
    std::vector<std::pair<std::string, std::string>> actions_;
};

// ─── The readers (design §6) ─────────────────────────────────────────────

// Set as NINJA_STATUS: ninja prints it before the description of each step
// it finishes, with the steps finished, the steps planned and the step's end
// time in seconds since ninja started.
//
// IT BEGINS WITH AN ESCAPE SEQUENCE ON PURPOSE. A step's command inherits
// ninja's environment, so a ninja it runs (a `prepare` action's CMake or
// vcpkg build) prints the same marker, and the outer ninja relays that output
// after the step. ninja prints its own status line as it is, and strips
// escape sequences from a command's output when its standard output is not a
// terminal, as it is not here: only the outer ninja's lines keep the leading
// `ESC [ 0 m`. The outer ninja runs with CLICOLOR_FORCE=0, which keeps that
// stripping on.
inline constexpr std::string_view kStatusFormat = "\x1b[0m@@mcpp %f %t %e@@ ";

struct StatusLine {
    std::size_t      finished = 0;
    std::size_t      total    = 0;
    long long        endMs    = 0;
    std::string_view text;     // the description, or the command under -v
};
std::optional<StatusLine> parse_status(std::string_view line);

// One step of ninja's log: its entries share start, end and command hash.
struct LogStep {
    long long                start = 0;   // ms since that ninja started
    long long                end   = 0;
    std::vector<std::string> outputs;     // normalised
};
// The complete entries of `text`, which starts at the beginning of a line,
// grouped by step. `consumed` is the length of the complete lines read.
// Header and comment lines are skipped; `*version` receives the format a
// header states.
std::vector<LogStep> parse_log(std::string_view text, std::size_t* consumed,
                               int* version = nullptr,
                               std::vector<std::size_t>* offsets = nullptr);
// The index of the first step of this run in `steps`: the suffix whose end
// times are among `ends`, the end times of the status lines seen (§6.2).
std::size_t run_boundary(const std::vector<LogStep>& steps, std::vector<long long> ends);

// The start file of `mcpp __action` (design §6.4).
inline constexpr std::string_view kStartsEnv  = "MCPP_ACTION_STARTS";
inline constexpr std::string_view kStartsFile = ".mcpp-action-starts";
struct ActionStart {
    std::string stamp;      // normalised
    long long   unixMs = 0;
};
std::vector<ActionStart> parse_starts(std::string_view text, std::size_t* consumed);
// Called by the wrapper before it runs the command: appends the start of the
// action whose first stamp is `stamp` to the file MCPP_ACTION_STARTS names,
// in one write. Nothing when the variable is unset.
void record_action_start(std::string_view stamp);

// ─── The model (design §4) ───────────────────────────────────────────────

enum class ProgramOutcome { Ran, Cached, Failed };

// Opens the report of this command: the region, its frame and its poll.
// `verbose` lists every package (design §4.3). Idempotent.
void open(bool verbose);
// Closes it: the region is erased. Idempotent.
void close();
// The number of configurations the command builds: with more than one, a
// package line names its configuration.
void configurations(std::size_t n);

// Build programs (design §4.2). `requested` programs are listed, the others
// folded into one line when `programs_done` is called.
void program_scheduled(std::string_view package, bool requested);
void program_compiling(std::string_view package, bool requested);
void program_running(std::string_view package, bool requested);
void program_finished(std::string_view package, bool requested, ProgramOutcome outcome,
                      std::chrono::milliseconds compile, std::chrono::milliseconds run);
void programs_done();

// The validations after ninja.
void checking();

// `Finished` (design §4.5): the whole command's time, and how it was spent.
void finished(std::string_view profile, std::string_view descriptor);
// A command that builds several configurations writes one `Finished`, after
// all of them: `finished` then only records what it was given, and
// `finish_deferred` writes it.
void defer_finished();
void finish_deferred();

// One build directory's ninja runs within this command.
class Build {
public:
    // Opaque: its definition is this module's own.
    struct Impl;

    explicit Build(const std::filesystem::path& dir);
    ~Build();
    Build(const Build&) = delete;
    Build& operator=(const Build&) = delete;

    // How the plan names its packages (the full path; the fast path reads
    // the record instead).
    void declare(std::vector<PackageInfo> packages);
    const std::vector<PackageInfo>& declared() const;
    void set_record(Record record);
    bool has_record() const;

    // The environment ninja runs with: NINJA_STATUS and the start file.
    std::vector<std::pair<std::string, std::string>> environment() const;
    // One ninja invocation.
    void pass_begin();
    void status(const StatusLine& line);
    // A `FAILED: <outputs>` line: the step's package is failed. Returns true
    // for the command's first failure, after which the caller writes
    // `error: build failed` and the step's diagnostics.
    bool failed(std::string_view outputs);
    void pass_end();
    // The build ended: every package still open gets its final line.
    void finish(bool success);

private:
    std::shared_ptr<Impl> impl_;
};

} // namespace mcpp::build::progress

namespace mcpp::build::progress {

using Clock = std::chrono::steady_clock;
using ms    = std::chrono::milliseconds;

// ─── The step record ─────────────────────────────────────────────────────

std::string normalise(std::string_view path) {
    std::string s(path);
    std::ranges::replace(s, '\\', '/');
    return std::filesystem::path(s).lexically_normal().generic_string();
}

namespace {

// Tabs and line ends cannot appear in a field.
std::string field(std::string_view s) {
    std::string out(s);
    std::ranges::replace(out, '\t', ' ');
    std::ranges::replace(out, '\n', ' ');
    std::ranges::replace(out, '\r', ' ');
    return out;
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> out;
    while (true) {
        auto t = line.find('\t');
        out.push_back(line.substr(0, t));
        if (t == std::string_view::npos) break;
        line.remove_prefix(t + 1);
    }
    return out;
}

std::size_t to_size(std::string_view s) {
    std::size_t v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

long long to_ll(std::string_view s) {
    long long v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

} // namespace

// Format: a header, then `P` lines (packages, in order), `A` lines (actions)
// and `O` lines (an output and its package's index).
std::string format_record(const Record& r) {
    std::string out = std::format("# mcpp steps v1\t{}\n", r.steps);
    for (auto const& p : r.packages)
        out += std::format("P\t{}\t{}\t{}\t{}\t{}\n", field(p.name), p.requested ? 1 : 0,
                           p.cachedUnits, p.steps, field(p.subject));
    std::vector<std::pair<std::string, std::string>> actions(r.actions.begin(), r.actions.end());
    std::ranges::sort(actions);
    for (auto const& [out1, label] : actions)
        out += std::format("A\t{}\t{}\n", field(out1), field(label));
    std::vector<std::pair<std::string, std::size_t>> owners(r.owner.begin(), r.owner.end());
    std::ranges::sort(owners);
    for (auto const& [path, index] : owners) {
        auto st = r.step.find(path);
        out += std::format("O\t{}\t{}\t{}\n", index,
                           st == r.step.end() ? std::size_t{0} : st->second, field(path));
    }
    return out;
}

Record parse_record(std::string_view text) {
    Record r;
    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        auto f = split_tabs(line);
        if (f[0] == "# mcpp steps v1" && f.size() >= 2) { r.steps = to_size(f[1]); continue; }
        if (f[0] == "P" && f.size() >= 6) {
            r.packages.push_back({std::string(f[1]), f[2] == "1", std::string(f[5]),
                                  to_size(f[3]), to_size(f[4])});
        } else if (f[0] == "A" && f.size() >= 3) {
            r.actions.emplace(std::string(f[1]), std::string(f[2]));
        } else if (f[0] == "O" && f.size() >= 4) {
            const auto index = to_size(f[1]);
            if (index < r.packages.size()) {
                r.owner.emplace(std::string(f[3]), index);
                r.step.emplace(std::string(f[3]), to_size(f[2]));
            }
        }
    }
    return r;
}

void write_record(const std::filesystem::path& buildDir, const Record& record) {
    std::ofstream f(buildDir / kRecordFile, std::ios::binary | std::ios::trunc);
    f << format_record(record);
}

std::optional<Record> read_record(const std::filesystem::path& buildDir) {
    std::ifstream f(buildDir / kRecordFile, std::ios::binary);
    if (!f) return std::nullopt;
    std::string text{std::istreambuf_iterator<char>(f), {}};
    return parse_record(text);
}

// ─── Attribution ─────────────────────────────────────────────────────────

void Attribution::owner(std::string_view package) { owner_ = std::string(package); }

namespace {

// The outputs and the rule of a `build` line: tokens up to the first colon
// ninja does not read as escaped, `|` dropped, `$ ` `$:` `$$` unescaped.
std::optional<std::pair<std::vector<std::string>, std::string>>
parse_build_line(std::string_view line) {
    if (!line.starts_with("build ")) return std::nullopt;
    line.remove_prefix(6);
    std::vector<std::string> outputs;
    std::string cur;
    std::size_t i = 0;
    bool done = false;
    auto flush = [&] {
        if (!cur.empty() && cur != "|") outputs.push_back(normalise(cur));
        cur.clear();
    };
    for (; i < line.size() && !done; ++i) {
        const char c = line[i];
        if (c == '$' && i + 1 < line.size()) {
            const char n = line[i + 1];
            if (n == ' ' || n == ':' || n == '$') { cur += n; ++i; continue; }
            cur += c;
            continue;
        }
        if (c == ':') { flush(); done = true; break; }
        if (c == ' ') { flush(); continue; }
        cur += c;
    }
    if (!done) return std::nullopt;
    std::string_view rest = line.substr(i + 1);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    auto rule = rest.substr(0, rest.find_first_of(" \n"));
    return std::pair{std::move(outputs), std::string(rule)};
}

} // namespace

void Attribution::statement(std::string_view text) {
    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        auto parsed = parse_build_line(line);
        if (!parsed || parsed->second == "phony" || parsed->first.empty()) continue;
        steps_.push_back({std::move(parsed->first), std::move(parsed->second), owner_});
    }
}

void Attribution::action(std::string_view firstOutput, std::string_view label) {
    actions_.emplace_back(normalise(firstOutput), std::string(label));
}

Record Attribution::record(const std::vector<PackageInfo>& declared) const {
    Record r;
    std::unordered_map<std::string, std::size_t> index;
    for (auto const& d : declared) {
        if (index.contains(d.name)) continue;
        index.emplace(d.name, r.packages.size());
        auto p = d;
        p.steps = 0;
        r.packages.push_back(std::move(p));
    }
    for (auto const& s : steps_) {
        const auto id = r.steps++;
        if (s.owner.empty()) continue;
        auto it = index.find(s.owner);
        if (it == index.end()) {
            it = index.emplace(s.owner, r.packages.size()).first;
            r.packages.push_back({s.owner, false, s.owner, 0, 0});
        }
        ++r.packages[it->second].steps;
        for (auto const& o : s.outputs) {
            r.owner.emplace(o, it->second);
            r.step.emplace(o, id);
        }
    }
    for (auto const& [out1, label] : actions_) r.actions.emplace(out1, label);
    return r;
}

// ─── The readers ─────────────────────────────────────────────────────────

std::optional<StatusLine> parse_status(std::string_view line) {
    constexpr std::string_view head = "\x1b[0m@@mcpp ";
    if (!line.starts_with(head)) return std::nullopt;
    line.remove_prefix(head.size());
    const auto close = line.find("@@ ");
    const auto closeEnd = close == std::string_view::npos ? line.find("@@") : close;
    if (closeEnd == std::string_view::npos) return std::nullopt;
    auto nums = line.substr(0, closeEnd);
    StatusLine s;
    s.text = close == std::string_view::npos ? std::string_view{} : line.substr(close + 3);
    std::array<std::string_view, 3> part;
    for (std::size_t i = 0; i < 3; ++i) {
        auto sp = nums.find(' ');
        part[i] = nums.substr(0, sp);
        if (sp == std::string_view::npos) { if (i < 2) return std::nullopt; break; }
        nums.remove_prefix(sp + 1);
    }
    s.finished = to_size(part[0]);
    s.total    = to_size(part[1]);
    // `%e` is seconds with three decimals: the step's end in milliseconds.
    auto e = part[2];
    auto dot = e.find('.');
    long long secs = to_ll(e.substr(0, dot));
    long long frac = 0;
    if (dot != std::string_view::npos) {
        auto f = e.substr(dot + 1);
        std::string three(f.substr(0, 3));
        while (three.size() < 3) three += '0';
        frac = to_ll(three);
    }
    s.endMs = secs * 1000 + frac;
    return s;
}

std::vector<LogStep> parse_log(std::string_view text, std::size_t* consumed, int* version,
                               std::vector<std::size_t>* offsets) {
    std::vector<LogStep> steps;
    std::size_t pos = 0;
    std::string lastHash;
    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        if (nl == std::string_view::npos) break;   // an entry still being written
        auto line = text.substr(pos, nl - pos);
        const auto lineStart = pos;
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (line.front() == '#') {
            if (version) {
                constexpr std::string_view v = "# ninja log v";
                if (line.starts_with(v)) *version = static_cast<int>(to_ll(line.substr(v.size())));
            }
            continue;
        }
        auto f = split_tabs(line);
        if (f.size() < 5) continue;
        const long long start = to_ll(f[0]), end = to_ll(f[1]);
        const std::string hash(f[4]);
        if (!steps.empty() && steps.back().start == start && steps.back().end == end
            && hash == lastHash) {
            steps.back().outputs.push_back(normalise(f[3]));
        } else {
            steps.push_back({start, end, {normalise(f[3])}});
            if (offsets) offsets->push_back(lineStart);
            lastHash = hash;
        }
    }
    if (consumed) *consumed = pos;
    return steps;
}

std::size_t run_boundary(const std::vector<LogStep>& steps, std::vector<long long> ends) {
    std::size_t i = steps.size();
    while (i > 0) {
        auto it = std::ranges::find(ends, steps[i - 1].end);
        if (it == ends.end()) break;
        ends.erase(it);
        --i;
    }
    return i;
}

std::vector<ActionStart> parse_starts(std::string_view text, std::size_t* consumed) {
    std::vector<ActionStart> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        if (nl == std::string_view::npos) break;
        auto line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        auto f = split_tabs(line);
        if (f.size() < 2 || f[0].empty()) continue;
        out.push_back({normalise(f[0]), to_ll(f[1])});
    }
    if (consumed) *consumed = pos;
    return out;
}

void record_action_start(std::string_view stamp) {
    const char* file = std::getenv(std::string(kStartsEnv).c_str());
    if (!file || !*file || stamp.empty()) return;
    const auto now = std::chrono::duration_cast<ms>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // One line, one write, in append mode: the lines of actions that start at
    // the same time do not interleave.
    (void)mcpp::platform::fs::append_atomically(
        std::filesystem::path(file), std::format("{}\t{}\n", field(stamp), now));
}

// ─── The model ───────────────────────────────────────────────────────────

// Not exported, and not TU-local either: `Build::Impl` holds them.
enum class Phase { Resolving, Programs, Building, Stopping, Checking };

struct Program {
    std::string name;
    bool requested = false;
    enum State { Waiting, Compiling, Running, Done } state = Waiting;
    ProgramOutcome outcome = ProgramOutcome::Ran;
    Clock::time_point since{};
    ms compile{0}, run{0};
};

struct PackageState {
    std::size_t finished = 0;
    long long   first    = std::numeric_limits<long long>::max();   // ms since command start
    long long   last     = 0;
    bool        failed    = false;
    bool        committed = false;
};

struct Running {
    std::size_t package = 0;
    std::string label;
    std::string stamp;
    long long   since = 0;   // ms since command start
};

struct Build::Impl {
    std::filesystem::path dir;
    std::vector<PackageInfo> declared;
    std::optional<Record> record;
    bool recordTried = false;
    bool closed = false;                       // the build's Build object is gone
    std::unordered_set<std::size_t> counted;   // steps already counted
    std::vector<PackageState> packages;
    bool depsCommitted = false;
    // Passes of ninja.
    long long   passStart = 0;       // ms since command start
    std::size_t doneBefore = 0, totalBefore = 0;   // earlier passes
    std::size_t finished = 0, total = 0;          // this pass
    bool        inPass = false;
    // The log of this pass.
    std::filesystem::path logPath;
    std::string logTail;             // the file's last bytes when the pass began
    std::uintmax_t logStart = 0;     // its size then
    std::optional<std::uintmax_t> logOffset;   // where this pass's entries are read from
    std::vector<long long> ends;     // end times of the status lines of this pass
    // The start file.
    std::uintmax_t startsOffset = 0;
    std::vector<Running> running;
    // The longest step, for `Finished`.
    long long   longest = 0;
    std::string longestLabel;
    bool        anyStep = false;
};

namespace {

// The fast path has no plan: it reads the record the plan wrote, when ninja
// reports its first step, so a build with nothing to do reads nothing.
void ensure_record(Build::Impl& b) {
    if (b.record || b.recordTried) return;
    b.recordTried = true;
    if (auto rec = read_record(b.dir)) {
        b.packages.assign(rec->packages.size(), PackageState{});
        b.record = std::move(*rec);
    }
}

struct Report {
    std::mutex m;
    bool open = false;
    bool verbose = false;
    Phase phase = Phase::Resolving;
    std::size_t configurations = 1;
    std::vector<Program> programs;
    bool programsCommitted = false;
    ms   programTime{0};
    std::vector<std::shared_ptr<Build::Impl>> builds;
    std::optional<long long> buildStart;   // ms since command start
    bool failureReported = false;
    bool deferred = false;
    std::optional<std::pair<std::string, std::string>> deferredFinish;
};

Report& report() {
    static Report r;
    return r;
}

long long now_ms() {
    return std::chrono::duration_cast<ms>(Clock::now() - mcpp::ui::command_start()).count();
}

constexpr std::size_t kColumnMax = 43;   // the state at column 56 at most

std::string plural(std::size_t n, std::string_view one, std::string_view many) {
    return std::format("{} {}", n, n == 1 ? one : many);
}

// ── Programs ──

std::size_t program_column(const Report& r) {
    std::size_t w = 0;
    for (auto const& p : r.programs)
        if (p.requested || r.verbose) w = std::max(w, mcpp::ui::display_width(p.name));
    w = std::max<std::size_t>(w, 16);   // "N dependencies"
    return std::min(w + 2, kColumnMax);
}

std::string program_state(const Program& p, bool verbose) {
    switch (p.state) {
    case Program::Waiting:   return "waiting";
    case Program::Compiling:
        return std::format("compiling {}", mcpp::ui::format_clock(
            std::chrono::duration_cast<ms>(Clock::now() - p.since)));
    case Program::Running:
        return std::format("running {}", mcpp::ui::format_clock(
            std::chrono::duration_cast<ms>(Clock::now() - p.since)));
    case Program::Done: break;
    }
    if (p.outcome == ProgramOutcome::Cached) return "cached";
    if (p.outcome == ProgramOutcome::Failed) return "failed";
    if (verbose && p.compile.count() > 0)
        return std::format("compiled {} · ran {}", mcpp::ui::format_duration(p.compile),
                           mcpp::ui::format_duration(p.run));
    return std::format("ran {}", mcpp::ui::format_duration(p.compile + p.run));
}

mcpp::ui::Tone program_tone(const Program& p) {
    if (p.state != Program::Done) return mcpp::ui::Tone::Plain;
    if (p.outcome == ProgramOutcome::Failed) return mcpp::ui::Tone::Bad;
    if (p.outcome == ProgramOutcome::Cached) return mcpp::ui::Tone::Muted;
    return mcpp::ui::Tone::Good;
}

std::string program_line(const Report& r, const Program& p) {
    return mcpp::ui::step_line("build.mcpp", p.name, program_column(r), program_state(p, r.verbose),
                               program_tone(p), /*infoVerb=*/true);
}

// The folded line of the dependencies' programs.
std::optional<std::string> folded_programs_line(const Report& r) {
    std::size_t n = 0, ran = 0, cached = 0, failed = 0;
    ms time{0};
    for (auto const& p : r.programs) {
        if (p.requested || r.verbose || p.state != Program::Done) continue;
        ++n;
        if (p.outcome == ProgramOutcome::Cached) ++cached;
        else if (p.outcome == ProgramOutcome::Failed) ++failed;
        else { ++ran; time += p.compile + p.run; }
    }
    if (n == 0) return std::nullopt;
    std::string state;
    if (ran) state = std::format("ran {}", mcpp::ui::format_duration(time));
    if (cached) state += std::format("{}{}", state.empty() ? "" : " · ",
                                     ran ? std::format("{} cached", cached) : std::string("cached"));
    if (failed) state += std::format("{}{} failed", state.empty() ? "" : " · ", failed);
    return mcpp::ui::step_line("build.mcpp", plural(n, "dependency", "dependencies"),
                               program_column(r), state,
                               failed ? mcpp::ui::Tone::Bad
                                      : ran ? mcpp::ui::Tone::Good : mcpp::ui::Tone::Muted,
                               /*infoVerb=*/true);
}

Program& program(Report& r, std::string_view name, bool requested) {
    for (auto& p : r.programs)
        if (p.name == name && p.state != Program::Done) return p;
    r.programs.push_back({std::string(name), requested});
    return r.programs.back();
}

// ── Packages ──

bool listed(const Report& r, const PackageInfo& p) { return p.requested || r.verbose; }

std::string subject_of(const Report& r, const Build::Impl& b, const PackageInfo& p) {
    if (r.configurations <= 1) return p.subject;
    return std::format("{} [{}]", p.subject, b.dir.filename().string());
}

std::size_t package_column(const Report& r, const Build::Impl& b) {
    std::size_t w = 16;   // "NN dependencies"
    if (b.record)
        for (auto const& p : b.record->packages)
            if (listed(r, p)) w = std::max(w, mcpp::ui::display_width(subject_of(r, b, p)));
    return std::min(w + 2, kColumnMax);
}

bool complete(const PackageInfo& p, const PackageState& s) {
    return p.steps > 0 && s.finished >= p.steps;
}

std::string span_of(const PackageState& s) {
    if (s.first > s.last) return {};
    return mcpp::ui::format_duration(ms(s.last - s.first));
}

std::string package_line(const Report& r, const Build::Impl& b, std::size_t i, bool final,
                         bool success) {
    const auto& p = b.record->packages[i];
    const auto& s = b.packages[i];
    std::string state;
    auto tone = mcpp::ui::Tone::Plain;
    if (s.failed) { state = "failed"; tone = mcpp::ui::Tone::Bad; }
    else if (s.finished == 0 && final) {
        state = p.cachedUnits > 0 ? std::format("cached {}", plural(p.cachedUnits, "unit", "units"))
                                  : "fresh";
        tone = mcpp::ui::Tone::Muted;
    } else if (p.cachedUnits > 0 && (complete(p, s) || (final && success))) {
        state = std::format("cached {}", plural(p.cachedUnits, "unit", "units"));
        tone = mcpp::ui::Tone::Muted;
    } else if (complete(p, s) || (final && success)) {
        state = std::format("done {}", span_of(s));
        tone = mcpp::ui::Tone::Good;
    } else {
        state = plural(s.finished, "step", "steps");
    }
    return mcpp::ui::step_line("Compiling", subject_of(r, b, p), package_column(r, b), state, tone);
}

// The folded line of the dependencies (design §4.3): those with steps in
// this build, and at the end also those the global cache supplied whole.
std::optional<std::string> dependencies_line(const Report& r, const Build::Impl& b, bool final,
                                             bool success) {
    std::size_t active = 0, cached = 0, cachedOnly = 0, steps = 0;
    long long first = std::numeric_limits<long long>::max(), last = 0;
    for (std::size_t i = 0; i < b.record->packages.size(); ++i) {
        const auto& p = b.record->packages[i];
        const auto& s = b.packages[i];
        if (listed(r, p) || s.committed) continue;
        if (p.cachedUnits > 0 && final) {
            ++cached;
            if (s.finished == 0) ++cachedOnly;
        }
        if (s.finished == 0) continue;
        ++active;
        steps += s.finished;
        first = std::min(first, s.first);
        last  = std::max(last, s.last);
    }
    const std::size_t n = active + cachedOnly;
    if (active == 0 && (!final || cached == 0)) return std::nullopt;
    std::string state;
    auto tone = mcpp::ui::Tone::Plain;
    if (final && success) {
        if (active > 0) {
            state = first <= last
                ? std::format("done {}", mcpp::ui::format_duration(ms(last - first)))
                : std::string("done");
            if (cached) state += std::format(" · {} cached", cached);
            tone = mcpp::ui::Tone::Good;
        } else {
            state = "cached";
            tone = mcpp::ui::Tone::Muted;
        }
    } else {
        state = plural(steps, "step", "steps");
    }
    return mcpp::ui::step_line("Compiling", plural(n, "dependency", "dependencies"),
                               package_column(r, b), state, tone);
}

bool dependencies_complete(const Report& r, const Build::Impl& b) {
    for (std::size_t i = 0; i < b.record->packages.size(); ++i) {
        const auto& p = b.record->packages[i];
        if (listed(r, p) || b.packages[i].committed) continue;
        if (p.steps > 0 && !complete(p, b.packages[i])) return false;
    }
    return true;
}

// Commits what became final; lines to write are appended to `out`.
void settle(Report& r, Build::Impl& b, std::vector<std::string>& out) {
    if (!b.record) return;
    for (std::size_t i = 0; i < b.record->packages.size(); ++i) {
        auto& s = b.packages[i];
        const auto& p = b.record->packages[i];
        if (s.committed || !listed(r, p) || !complete(p, s)) continue;
        s.committed = true;
        out.push_back(package_line(r, b, i, /*final=*/true, /*success=*/true));
    }
    if (!b.depsCommitted && dependencies_complete(r, b)) {
        if (auto l = dependencies_line(r, b, /*final=*/true, /*success=*/true)) {
            out.push_back(*l);
            b.depsCommitted = true;
            for (std::size_t i = 0; i < b.record->packages.size(); ++i)
                if (!listed(r, b.record->packages[i]) && b.packages[i].finished > 0)
                    b.packages[i].committed = true;
        }
    }
}

void write_lines(const std::vector<std::string>& lines) {
    for (auto const& l : lines) mcpp::ui::line(l);
    if (!lines.empty()) mcpp::ui::touch_region();
}

// Reads the log of an open pass; model lock held.
void read_log(Build::Impl& b) {
    if (!b.inPass || !b.record) return;
    std::error_code ec;
    const auto size = std::filesystem::file_size(b.logPath, ec);
    if (ec) return;
    std::ifstream f(b.logPath, std::ios::binary);
    if (!f) return;
    if (!b.logOffset) {
        // Is the file the one the pass began with? A recompaction rewrote it.
        bool same = size >= b.logStart;
        if (same && !b.logTail.empty()) {
            std::string tail(b.logTail.size(), '\0');
            f.seekg(static_cast<std::streamoff>(b.logStart - b.logTail.size()));
            f.read(tail.data(), static_cast<std::streamsize>(tail.size()));
            same = f && tail == b.logTail;
            f.clear();
        }
        if (same) b.logOffset = b.logStart;
        else {
            // This pass's entries are the suffix whose end times are those of
            // the status lines seen (design §6.2).
            f.seekg(0);
            std::string all{std::istreambuf_iterator<char>(f), {}};
            std::size_t consumed = 0;
            std::vector<std::size_t> offsets;
            auto steps = parse_log(all, &consumed, nullptr, &offsets);
            const auto first = run_boundary(steps, b.ends);
            b.logOffset = first < offsets.size() ? offsets[first] : consumed;
            f.clear();
        }
    }
    if (size <= *b.logOffset) return;
    f.seekg(static_cast<std::streamoff>(*b.logOffset));
    std::string text(static_cast<std::size_t>(size - *b.logOffset), '\0');
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(f.gcount()));
    std::size_t consumed = 0;
    auto steps = parse_log(text, &consumed);
    *b.logOffset += consumed;
    for (auto const& st : steps) {
        const long long start = b.passStart + st.start, end = b.passStart + st.end;
        std::optional<std::size_t> owner;
        bool seen = false;
        for (auto const& o : st.outputs) {
            if (auto it = b.record->owner.find(o); it != b.record->owner.end()) {
                owner = it->second;
                if (auto sid = b.record->step.find(o); sid != b.record->step.end())
                    seen = !b.counted.insert(sid->second).second;
                break;
            }
        }
        // The rest of a step whose first entries an earlier read counted.
        if (seen) continue;
        std::string label = st.outputs.front();
        if (auto it = b.record->actions.find(st.outputs.front()); it != b.record->actions.end())
            label = it->second;
        if (owner) {
            auto& s = b.packages[*owner];
            ++s.finished;
            s.first = std::min(s.first, start);
            s.last  = std::max(s.last, end);
            label = std::format("{}: {}", b.record->packages[*owner].name, label);
        }
        if (st.end - st.start > b.longest || !b.anyStep) {
            b.longest = st.end - st.start;
            b.longestLabel = label;
            b.anyStep = true;
        }
        std::erase_if(b.running, [&](const Running& a) {
            return std::ranges::find(st.outputs, a.stamp) != st.outputs.end();
        });
    }
}

// Reads the start file of an open pass; model lock held.
void read_starts(Build::Impl& b) {
    if (!b.inPass || !b.record) return;
    std::error_code ec;
    const auto path = b.dir / kStartsFile;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size <= b.startsOffset) return;
    std::ifstream f(path, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(b.startsOffset));
    std::string text(static_cast<std::size_t>(size - b.startsOffset), '\0');
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(f.gcount()));
    std::size_t consumed = 0;
    auto starts = parse_starts(text, &consumed);
    b.startsOffset += consumed;
    const auto nowUnix = std::chrono::duration_cast<ms>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (auto const& a : starts) {
        auto owner = b.record->owner.find(a.stamp);
        if (owner == b.record->owner.end()) continue;
        auto label = b.record->actions.find(a.stamp);
        b.running.push_back({owner->second,
                             label != b.record->actions.end() ? label->second : a.stamp,
                             a.stamp, now_ms() - std::max<long long>(0, nowUnix - a.unixMs)});
    }
}

// The builds whose Build object is alive: the frame, the poll and the status
// line read these. `Finished` reads every build of the command.
std::vector<std::shared_ptr<Build::Impl>> live_builds(Report& r) {
    std::vector<std::shared_ptr<Build::Impl>> out;
    for (auto const& b : r.builds)
        if (!b->closed) out.push_back(b);
    return out;
}

std::string phase_status(Report& r) {
    const auto clock = mcpp::ui::format_clock(ms(now_ms()));
    std::string current;
    long long oldest = std::numeric_limits<long long>::max();
    std::size_t done = 0, total = 0;
    for (auto const& b : live_builds(r)) {
        done  += b->doneBefore + b->finished;
        total += b->totalBefore + b->total;
        for (auto const& a : b->running)
            if (a.since < oldest) {
                oldest  = a.since;
                current = std::format("{}: {} {}", b->record->packages[a.package].name, a.label,
                                      mcpp::ui::format_clock(ms(now_ms() - a.since)));
            }
    }
    switch (r.phase) {
    case Phase::Resolving:
        return mcpp::ui::status_line("Resolving", std::format("· {}", clock));
    case Phase::Programs: {
        for (auto const& p : r.programs)
            if (p.state == Program::Compiling || p.state == Program::Running)
                current = std::format("{} {}", p.name, mcpp::ui::format_clock(
                    std::chrono::duration_cast<ms>(Clock::now() - p.since)));
        return mcpp::ui::status_line("Running build programs",
            std::format("· {}{}", clock, current.empty() ? "" : " · " + current));
    }
    case Phase::Building:
    case Phase::Stopping:
        return mcpp::ui::status_line(r.phase == Phase::Building ? "Building" : "Stopping",
            std::format("{}· {}{}", r.phase == Phase::Building && total > 0
                                        ? std::format("{}/{} ", done, total) : "",
                        clock, current.empty() ? "" : " · " + current));
    case Phase::Checking:
        return mcpp::ui::status_line("Checking", std::format("· {}", clock));
    }
    return {};
}

mcpp::ui::Frame frame() {
    auto& r = report();
    std::lock_guard lock(r.m);
    mcpp::ui::Frame f;
    for (auto const& p : r.programs)
        if (p.state != Program::Done && (p.requested || r.verbose)) f.lines.push_back(program_line(r, p));
    for (auto const& b : live_builds(r)) {
        if (!b->record) continue;
        for (std::size_t i = 0; i < b->record->packages.size(); ++i) {
            const auto& p = b->record->packages[i];
            const auto& s = b->packages[i];
            if (s.committed || s.finished == 0 || !listed(r, p)) continue;
            f.lines.push_back(package_line(r, *b, i, /*final=*/false, false));
        }
        if (!b->depsCommitted)
            if (auto l = dependencies_line(r, *b, /*final=*/false, false)) f.lines.push_back(*l);
    }
    f.status = phase_status(r);
    return f;
}

void poll() {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        for (auto const& b : live_builds(r)) {
            read_starts(*b);
            read_log(*b);
            settle(r, *b, out);
        }
    }
    write_lines(out);
}

} // namespace

void open(bool verbose) {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        if (r.open) return;
        r.open = true;
        r.verbose = verbose;
    }
    mcpp::ui::open_region(&frame, &poll);
}

void close() {
    mcpp::ui::close_region();
    auto& r = report();
    std::lock_guard lock(r.m);
    r.open = false;
}

void configurations(std::size_t n) {
    auto& r = report();
    std::lock_guard lock(r.m);
    r.configurations = std::max<std::size_t>(1, n);
}

void program_scheduled(std::string_view package, bool requested) {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        program(r, package, requested);
    }
    mcpp::ui::touch_region();
}

void program_compiling(std::string_view package, bool requested) {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        r.phase = Phase::Programs;
        auto& p = program(r, package, requested);
        p.state = Program::Compiling;
        p.since = Clock::now();
    }
    mcpp::ui::touch_region();
}

void program_running(std::string_view package, bool requested) {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        r.phase = Phase::Programs;
        auto& p = program(r, package, requested);
        p.state = Program::Running;
        p.since = Clock::now();
    }
    mcpp::ui::touch_region();
}

void program_finished(std::string_view package, bool requested, ProgramOutcome outcome,
                      std::chrono::milliseconds compile, std::chrono::milliseconds run) {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        auto& p = program(r, package, requested);
        p.state   = Program::Done;
        p.outcome = outcome;
        p.compile = compile;
        p.run     = run;
        r.programTime += compile + run;
        if (p.requested || r.verbose || outcome == ProgramOutcome::Failed)
            out.push_back(program_line(r, p));
        if (outcome == ProgramOutcome::Failed) p.requested = true;   // not folded again
    }
    write_lines(out);
    mcpp::log::info("progress", std::format("build.mcpp {} {}", package,
        outcome == ProgramOutcome::Cached ? "cached"
        : outcome == ProgramOutcome::Failed ? "failed"
        : std::format("compiled {}ms ran {}ms", compile.count(), run.count())));
}

void programs_done() {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        if (r.programsCommitted) return;
        r.programsCommitted = true;
        if (auto l = folded_programs_line(r)) out.push_back(*l);
    }
    write_lines(out);
}

void checking() {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        r.phase = Phase::Checking;
    }
    mcpp::ui::touch_region();
}

void defer_finished() {
    auto& r = report();
    std::lock_guard lock(r.m);
    r.deferred = true;
}

void finish_deferred() {
    auto& r = report();
    std::optional<std::pair<std::string, std::string>> f;
    {
        std::lock_guard lock(r.m);
        r.deferred = false;
        f = std::exchange(r.deferredFinish, std::nullopt);
    }
    if (f) finished(f->first, f->second);
}

void finished(std::string_view profile, std::string_view descriptor) {
    auto& r = report();
    std::string detail;
    const auto total = now_ms();
    {
        std::lock_guard lock(r.m);
        if (r.deferred) {
            r.deferredFinish.emplace(std::string(profile), std::string(descriptor));
            return;
        }
        // The breakdown and the longest step explain a wait; a command shorter
        // than ten seconds has none to explain (design §4.5).
        if (total >= 10'000) {
            const long long build = r.buildStart ? total - *r.buildStart : 0;
            const long long programs = r.programTime.count();
            const long long plan = std::max<long long>(0, total - build - programs);
            std::vector<std::string> parts;
            if (plan > 0)     parts.push_back("plan " + mcpp::ui::format_duration(ms(plan)));
            if (programs > 0) parts.push_back("programs " + mcpp::ui::format_duration(ms(programs)));
            if (build > 0)    parts.push_back("build " + mcpp::ui::format_duration(ms(build)));
            if (parts.size() > 1)
                for (std::size_t i = 0; i < parts.size(); ++i)
                    detail += (i ? " · " : "") + parts[i];
            long long longest = 0;
            std::string label;
            for (auto const& b : r.builds)
                if (b->anyStep && b->longest > longest) {
                    longest = b->longest;
                    label   = b->longestLabel;
                }
            if (build >= 10'000 && longest * 4 >= build && !label.empty())
                detail += std::format("{}longest {} {}", detail.empty() ? "" : " · ", label,
                                      mcpp::ui::format_duration(ms(longest)));
        }
    }
    // `Finished` ends the report: the region is erased before it, and not
    // drawn again below it.
    close();
    mcpp::ui::finished(profile, ms(total), descriptor, detail);
}

// ─── Build ───────────────────────────────────────────────────────────────

Build::Build(const std::filesystem::path& dir) : impl_(std::make_shared<Impl>()) {
    impl_->dir = dir;
    auto& r = report();
    std::lock_guard lock(r.m);
    r.builds.push_back(impl_);
}

// A build that ended without `finish` (the fast path's stale graph, which the
// full path then plans and builds) shows nothing from here on.
Build::~Build() {
    auto& r = report();
    std::lock_guard lock(r.m);
    impl_->closed = true;
}

void Build::declare(std::vector<PackageInfo> packages) {
    auto& r = report();
    std::lock_guard lock(r.m);
    impl_->declared = std::move(packages);
}

const std::vector<PackageInfo>& Build::declared() const { return impl_->declared; }

void Build::set_record(Record record) {
    auto& r = report();
    std::lock_guard lock(r.m);
    impl_->packages.assign(record.packages.size(), PackageState{});
    impl_->record = std::move(record);
    impl_->recordTried = true;
}

bool Build::has_record() const {
    auto& r = report();
    std::lock_guard lock(r.m);
    return impl_->record.has_value();
}

std::vector<std::pair<std::string, std::string>> Build::environment() const {
    // CLICOLOR_FORCE=0 keeps ninja stripping escape sequences from the
    // commands' output, which is what tells its own status lines from a
    // nested ninja's (see kStatusFormat).
    return {{"NINJA_STATUS", std::string(kStatusFormat)},
            {"CLICOLOR_FORCE", "0"},
            {std::string(kStartsEnv), (impl_->dir / kStartsFile).string()}};
}

void Build::pass_begin() {
    auto& r = report();
    {
        std::lock_guard lock(r.m);
        auto& b = *impl_;
        b.doneBefore  += b.finished;
        b.totalBefore += b.total;
        b.finished = b.total = 0;
        b.passStart = now_ms();
        if (!r.buildStart) r.buildStart = b.passStart;
        r.phase = Phase::Building;
        b.inPass = true;
        b.ends.clear();
        b.logPath = b.dir / ".ninja_log";
        b.logOffset.reset();
        std::error_code ec;
        b.logStart = std::filesystem::exists(b.logPath, ec)
                         ? std::filesystem::file_size(b.logPath, ec) : 0;
        if (ec) b.logStart = 0;
        b.logTail.clear();
        if (b.logStart > 0) {
            std::ifstream f(b.logPath, std::ios::binary);
            const auto n = std::min<std::uintmax_t>(b.logStart, 256);
            f.seekg(static_cast<std::streamoff>(b.logStart - n));
            b.logTail.resize(static_cast<std::size_t>(n));
            f.read(b.logTail.data(), static_cast<std::streamsize>(n));
            if (!f) b.logTail.clear();
        }
        b.running.clear();
        std::ofstream(b.dir / kStartsFile, std::ios::binary | std::ios::trunc);
        b.startsOffset = 0;
    }
    mcpp::ui::touch_region();
}

void Build::status(const StatusLine& line) {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        auto& b = *impl_;
        ensure_record(b);
        b.finished = line.finished;
        b.total    = line.total;
        b.ends.push_back(line.endMs);
        read_starts(b);
        read_log(b);
        settle(r, b, out);
    }
    write_lines(out);
    mcpp::ui::touch_region();
}

bool Build::failed(std::string_view outputs) {
    auto& r = report();
    std::vector<std::string> out;
    bool first = false;
    {
        std::lock_guard lock(r.m);
        auto& b = *impl_;
        ensure_record(b);
        // `FAILED: [code=N] out1 out2 `: the step's outputs, space-separated.
        auto rest = outputs;
        if (rest.starts_with("[code=")) {
            auto close = rest.find(']');
            rest = close == std::string_view::npos ? std::string_view{} : rest.substr(close + 1);
        }
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        std::optional<std::size_t> owner;
        while (!rest.empty() && !owner) {
            auto sp = rest.find(' ');
            auto o = normalise(rest.substr(0, sp));
            if (b.record)
                if (auto it = b.record->owner.find(o); it != b.record->owner.end()) owner = it->second;
            rest.remove_prefix(sp == std::string_view::npos ? rest.size() : sp + 1);
        }
        if (owner && !b.packages[*owner].committed) {
            b.packages[*owner].failed = true;
            b.packages[*owner].committed = true;
            out.push_back(package_line(r, b, *owner, /*final=*/true, /*success=*/false));
        }
        r.phase = Phase::Stopping;
        first = !r.failureReported;
        r.failureReported = true;
    }
    write_lines(out);
    return first;
}

void Build::pass_end() {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        auto& b = *impl_;
        read_log(b);
        settle(r, b, out);
        b.inPass = false;
        b.running.clear();
    }
    write_lines(out);
}

void Build::finish(bool success) {
    auto& r = report();
    std::vector<std::string> out;
    {
        std::lock_guard lock(r.m);
        auto& b = *impl_;
        if (b.record) {
            // In the order the packages completed; those with nothing to do
            // last, in the plan's order.
            std::vector<std::pair<long long, std::string>> lines;
            constexpr auto never = std::numeric_limits<long long>::max();
            for (std::size_t i = 0; i < b.record->packages.size(); ++i) {
                auto& s = b.packages[i];
                const auto& p = b.record->packages[i];
                if (s.committed || !listed(r, p)) continue;
                // A package with nothing to do is listed only with --verbose.
                if (s.finished == 0 && !(r.verbose && success)) continue;
                s.committed = true;
                lines.emplace_back(s.finished ? s.last : never,
                                   package_line(r, b, i, /*final=*/true, success));
            }
            if (!b.depsCommitted) {
                long long last = 0;
                for (std::size_t i = 0; i < b.record->packages.size(); ++i)
                    if (!listed(r, b.record->packages[i]) && b.packages[i].finished)
                        last = std::max(last, b.packages[i].last);
                if (auto l = dependencies_line(r, b, /*final=*/true, success))
                    lines.emplace_back(last ? last : never, *l);
                b.depsCommitted = true;
            }
            std::ranges::stable_sort(lines, {}, &std::pair<long long, std::string>::first);
            for (auto& l : lines) out.push_back(std::move(l.second));
        }
        std::size_t done = 0;
        for (auto const& s : b.packages) done += s.finished;
        mcpp::log::info("progress", std::format("build {}: {} steps, {} attributed, {}",
            b.dir.filename().string(), b.doneBefore + b.finished, done,
            success ? "succeeded" : "failed"));
    }
    write_lines(out);
}

} // namespace mcpp::build::progress
