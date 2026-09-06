/**
 * @file ir_signal.cpp
 * @brief IR code `.ir` text (de)serialisation (slice-0004). Pure, native-testable.
 */

#include "ir_signal.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace adversary {
namespace ir {

namespace {

constexpr const char* MAGIC = "IR1";  // format magic + version 1

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Parse a whole decimal token into @p v; false on empty, any non-digit, or a
// value past @p max. Whole-token consumption is enforced so "12x" fails loud.
bool parseUDec(const std::string& tok, uint64_t max, uint64_t& v) {
    if (tok.empty()) return false;
    for (char c : tok) {
        if (c < '0' || c > '9') return false;
    }
    errno = 0;
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(tok.c_str(), &end, 10);
    if (errno != 0 || end != tok.c_str() + tok.size()) return false;
    if (parsed > max) return false;
    v = parsed;
    return true;
}

// Parse a whole hex token (no 0x prefix, <=16 digits) into a uint64.
bool parseUHex(const std::string& tok, uint64_t& v) {
    if (tok.empty() || tok.size() > 16) return false;
    for (char c : tok) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    errno = 0;
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(tok.c_str(), &end, 16);
    if (errno != 0 || end != tok.c_str() + tok.size()) return false;
    v = parsed;
    return true;
}

// Parse the whitespace-separated timing list into @p out; false on a malformed
// number or more than IR_MAX_TIMINGS entries.
bool parseTimings(const std::string& val, std::vector<uint16_t>& out) {
    out.clear();
    size_t p = 0;
    while (p < val.size()) {
        while (p < val.size() && (val[p] == ' ' || val[p] == '\t')) ++p;
        if (p >= val.size()) break;
        size_t q = p;
        while (q < val.size() && val[q] != ' ' && val[q] != '\t') ++q;
        uint64_t v;
        if (!parseUDec(val.substr(p, q - p), 0xFFFFull, v)) return false;
        if (out.size() >= IR_MAX_TIMINGS) return false;
        out.push_back(static_cast<uint16_t>(v));
        p = q;
    }
    return true;
}

}  // namespace

bool serializeIrSignal(const IrSignal& sig, std::string& out) {
    if (!irSignalValid(sig)) return false;

    char buf[64];
    std::string s = "IR1\n";
    if (sig.encoding == IrEncoding::Parsed) {
        s += "type: parsed\n";
        snprintf(buf, sizeof(buf), "protocol: %u\n", static_cast<unsigned>(sig.protocol));
        s += buf;
        snprintf(buf, sizeof(buf), "value: %llX\n",
                 static_cast<unsigned long long>(sig.value));
        s += buf;
        snprintf(buf, sizeof(buf), "bits: %u\n", static_cast<unsigned>(sig.bits));
        s += buf;
    } else {
        s += "type: raw\n";
        snprintf(buf, sizeof(buf), "carrier: %u\n",
                 static_cast<unsigned>(sig.carrierHz));
        s += buf;
        s += "timings:";
        for (uint16_t t : sig.timingsUs) {
            snprintf(buf, sizeof(buf), " %u", static_cast<unsigned>(t));
            s += buf;
        }
        s += "\n";
    }
    out = std::move(s);
    return true;
}

bool deserializeIrSignal(const char* data, size_t len, IrSignal& out) {
    if (!data || len == 0) return false;
    const std::string text(data, len);

    // Split into lines (keeping empty ones; they're skipped below).
    std::vector<std::string> lines;
    size_t pos = 0;
    while (true) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }

    // First non-blank line is the "IR1" magic/version header.
    size_t i = 0;
    while (i < lines.size() && trim(lines[i]).empty()) ++i;
    if (i >= lines.size() || trim(lines[i]) != MAGIC) return false;
    ++i;

    IrSignal tmp;
    bool haveType = false, isParsed = false;
    bool haveProtocol = false, haveValue = false, haveBits = false;
    bool haveCarrier = false, haveTimings = false;

    for (; i < lines.size(); ++i) {
        const std::string line = trim(lines[i]);
        if (line.empty()) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) return false;  // junk line: fail loud
        const std::string key = trim(line.substr(0, colon));
        const std::string val = trim(line.substr(colon + 1));

        if (key == "type") {
            if (val == "parsed") isParsed = true;
            else if (val == "raw") isParsed = false;
            else return false;
            haveType = true;
        } else if (key == "protocol") {
            uint64_t v;
            if (!parseUDec(val, 0xFFFFFFFFull, v)) return false;
            tmp.protocol = static_cast<uint32_t>(v);
            haveProtocol = true;
        } else if (key == "value") {
            uint64_t v;
            if (!parseUHex(val, v)) return false;
            tmp.value = v;
            haveValue = true;
        } else if (key == "bits") {
            uint64_t v;
            if (!parseUDec(val, 0xFFFFull, v)) return false;
            tmp.bits = static_cast<uint16_t>(v);
            haveBits = true;
        } else if (key == "carrier") {
            uint64_t v;
            if (!parseUDec(val, 0xFFFFFFFFull, v)) return false;
            tmp.carrierHz = static_cast<uint32_t>(v);
            haveCarrier = true;
        } else if (key == "timings") {
            if (!parseTimings(val, tmp.timingsUs)) return false;
            haveTimings = true;
        }
        // Unknown keys are ignored for forward-compatibility.
    }

    if (!haveType) return false;
    tmp.encoding = isParsed ? IrEncoding::Parsed : IrEncoding::Raw;
    if (isParsed) {
        if (!(haveProtocol && haveValue && haveBits)) return false;
    } else {
        if (!(haveCarrier && haveTimings)) return false;
    }
    if (!irSignalValid(tmp)) return false;

    out = std::move(tmp);
    return true;
}

} // namespace ir
} // namespace adversary
