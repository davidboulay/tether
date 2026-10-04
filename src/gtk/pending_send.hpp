#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <tether/bluetooth/messages.hpp>

namespace tether::ui {

    struct PendingSend {
        std::string operation_id;
        std::string thread;
        std::string body;
        std::string handle;
        std::string failure;
        bool succeeded = false;
        int64_t timestamp = 0;
        std::set<std::string> previous_handles;
    };

    // Keep send ownership separate from the currently visible conversation.
    class PendingSends {
    public:
        void put(PendingSend send) { sends_[bluetooth::thread_bucket(send.thread)] = std::move(send); }

        PendingSend* for_thread(const std::string& thread) {
            auto it = sends_.find(bluetooth::thread_bucket(thread));
            return it == sends_.end() ? nullptr : &it->second;
        }

        PendingSend* for_result(const std::string& thread, const std::string& operation_id) {
            auto* send = for_thread(thread);
            return send && !operation_id.empty() && send->operation_id == operation_id ? send : nullptr;
        }

        bool reconcile(const std::string& thread,
                       const std::string& handle,
                       const std::string& body = {},
                       bool outgoing = false,
                       int64_t timestamp = 0) {
            auto* send = for_thread(thread);
            if (!send || !send->succeeded || handle.empty())
                return false;
            // MAP may replace the local echo before the listing reaches the UI.
            // Match its replacement using the store's body/time rules, excluding
            // handles already present when this attempt started.
            const bool replacement = outgoing && !bluetooth::is_local_handle(handle) && send->timestamp > 0 &&
                                     timestamp > 0 && !send->previous_handles.contains(handle) &&
                                     std::llabs(send->timestamp - timestamp) <= bluetooth::LOCAL_ECHO_WINDOW_SECONDS &&
                                     normalized_body(send->body) == normalized_body(body);
            if (send->handle != handle && !replacement)
                return false;
            sends_.erase(bluetooth::thread_bucket(thread));
            return true;
        }

    private:
        static std::string normalized_body(const std::string& body) {
            std::string result;
            bool space = false;
            for (unsigned char c : body) {
                if (std::isspace(c)) {
                    space = !result.empty();
                    continue;
                }
                if (space)
                    result += ' ';
                result += static_cast<char>(c);
                space = false;
            }
            return result;
        }

        std::map<std::string, PendingSend> sends_;
    };

} // namespace tether::ui
