#!/usr/bin/env bash
# requires: unix-shell python3
# 866 -- with narration on standard error and results on standard output, the
# two streams on one pipe keep the order in which they were written, and the
# machine-readable modes keep standard output for their document (output
# streams plan 2026-10-01, §6 R3).
#
# Two streams are two write paths. `2>&1` joins them at the pipe, where each
# write arrives when it is made, so the order is the order of the writes only
# if mcpp flushes one before it writes the other. This states that it does.
#
# Criteria:
#   A. `mcpp run 2>&1`: the `Running` line, then the blank line that separates
#      it, then the program's output. The program flushes each line before it
#      writes the next to the other stream, so the order of the program's own
#      lines is fixed too.
#   B. `mcpp test --message-format json`: every line of standard output is a
#      JSON object, and standard error carries no status. The mode's
#      `set_quiet` is unchanged.
#   C. `mcpp build 2>&1` orders the steps: `Compiling` before `Finished`, with
#      a blank line before `Finished`.
#   D. The live status row follows standard error. With standard output a pipe
#      and standard error a terminal the row is drawn, and the pipe receives
#      nothing; with standard output a terminal and standard error a file the
#      row is not drawn, the file holds the steps without an escape sequence,
#      and the terminal receives nothing.
set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
cd "$TMP"

mkdir -p p/src p/tests q/src
cat > p/mcpp.toml <<'EOF'
[package]
name    = "p"
version = "0.1.0"

[targets.p]
kind = "bin"
main = "src/main.cpp"
EOF
cat > p/src/main.cpp <<'EOF'
#include <cstdio>
int main() {
    std::fputs("first, to standard output\n", stdout);
    std::fflush(stdout);
    std::fputs("second, to standard error\n", stderr);
    std::fputs("third, to standard output\n", stdout);
    std::fflush(stdout);
    return 0;
}
EOF
cat > p/tests/t_ok.cpp <<'EOF'
int main() { return 0; }
EOF
# A build that lasts long enough for the status row to be drawn (it is first
# drawn half a second into the command): one action that sleeps.
cat > q/mcpp.toml <<'EOF'
[package]
name    = "q"
version = "0.1.0"

[targets.q]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > q/src/main.cpp
cat > q/build.mcpp <<'EOF'
import mcpp;
#include <string>
int main() {
    const std::string prefix = std::string(mcpp::out_dir()) + "/tool";
    mcpp::action a;
    a.id   = "q:wait";
    a.role = mcpp::roles::prepare;
    a.arg("python3").arg("-c")
     .arg("import os, sys, time; time.sleep(3); os.makedirs(sys.argv[1], exist_ok=True); open(os.path.join(sys.argv[1], \"ready\"), \"w\").close()")
     .arg(prefix.c_str())
     .output((prefix + ".stamp").c_str())
     .output_dir(prefix.c_str())
     .submit();
    return 0;
}
EOF
cd p

# ── C ──────────────────────────────────────────────────────────────────────
"$MCPP" build 2>&1 | cat > c.log
compiling=$(grep -n 'Compiling p v0.1.0' c.log | head -1 | cut -d: -f1)
finished=$(grep -n 'Finished ' c.log | head -1 | cut -d: -f1)
[ -n "$compiling" ] && [ -n "$finished" ] || fail "C: the steps are not on the pipe" c.log
[ "$compiling" -lt "$finished" ] || fail "C: Finished precedes Compiling" c.log
[ "$(sed -n "$((finished - 1))p" c.log)" = "" ] || fail "C: no blank line precedes Finished" c.log

# ── A ──────────────────────────────────────────────────────────────────────
"$MCPP" run 2>&1 | cat > a.log
running=$(grep -n 'Running `' a.log | head -1 | cut -d: -f1)
first=$(grep -n '^first, to standard output' a.log | cut -d: -f1)
second=$(grep -n '^second, to standard error' a.log | cut -d: -f1)
third=$(grep -n '^third, to standard output' a.log | cut -d: -f1)
[ -n "$running" ] && [ -n "$first" ] && [ -n "$second" ] && [ -n "$third" ] \
    || fail "A: a line is missing from the pipe" a.log
