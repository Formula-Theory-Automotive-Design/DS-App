#pragma once

#include <string>
#include <vector>

namespace backend {

// Definition of one configurable ELRS link setting: the friendly key used in
// radio.conf, and the candidate parameter-field names to look for in the
// module's menu (tried in order; the first matching TextSelection wins). Field
// names vary slightly across ELRS versions, hence a list.
struct LinkSettingDef {
    std::string key;                       // e.g. "packet_rate"
    std::vector<std::string> fieldNames;   // e.g. {"Packet Rate"}
    std::string help;                      // shown in errors / the sample file
};

// The canonical list of settings the backend knows how to push. Shared by the
// config parser (to validate keys) and the ELRS client (to find fields).
const std::vector<LinkSettingDef>& linkSettingDefs();

// Returns the candidate field names for `key`, or empty if the key is unknown.
const std::vector<std::string>* candidateFieldNames(const std::string& key);

// radio.conf parser: one `key = value` rule per line, '#' starts a comment.
// Keys must be in linkSettingDefs(); values are kept verbatim (matched against
// the module's option lists later). Hot-reloaded like the channel map.
class LinkConfig {
public:
    struct Desired {
        std::string key;
        std::string value;
    };

    // Parse `path`. On failure returns false and fills `err`.
    bool loadFile(const std::string& path, std::string& err);

    // Reparse if the file's mtime changed. Returns true if a reload happened;
    // on parse error keeps the previous settings and fills `err`.
    bool reloadIfChanged(const std::string& path, std::string& err);

    bool loaded() const { return loaded_; }
    const std::vector<Desired>& desired() const { return desired_; }

private:
    std::vector<Desired> desired_;
    bool loaded_     = false;
    long mtime_sec_  = 0;
    long mtime_nsec_ = 0;
};

}  // namespace backend
