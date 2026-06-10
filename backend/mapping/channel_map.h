#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "crsf_packets.h"
#include "hid/joystick.h"

namespace backend {

// Human-editable joystick -> CRSF channel mapping, loaded from a text file and
// hot-reloaded when the file changes (so you can tune the mapping live without
// restarting the backend).
//
// File format — one rule per line, '#' starts a comment:
//
//     chN = <expr>
//
// <expr> is either a single whole-channel constant, or a sum of signed terms:
//     axisK     raw axis K, -1..1                 (e.g. a steering wheel)
//     pedalK    axis K read as a pedal: released(+1) -> 0, pressed(-1) -> 1
//     buttonK   0 or 1
//     <float>   a scalar offset in the normalized domain (1.0 == full travel)
//     center | min | max | <number 172..1811>     a whole-channel constant
//                                                  (must be the only token)
//
// Any axis/pedal/button term may carry a factor and/or a clamp:
//     0.5*axis0               scale a term (gain / rate)
//     clamp(axis0,-0.5,0.5)   limit a term's travel
//     2*clamp(axis0,-0.5,0.5) clamp first, then scale
//
// Terms combine with + / - and the summed result is clamped to [-1,1] then
// scaled so  0 -> 992 (center),  +1 -> 1811,  -1 -> 172.
// Reverse a control by negating it. Examples:
//     ch0 = axis0                  # steering
//     ch0 = 0.5*axis0 + 0.1        # half-rate steering with a small right trim
//     ch1 = pedal2 - pedal5        # accelerator(axis2) minus brake(axis5)
//     ch0 = -axis0                 # reversed steering
//     ch0 = clamp(axis0,-0.5,0.5)  # steering limited to half travel
class ChannelMap {
public:
    // Parse `path`. On failure returns false and fills `err`.
    bool loadFile(const std::string& path, std::string& err);

    // If the file's mtime changed since the last load, reparse it. Returns true
    // if a reload happened; on parse error keeps the previous rules and fills
    // `err`.
    bool reloadIfChanged(const std::string& path, std::string& err);

    bool loaded() const { return !rules_.empty(); }

    // Write channel values for every rule into `ch` (channels without a rule
    // keep their existing value).
    void apply(const JoystickState& js, crsf::RCChannels& ch) const;

    // Parsed representation. Public so the file parser (in the .cpp) can build it.
    enum Kind { Axis = 0, Pedal = 1, Button = 2, Const = 3 };
    struct Term {
        Kind  kind     = Axis;
        int   idx      = 0;       // axis/pedal/button index (unused for Const)
        float coeff    = 1.0f;    // signed factor; "-0.5*axis0" -> coeff == -0.5
        float constVal = 0.0f;    // normalized scalar offset (Const only)
        bool  hasClamp = false;   // clamp(term, lo, hi)
        float clampLo  = -1.0f;
        float clampHi  =  1.0f;
    };
    struct Rule {
        int           channel  = 0;
        bool          isConst  = false;  // whole-channel constant
        std::uint16_t constVal = crsf::CRSF_CHANNEL_VALUE_MID;
        std::vector<Term> terms;
    };

private:
    std::vector<Rule> rules_;
    long mtime_sec_  = 0;
    long mtime_nsec_ = 0;
};

}  // namespace backend