[ "$running" -lt "$first" ] || fail "A: the Running line does not precede the program's output" a.log
[ "$(sed -n "$((running + 1))p" a.log)" = "" ] || fail "A: no blank line follows Running" a.log
[ "$((running + 2))" = "$first" ] || fail "A: something lies between the blank line and the program's output" a.log
[ "$first" -lt "$second" ] && [ "$second" -lt "$third" ] \
    || fail "A: the program's lines are out of order" a.log

# ── B ──────────────────────────────────────────────────────────────────────
"$MCPP" test --message-format json > b.out 2> b.err || fail "B: mcpp test --message-format json failed" b.out b.err
[ -s b.out ] || fail "B: no record on standard output"
python3 - b.out <<'PY' || fail "B: a line of standard output is not a JSON object" b.out
import json, sys
for line in open(sys.argv[1]):
    line = line.strip()
    if line:
        assert isinstance(json.loads(line), dict), line
PY
grep -qE 'Compiling|Resolving|Resolved|Finished' b.err && fail "B: status on standard error under --message-format json" b.err

# ── D ──────────────────────────────────────────────────────────────────────
cd "$TMP/q"
python3 - "$MCPP" <<'PY' > d.log 2>&1 || fail "D: the terminal builds" d.log
import os, pty, select, sys

def run(mcpp, stdout_is_terminal):
    """Runs `mcpp build` with one standard stream a pseudo-terminal and the
    other a pipe (standard output) or a file (standard error); returns what
    the terminal and the other stream received."""
    master, slave = pty.openpty()
    if stdout_is_terminal:
        out_fd = slave
        err_fd = os.open("err.txt", os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
        other = None
    else:
        r, w = os.pipe()
        out_fd, err_fd, other = w, slave, r
    pid = os.fork()
    if pid == 0:
        os.close(master)
        if other is not None:
            os.close(other)
        os.dup2(out_fd, 1)
        os.dup2(err_fd, 2)
        os.dup2(os.open(os.devnull, os.O_RDONLY), 0)
        os.environ["TERM"] = "xterm-256color"
        os.environ["LANG"] = "C.UTF-8"
        os.environ["MCPP_PROGRESS"] = "plain"
        os.environ.pop("NO_COLOR", None)
        os.execvp(mcpp, [mcpp, "build"])
    os.close(slave)
    if not stdout_is_terminal:
        os.close(out_fd)
    term, piped = b"", b""
    open_fds = [master] + ([other] if other is not None else [])
    while open_fds:
        ready, _, _ = select.select(open_fds, [], [], 1.0)
        for fd in ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                chunk = b""
            if not chunk:
                open_fds.remove(fd)
            elif fd == master:
                term += chunk
            else:
                piped += chunk
    _, status = os.waitpid(pid, 0)
    assert os.WEXITSTATUS(status) == 0, "the build failed"
    return term.decode("utf-8", "replace"), piped.decode("utf-8", "replace")

mcpp = sys.argv[1]

# Standard output a pipe, standard error a terminal: the row is drawn there.
term, piped = run(mcpp, stdout_is_terminal=False)
print("--- D1 terminal (standard error)"); print(repr(term[-400:]))
assert "\x1b[?7l" in term, "D1: the status row is not drawn on standard error"
assert "Finished" in term, "D1: Finished is not on standard error"
assert piped == "", f"D1: the pipe received {piped!r}"

# Standard output a terminal, standard error a file: no row, no escape sequence.
term, piped = run(mcpp, stdout_is_terminal=True)
err = open("err.txt", encoding="utf-8").read()
print("--- D2 terminal (standard output)"); print(repr(term[-400:]))
print("--- D2 file (standard error)"); print(repr(err[-400:]))
assert term == "", f"D2: the terminal on standard output received {term!r}"
assert "\x1b" not in err, "D2: the file on standard error holds an escape sequence"
assert "Finished" in err, "D2: Finished is not in the file on standard error"
PY

echo "OK: the streams keep their order on one pipe; JSON standard output is clean; the row follows standard error"
