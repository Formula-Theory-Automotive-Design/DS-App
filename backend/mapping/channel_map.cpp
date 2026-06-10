#include "mapping/channel_map.h"

#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace backend {

namespace {

constexpr float kSpan =
    (crsf::CRSF_CHANNEL_VALUE_MAX - crsf::CRSF_CHANNEL_VALUE_MIN) / 2.0f;

std::uint16_t toChannel(float v) {
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    return static_cast<std::uint16_t>(crsf::CRSF_CHANNEL_VALUE_MID + v * kSpan);
}

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "axis3" with prefix "axis" -> idx 3. Returns false if prefix doesn't match.
bool matchIndexed(const std::string& tok, const char* prefix, int& idx) {
    const std::size_t n = std::strlen(prefix);
    if (tok.size() <= n || tok.compare(0, n, prefix) != 0) return false;
    for (std::size_t i = n; i < tok.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    idx = std::atoi(tok.c_str() + n);
    return true;
}

// --- Expression tokenizer + recursive-descent parser ------------------------
//
//   expr   := ['+'|'-'] term ( ('+'|'-') term )*
//   term   := number '*' factor      (scaled term)
//           | number                 (bare scalar offset)
//           | factor
//   factor := 'clamp' '(' atom ',' number ',' number ')'
//           | atom
//   atom   := axisN | pedalN | buttonN

struct Tok {
    enum Type { Num, Ident, Plus, Minus, Star, LParen, RParen, Comma, End };
    Type type = End;
    float num = 0.0f;
    std::string text;
    Tok() = default;
    Tok(Type t) : type(t) {}  // NOLINT: implicit so `{Tok::Plus}` is concise
};

bool tokenize(const std::string& s, std::vector<Tok>& out, std::string& err) {
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        switch (c) {
            case '+': out.push_back({Tok::Plus});   ++i; continue;
            case '-': out.push_back({Tok::Minus});  ++i; continue;
            case '*': out.push_back({Tok::Star});   ++i; continue;
            case '(': out.push_back({Tok::LParen}); ++i; continue;
            case ')': out.push_back({Tok::RParen}); ++i; continue;
            case ',': out.push_back({Tok::Comma});  ++i; continue;
            default: break;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.'))
                ++j;
            Tok t; t.type = Tok::Num; t.text = s.substr(i, j - i);
            t.num = static_cast<float>(std::atof(t.text.c_str()));
            out.push_back(t); i = j; continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < s.size() && std::isalnum(static_cast<unsigned char>(s[j]))) ++j;
            Tok t; t.type = Tok::Ident; t.text = s.substr(i, j - i);
            out.push_back(t); i = j; continue;
        }
        err = std::string("unexpected character '") + c + "'";
        return false;
    }
    out.push_back({Tok::End});
    return true;
}

struct Parser {
    const std::vector<Tok>& toks;
    std::size_t pos = 0;
    std::string err;

    explicit Parser(const std::vector<Tok>& t) : toks(t) {}

    const Tok& cur() const { return toks[pos]; }
    bool is(Tok::Type t) const { return cur().type == t; }
    bool eat(Tok::Type t) { if (is(t)) { ++pos; return true; } return false; }

    // A signed numeric literal, e.g. the bounds of clamp(...).
    bool number(float& out) {
        float sign = 1.0f;
        if (eat(Tok::Minus)) sign = -1.0f; else eat(Tok::Plus);
        if (!is(Tok::Num)) { err = "expected a number"; return false; }
        out = sign * cur().num; ++pos; return true;
    }

    bool atom(ChannelMap::Term& term) {
        if (!is(Tok::Ident)) { err = "expected axis/pedal/button"; return false; }
        const std::string& id = cur().text;
        int idx;
        if      (matchIndexed(id, "axis",   idx)) { term.kind = ChannelMap::Axis;   term.idx = idx; }
        else if (matchIndexed(id, "pedal",  idx)) { term.kind = ChannelMap::Pedal;  term.idx = idx; }
        else if (matchIndexed(id, "button", idx)) { term.kind = ChannelMap::Button; term.idx = idx; }
        else { err = "unknown term '" + id + "'"; return false; }
        ++pos; return true;
    }

    bool factor(ChannelMap::Term& term) {
        if (is(Tok::Ident) && cur().text == "clamp") {
            ++pos;
            if (!eat(Tok::LParen)) { err = "expected '(' after clamp"; return false; }
            if (!atom(term)) return false;
            if (!eat(Tok::Comma)) { err = "expected ',' in clamp(term, lo, hi)"; return false; }
            if (!number(term.clampLo)) return false;
            if (!eat(Tok::Comma)) { err = "expected ',' in clamp(term, lo, hi)"; return false; }
            if (!number(term.clampHi)) return false;
            if (!eat(Tok::RParen)) { err = "expected ')' to close clamp(...)"; return false; }
            if (term.clampLo > term.clampHi) { err = "clamp low bound exceeds high bound"; return false; }
            term.hasClamp = true;
            return true;
        }
        return atom(term);
    }

    // Parse one term, applying `sign` (+1/-1) as the outer sign.
    bool term(float sign, ChannelMap::Term& out) {
        if (is(Tok::Num)) {
            const float n = cur().num; ++pos;
            if (eat(Tok::Star)) {            // number '*' factor  -> scaled term
                out.coeff = sign * n;
                return factor(out);
            }
            out.kind = ChannelMap::Const;    // bare number -> scalar offset
            out.coeff = sign;
            out.constVal = n;
            return true;
        }
        out.coeff = sign;
        return factor(out);
    }

