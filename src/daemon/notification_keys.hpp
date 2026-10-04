#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

namespace tether {

    // A grouped popup stays visible until its last phone notification is removed.
    template <typename Popup>
    Popup detach_notification_key(std::unordered_map<std::string, Popup>& keys, const std::string& key) {
        auto it = keys.find(key);
        if (it == keys.end())
            return {};
        const auto popup = it->second;
        keys.erase(it);
        const bool has_members =
            std::any_of(keys.begin(), keys.end(), [popup](const auto& entry) { return entry.second == popup; });
        return has_members ? Popup{} : popup;
    }

} // namespace tether
