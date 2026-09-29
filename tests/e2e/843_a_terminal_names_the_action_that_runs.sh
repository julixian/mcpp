#!/usr/bin/env bash
# requires: python3 unix-shell
# 843_a_terminal_names_the_action_that_runs.sh -- build progress design
# 2026-09-29, §4.4 and §5.1 (the terminal medium), through a pseudo-terminal.
#
#   T1  while a `prepare` action runs, the status line names its package, its
#       label and its clock: ninja reports no step when it starts, and the
#       engine's action wrapper does (§6.4);
#   T2  the package's line becomes final with its outcome, and one blank line
#       precedes `Finished`;
#   T3  the status line is not drawn again after `Finished`.
set -e

TMP=$(mktemp -d)
trap "rm -rf $TMP" EXIT
cd "$TMP"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
MCPP="${MCPP:-mcpp}"

mkdir -p app/src
cat > app/mcpp.toml <<'EOF'
[package]
name = "app"
version = "0.1.0"

[targets.app]
kind = "bin"
main = "src/main.cpp"
EOF
printf 'int main() { return 0; }\n' > app/src/main.cpp
cat > app/build.mcpp <<'EOF'
import mcpp;
#include <string>
int main() {
    const std::string prefix = std::string(mcpp::out_dir()) + "/tool";
    mcpp::action a;
    a.id   = "app:install";
    a.role = mcpp::roles::prepare;
    a.arg("python3").arg("-c")
     .arg("import os, sys, time; time.sleep(4); os.makedirs(sys.argv[1], exist_ok=True); open(os.path.join(sys.argv[1], \"ready\"), \"w\").close()")
     .arg(prefix.c_str())
     .output((prefix + ".stamp").c_str())
     .output_dir(prefix.c_str())
     .submit();
    return 0;
}
EOF
cd app
python3 - "$MCPP" <<'PY' > pty.log 2>&1 || fail "the terminal build" pty.log
import fcntl, os, pty, re, struct, sys, termios
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.environ.pop("NO_COLOR", None)
    os.execvp(sys.argv[1], [sys.argv[1], "build"])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
raw = b""
while True:
    try:
        chunk = os.read(fd, 65536)
    except OSError:
        break
    if not chunk:
        break
    raw += chunk
_, status = os.waitpid(pid, 0)
text = raw.decode("utf-8", "replace")

# The screen the terminal shows at the end: carriage return, line feed,
# cursor up (`CSI n A`), erase to the end of the screen (`CSI J`) and of the
# line (`CSI K`); colour is ignored.
def screen_of(s):
    rows, r, c, i = [""], 0, 0, 0
    while i < len(s):
        ch = s[i]
        if ch == "\x1b" and i + 1 < len(s) and s[i + 1] == "[":
            j = i + 2
            while j < len(s) and not ("@" <= s[j] <= "~"):
                j += 1
            params, final = s[i + 2:j], s[j] if j < len(s) else ""
            n = int(params) if params.isdigit() else 1
            if final == "A":
                r = max(0, r - n)
            elif final == "J":
                rows[r] = rows[r][:c]
                del rows[r + 1:]
            elif final == "K":
                rows[r] = "" if params == "2" else rows[r][:c]
            i = j + 1
            continue
        if ch == "\r":
            c = 0
        elif ch == "\n":
            r += 1
            while len(rows) <= r:
                rows.append("")
        else:
            line = rows[r].ljust(c)
            rows[r] = line[:c] + ch + line[c + 1:]
            c += 1
        i += 1
    return [row.rstrip() for row in rows]

plain = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
screen = screen_of(text)
print("\n".join(screen))
assert os.WEXITSTATUS(status) == 0, "the build failed"
# T1: from the stream, since the status line is erased at the end.
assert re.search(r"app: app:install \d+:\d\d", plain), "T1: no status line named the running action"
# T2
at = max(i for i, row in enumerate(screen) if "Finished" in row)
assert screen[at - 1] == "", "T2: no blank line precedes Finished"
assert re.search(r"Compiling app v0\.1\.0 \(\.\) +done \d", screen[at - 2]), \
    "T2: the package's final line does not come before Finished"
# T3
assert not any(("Building" in row or "Checking" in row) for row in screen[at:]), \
    "T3: the status line was drawn after Finished"
PY

echo "PASS: 843_a_terminal_names_the_action_that_runs"
