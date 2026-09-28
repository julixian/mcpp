// mcpp.build.depfile — the first record of a GNU depfile.
//
// GCC's `-fmodules` adds records to any `-MMD`/`-MF` depfile of a unit that
// imports or provides a module: the BMI "having its own inputs", a phony
// `<name>.c++-module` target, and a `CXX_IMPORTS +=` line. ninja's depfile
// loader rejects the first of those, because the BMI is also a declared output
// of the same edge (ninja_backend.cppm, the note above `gnuDepfile`). The
// textual `#include` graph, which is all header tracking needs, is the FIRST
// record: the target line and its indented continuation lines.
//
// On POSIX an awk program keeps it (`NR==1{print;next} /^[^ ]/{exit}
// {print}`), chained after the compile in the rule's shell command. Windows has
// no shell to chain it in, so `mcpp depfile-filter` runs the compile and
// applies this function; the two are the same rule, stated once each for their
// host, and `tests/unit/test_depfile.cpp` holds them to the same output.

export module mcpp.build.depfile;

import std;

export namespace mcpp::build::depfile {

// The first line, then every following line up to the first one that starts
// with a character other than a space. An empty line does not end the record,
// as it does not for the awk program.
inline std::string first_record(std::string_view raw) {
    std::string out;
    std::size_t at = 0;
    bool first = true;
    while (at < raw.size()) {
        auto nl = raw.find('\n', at);
        const auto end = nl == std::string_view::npos ? raw.size() : nl + 1;
        const auto line = raw.substr(at, end - at);
        if (!first && !line.empty() && line.front() != ' ' && line.front() != '\n'
            && line.front() != '\r')
            break;
        out.append(line);
        first = false;
        at = end;
    }
    return out;
}

} // namespace mcpp::build::depfile
