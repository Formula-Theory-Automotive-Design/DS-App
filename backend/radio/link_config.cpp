#include "radio/link_config.h"

#include <sys/stat.h>

#include <fstream>
#include <string>

namespace backend {

const std::vector<LinkSettingDef>& linkSettingDefs() {
    // ExpressLRS menu field names. Power lives inside the "TX Power" folder as
    // "Max Power"; we list "Max Power" first so we write the option list rather
    // than the folder itself. "FEC" is only present on some modules.
    static const std::vector<LinkSettingDef> defs = {
        {"packet_rate",     {"Packet Rate"},           "e.g. 50, 150, 250, 500 (Hz)"},
        {"power",           {"Max Power", "Power"},     "e.g. 10, 25, 50, 100, 250 (mW)"},
        {"telemetry_ratio", {"Telem Ratio"},            "off, 1:128, 1:64 ... 1:2, std"},
        {"switch_mode",     {"Switch Mode"},            "hybrid or wide"},
        {"fec",             {"FEC"},                    "on or off (module-dependent)"},
    };
    return defs;
}

const std::vector<std::string>* candidateFieldNames(const std::string& key) {
    for (const LinkSettingDef& d : linkSettingDefs())
        if (d.key == key) return &d.fieldNames;
    return nullptr;
}

namespace {

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

bool LinkConfig::loadFile(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }

    std::vector<Desired> parsed;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (const std::size_t h = line.find('#'); h != std::string::npos)
            line = line.substr(0, h);
        line = trim(line);
        if (line.empty()) continue;

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            err = "line " + std::to_string(lineNo) + ": expected 'key = value'";
            return false;
        }
        const std::string key   = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (!candidateFieldNames(key)) {
            err = "line " + std::to_string(lineNo) + ": unknown setting '" + key + "'";
            return false;
        }
        if (value.empty()) {
            err = "line " + std::to_string(lineNo) + ": '" + key + "' has no value";
            return false;
        }
        parsed.push_back({key, value});
    }

    desired_ = std::move(parsed);
    loaded_  = true;

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        mtime_sec_  = static_cast<long>(st.st_mtim.tv_sec);
        mtime_nsec_ = static_cast<long>(st.st_mtim.tv_nsec);
    }
    return true;
}

bool LinkConfig::reloadIfChanged(const std::string& path, std::string& err) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (static_cast<long>(st.st_mtim.tv_sec) == mtime_sec_ &&
        static_cast<long>(st.st_mtim.tv_nsec) == mtime_nsec_) {
        return false;
    }
    auto saved = desired_;
    if (!loadFile(path, err)) {
        desired_ = std::move(saved);
        mtime_sec_  = static_cast<long>(st.st_mtim.tv_sec);
        mtime_nsec_ = static_cast<long>(st.st_mtim.tv_nsec);
        return false;
    }
    return true;
}

}  // namespace backend
