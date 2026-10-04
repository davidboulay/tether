#include "verbs.hpp"

namespace tether::cli {

    const Verb kVerbs[] = {
        {"status", "--status", "tether status"},
        {"devices", "--list-devices", "tether devices"},
        {"pending", "--pending", "tether pending"},
        {"accept", "--accept", "tether accept <fingerprint>"},
        {"reject", "--reject", "tether reject <fingerprint>"},
        {"forget", "--forget", "tether forget <fingerprint>"},
        {"pair", "--pair", "tether pair --host <ip>"},
        {"discover", "--discover", "tether discover"},
        {"send", "--send-file", "tether send <path>"},
        {"copy", "--set-clipboard", "tether copy [text]"},
        {"paste", "--get-clipboard", "tether paste"},
        {"clipboard", "--clipboard-sync", "tether clipboard <on|off|status>"},
        {"mute", "--mute", "tether mute <bundle-id>|list"},
        {"unmute", "--unmute", "tether unmute <bundle-id>"},
        {"service", "--install-service", "tether service"},
        {"version", "--version", "tether version"},
        {"help", "--help", "tether help"},
    };

    const size_t kVerbCount = sizeof(kVerbs) / sizeof(kVerbs[0]);

    std::vector<std::string> expand_verbs(const std::vector<std::string>& args) {
        // Global switches that take no value may come first: 'tether --json status'.
        size_t at = 1;
        while (at < args.size() && args[at] == "--json")
            ++at;
        if (at >= args.size() || args[at].empty() || args[at][0] == '-')
            return args;

        std::vector<std::string> out;
        out.reserve(args.size());
        out.insert(out.end(), args.begin(), args.begin() + static_cast<std::ptrdiff_t>(at));

        // 'tether bt <name> ...' is '--bt-<name> ...', so every Bluetooth flag
        // has a subcommand without a table to keep in step with it.
        if (args[at] == "bt") {
            if (at + 1 >= args.size() || args[at + 1].empty() || args[at + 1][0] == '-')
                return args;
            out.push_back("--bt-" + args[at + 1]);
            out.insert(out.end(), args.begin() + static_cast<std::ptrdiff_t>(at + 2), args.end());
            return out;
        }

        for (size_t i = 0; i < kVerbCount; ++i) {
            if (args[at] != kVerbs[i].verb)
                continue;
            out.push_back(kVerbs[i].flag);
            out.insert(out.end(), args.begin() + static_cast<std::ptrdiff_t>(at + 1), args.end());
            return out;
        }

        return args;
    }

} // namespace tether::cli
