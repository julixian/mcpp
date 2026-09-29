// mcpp.platform.terminal — terminal capability detection and output.
//
// Provides:
//   is_tty()          — whether stdout is a terminal
//   is_terminal(s)    — whether a standard stream is a terminal
//   can_move_cursor(s)— whether a live display may be drawn on it
//   cols(), rows()    — the terminal's size
//   write(s, text)    — UTF-8 text to a standard stream

module;
#include <cstdio>
#include <cstdlib>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/ioctl.h>
#endif
#if defined(_WIN32)
#include <io.h>        // _dup, _dup2, _close, _get_osfhandle, _fileno
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // SetHandleInformation, GetConsoleMode, WriteConsoleW
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING   // older SDK and MinGW headers
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>    // dup2, close
#include <fcntl.h>     // fcntl, F_DUPFD_CLOEXEC
#endif

export module mcpp.platform.terminal;

import std;

export namespace mcpp::platform::terminal {

// The two standard streams a human reads.
enum class Stream { Out, Err };

// Returns true if stdout is connected to a terminal (TTY).
bool is_tty();

// Whether the stream is a terminal: `isatty` on POSIX, macOS included, and a
// console on Windows. Until 2026.9.29.5 this was compiled only under
// `__unix__`, which Apple's compilers do not define, so macOS and Windows were
// never terminals and drew neither colour nor a live line. A Windows program
// under mintty writes to a pipe, not a console, and is not a terminal here.
bool is_terminal(Stream s);

// Whether a display that moves the cursor may be drawn on the stream: a
// terminal, not `TERM=dumb`, and on Windows a console that accepted virtual
// terminal processing. The first call on Windows enables that processing, so
// that the escape sequences mcpp writes are interpreted rather than printed.
bool can_move_cursor(Stream s);

// Returns the terminal width in columns. Tries the terminal first (TIOCGWINSZ,
// or the console's window on Windows), falls back to $COLUMNS, then to 80.
std::size_t cols();

// The terminal's height in rows, by the same order, with $LINES and 24.
std::size_t rows();

// Writes UTF-8 text to the stream. A Windows console receives it as UTF-16
// through WriteConsoleW, so that text outside ASCII (`·`, `→`, a path in
// Chinese) appears as written whatever the console's code page is; the stdio
// buffer is flushed first, so the two paths keep their order. Everything else
// receives the bytes through stdio, unflushed.
void write(Stream s, std::string_view text);

// EVERYTHING WRITTEN TO STANDARD OUTPUT GOES TO STANDARD ERROR UNTIL THIS IS
// DESTROYED.
//
// The redirection is made at the file descriptor, so a child process that
// inherits standard output follows it, and so does narration that prints to
// stdout without consulting any quiet flag. A command whose standard output is
// a document (`mcpp emit build-database`) plans under one of these and prints
// the document after it is gone: people still see the progress, on stderr, and
// the document arrives alone.
//
// THE SAVED DESCRIPTOR IS NOT INHERITED. It is the caller's pipe, and a child
// started during the redirection (a build program, an xlings refresh) that
// inherited it would keep the caller from reading end-of-file for as long as
// the child lives, however long after mcpp itself exited. Measured
// (mcpp-community/mcpp#648): a build program started by
// `emit build-database` held the caller's pipe as its descriptor 3. The copy
// is therefore close-on-exec on POSIX and not inheritable on Windows, so a
// child receives only descriptors 0 to 2, and 1 is standard error here.
class StdoutToStderr {
public:
    StdoutToStderr();
    ~StdoutToStderr();
    StdoutToStderr(const StdoutToStderr&) = delete;
    StdoutToStderr& operator=(const StdoutToStderr&) = delete;
private:
    int saved_ = -1;
};

} // namespace mcpp::platform::terminal

namespace mcpp::platform::terminal {

namespace {

std::FILE* file_of(Stream s) { return s == Stream::Out ? stdout : stderr; }

#if defined(_WIN32)
HANDLE handle_of(Stream s) {
    const auto h = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file_of(s))));
    return h == nullptr ? INVALID_HANDLE_VALUE : h;
}

bool console_of(Stream s, HANDLE* out = nullptr) {
    const auto h = handle_of(s);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !::GetConsoleMode(h, &mode)) return false;
    if (out) *out = h;
    return true;
}
#endif

} // namespace

bool is_terminal(Stream s) {
#if defined(_WIN32)
    return console_of(s);
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(::fileno(file_of(s))) != 0;
#else
    (void)s;
    return false;
#endif
}

bool is_tty() { return is_terminal(Stream::Out); }

bool can_move_cursor(Stream s) {
    if (!is_terminal(s)) return false;
    if (const char* term = std::getenv("TERM"); term && std::string_view(term) == "dumb")
        return false;
#if defined(_WIN32)
    HANDLE h;
    if (!console_of(s, &h)) return false;
    DWORD mode = 0;
    if (!::GetConsoleMode(h, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

namespace {
std::size_t from_env(const char* name, std::size_t fallback) {
    if (auto* e = std::getenv(name); e && *e) {
        try { auto n = std::stoul(e); if (n > 0) return n; } catch (...) {}
    }
    return fallback;
}
} // namespace

std::size_t cols() {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (HANDLE h; console_of(Stream::Out, &h) && ::GetConsoleScreenBufferInfo(h, &info))
        return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
#elif defined(__unix__) || defined(__APPLE__)
    struct winsize w{};
    if (::ioctl(::fileno(stdout), TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
#endif
    return from_env("COLUMNS", 80);
}

std::size_t rows() {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (HANDLE h; console_of(Stream::Out, &h) && ::GetConsoleScreenBufferInfo(h, &info))
        return static_cast<std::size_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
#elif defined(__unix__) || defined(__APPLE__)
    struct winsize w{};
    if (::ioctl(::fileno(stdout), TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
        return w.ws_row;
#endif
    return from_env("LINES", 24);
}

void write(Stream s, std::string_view text) {
    if (text.empty()) return;
#if defined(_WIN32)
    if (HANDLE h; console_of(s, &h)) {
        std::fflush(file_of(s));
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                            static_cast<int>(text.size()), nullptr, 0);
        if (n > 0) {
            std::wstring wide(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  wide.data(), n);
            DWORD written = 0;
            if (::WriteConsoleW(h, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr))
                return;
        }
    }
#endif
    std::fwrite(text.data(), 1, text.size(), file_of(s));
}

StdoutToStderr::StdoutToStderr() {
    std::fflush(stdout);
#if defined(_WIN32)
    saved_ = ::_dup(1);
    if (saved_ >= 0) {
        // `_dup` duplicates the handle as inheritable, and CreateProcess with
        // handle inheritance (every `_popen` and `system`) passes it on.
        const auto h = reinterpret_cast<HANDLE>(::_get_osfhandle(saved_));
        if (h != INVALID_HANDLE_VALUE)
            ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        ::_dup2(2, 1);
    }
#else
    // Not `dup`: its copy survives exec. Descriptor 3 or above, as `dup`
    // would have chosen, so nothing else about the redirection moves.
    saved_ = ::fcntl(1, F_DUPFD_CLOEXEC, 3);
    if (saved_ >= 0) ::dup2(2, 1);
#endif
}

StdoutToStderr::~StdoutToStderr() {
    std::fflush(stdout);
    if (saved_ < 0) return;
#if defined(_WIN32)
    ::_dup2(saved_, 1);
    ::_close(saved_);
#else
    ::dup2(saved_, 1);
    ::close(saved_);
#endif
}

} // namespace mcpp::platform::terminal
