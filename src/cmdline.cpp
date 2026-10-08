// Windows command-line quoting (a pure function, so it is tested everywhere).
#include "brolink/stream.h"

namespace brolink {

namespace {

// One argument quoted the way the MSVC runtime's argv parser undoes it.
// Backslashes are literal except in front of a quote (or the closing quote
// added here), where each must be doubled.
std::string quote_arg(std::string_view arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(arg);
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') out.append(backslashes * 2 + 1, '\\');
        else out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

}  // namespace

std::string windows_command_line(const std::vector<std::string>& argv) {
    if (argv.empty()) return {};
    // argv[0] is parsed differently by the CRT: quotes delimit, backslashes are
    // never escapes (and file names cannot contain quotes).
    const std::string& command = argv[0];
    std::string line;
    if (command.empty() || command.find_first_of(" \t") != std::string::npos) {
        line += '"';
        line += command;
        line += '"';
    } else {
        line = command;
    }
    for (size_t i = 1; i < argv.size(); ++i) {
        line.push_back(' ');
        line += quote_arg(argv[i]);
    }
    return line;
}

}  // namespace brolink