    bool expr(ChannelMap::Rule& rule) {
        float sign = 1.0f;
        if (eat(Tok::Minus)) sign = -1.0f; else eat(Tok::Plus);
        ChannelMap::Term first;
        if (!term(sign, first)) return false;
        rule.terms.push_back(first);
        while (is(Tok::Plus) || is(Tok::Minus)) {
            const float s = is(Tok::Minus) ? -1.0f : 1.0f; ++pos;
            ChannelMap::Term next;
            if (!term(s, next)) return false;
            rule.terms.push_back(next);
        }
        if (!is(Tok::End)) {
            err = "unexpected '" + (cur().text.empty() ? std::string("end of line")
                                                       : cur().text) + "'";
            return false;
        }
        return true;
    }
};

}  // namespace

bool ChannelMap::loadFile(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }

    std::vector<Rule> parsed;
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
            err = "line " + std::to_string(lineNo) + ": expected 'chN = ...'";
            return false;
        }
        std::string lhs = trim(line.substr(0, eq));
        std::string rhs = trim(line.substr(eq + 1));

        int ch = 0;
        if (!matchIndexed(lhs, "ch", ch) ||
            ch < 0 || ch >= static_cast<int>(crsf::CRSF_NUM_CHANNELS)) {
            err = "line " + std::to_string(lineNo) + ": bad channel '" + lhs + "'";
            return false;
        }

        Rule rule;
        rule.channel = ch;

        if (rhs.empty()) {
            err = "line " + std::to_string(lineNo) + ": empty expression";
            return false;
        }

        // A whole-channel constant (named, or a CRSF integer literal) must be the
        // entire right-hand side. A bare float, by contrast, is a normalized
        // scalar offset term and falls through to the expression parser.
        if (rhs == "center") { rule.isConst = true; rule.constVal = crsf::CRSF_CHANNEL_VALUE_MID; parsed.push_back(rule); continue; }
        if (rhs == "min")    { rule.isConst = true; rule.constVal = crsf::CRSF_CHANNEL_VALUE_MIN; parsed.push_back(rule); continue; }
        if (rhs == "max")    { rule.isConst = true; rule.constVal = crsf::CRSF_CHANNEL_VALUE_MAX; parsed.push_back(rule); continue; }
        {
            std::string digits = rhs;
            if (digits[0] == '+' || digits[0] == '-') digits = digits.substr(1);
            bool isInt = !digits.empty();
            for (char c : digits)
                if (!std::isdigit(static_cast<unsigned char>(c))) isInt = false;
            if (isInt) {
                int v = std::atoi(rhs.c_str());
                if (v < crsf::CRSF_CHANNEL_VALUE_MIN) v = crsf::CRSF_CHANNEL_VALUE_MIN;
                if (v > crsf::CRSF_CHANNEL_VALUE_MAX) v = crsf::CRSF_CHANNEL_VALUE_MAX;
                rule.isConst = true; rule.constVal = static_cast<std::uint16_t>(v);
                parsed.push_back(rule); continue;
            }
        }

        // Sum of signed terms, each optionally scaled and/or clamped.
        std::vector<Tok> tokens;
        std::string perr;
        if (!tokenize(rhs, tokens, perr)) {
            err = "line " + std::to_string(lineNo) + ": " + perr;
            return false;
        }
        Parser parser(tokens);
        if (!parser.expr(rule)) {
            err = "line " + std::to_string(lineNo) + ": " + parser.err;
            return false;
        }
        parsed.push_back(rule);
    }

    rules_ = std::move(parsed);

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        mtime_sec_  = static_cast<long>(st.st_mtim.tv_sec);
        mtime_nsec_ = static_cast<long>(st.st_mtim.tv_nsec);
    }
    return true;
}

bool ChannelMap::reloadIfChanged(const std::string& path, std::string& err) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (static_cast<long>(st.st_mtim.tv_sec) == mtime_sec_ &&
        static_cast<long>(st.st_mtim.tv_nsec) == mtime_nsec_) {
        return false;
    }
    auto saved = rules_;            // keep current rules if the new file is bad
    if (!loadFile(path, err)) {
        rules_ = std::move(saved);
        // still stamp the mtime so we don't retry a broken file every tick
        mtime_sec_  = static_cast<long>(st.st_mtim.tv_sec);
        mtime_nsec_ = static_cast<long>(st.st_mtim.tv_nsec);
        return false;
    }
    return true;
}

void ChannelMap::apply(const JoystickState& js, crsf::RCChannels& ch) const {
    for (const Rule& r : rules_) {
        if (r.channel < 0 || r.channel >= static_cast<int>(crsf::CRSF_NUM_CHANNELS))
            continue;
        if (r.isConst) { ch.channels[r.channel] = r.constVal; continue; }

        float acc = 0.0f;
        for (const Term& t : r.terms) {
            float v = 0.0f;
            switch (t.kind) {
                case Axis:
                    if (t.idx >= 0 && t.idx < js.numAxes) v = js.axes[t.idx];
                    break;
                case Pedal: {
                    // Released pedal rests at +1 -> 0; fully pressed -1 -> 1.
                    // Default to "released" (0) when the axis is absent.
                    float a = (t.idx >= 0 && t.idx < js.numAxes) ? js.axes[t.idx] : 1.0f;
                    v = (1.0f - a) * 0.5f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    break;
                }
                case Button:
                    if (t.idx >= 0 && t.idx < js.numButtons && js.buttons[t.idx]) v = 1.0f;
                    break;
                case Const:
                    v = t.constVal;
                    break;
            }
            // Clamp the term's input range, then apply its factor.
            if (t.hasClamp) {
                if (v < t.clampLo) v = t.clampLo;
                if (v > t.clampHi) v = t.clampHi;
            }
            acc += t.coeff * v;
        }
        ch.channels[r.channel] = toChannel(acc);
    }
}

}  // namespace backend
