#include "common/vectors.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mc::test {

namespace {

bool isBlank(std::string_view line) {
    for (char c : line) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Splits `text` on '\n', stripping a trailing '\r' from each line so CRLF and LF files parse
// identically (git.autocrlf on this project's Windows checkout produces CRLF).
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    for (size_t k = 0; k <= text.size(); ++k) {
        if (k == text.size() || text[k] == '\n') {
            std::string_view line = text.substr(start, k - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            lines.push_back(line);
            start = k + 1;
        }
    }
    return lines;
}

std::vector<std::string_view> splitWhitespace(std::string_view s) {
    std::vector<std::string_view> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i > start) {
            tokens.push_back(s.substr(start, i - start));
        }
    }
    return tokens;
}

// A metadata "key:" token: an identifier (letter/underscore then alnum/underscore) immediately
// followed by ':', nothing else.
bool isKeyToken(std::string_view tok) {
    if (tok.size() < 2 || tok.back() != ':') {
        return false;
    }
    std::string_view ident = tok.substr(0, tok.size() - 1);
    if (ident.empty() || !(std::isalpha(static_cast<unsigned char>(ident[0])) || ident[0] == '_')) {
        return false;
    }
    for (char c : ident) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
            return false;
        }
    }
    return true;
}

bool allDigits(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

// `<NAME>` -> control byte, per the reference spec's debug-log notation (§6, "control codes
// shown as <STX>, <ETX>, <ENQ>, <ACK>, <NAK>, <CR>, <LF>, <DLE>").
struct ControlName {
    std::string_view name;
    uint8_t byte;
};
constexpr ControlName kControlNames[] = {
    {"STX", 0x02}, {"ETX", 0x03}, {"ENQ", 0x05}, {"ACK", 0x06},
    {"NAK", 0x15}, {"CR", 0x0D},  {"LF", 0x0A},  {"DLE", 0x10},
};

[[noreturn]] void fail(std::string_view file, int line, const std::string& reason) {
    throw VecFormatError(std::string(file) + ":" + std::to_string(line) + ": " + reason);
}

std::vector<uint8_t> decodeHexLine(std::string_view rawLine, std::string_view file, int lineNo) {
    std::string digits;
    digits.reserve(rawLine.size());
    for (char c : rawLine) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        }
    }
    if (digits.size() % 2 != 0) {
        fail(file, lineNo,
             "odd number of hex digits (" + std::to_string(digits.size()) + ")");
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(digits.size() / 2);
    for (size_t k = 0; k < digits.size(); k += 2) {
        int hi = hexDigit(digits[k]);
        int lo = hexDigit(digits[k + 1]);
        if (hi < 0 || lo < 0) {
            fail(file, lineNo, "invalid hex digit in '" + digits + "'");
        }
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

// Decodes an ASCII text line into the bytes it stands for: a `<NAME>` tag becomes its control
// byte, every other character becomes its own ASCII byte. Throws on an unrecognized tag (VEC-01
// negative: "unknown control name").
std::vector<uint8_t> decodeControlText(std::string_view text, std::string_view file, int lineNo) {
    std::vector<uint8_t> out;
    out.reserve(text.size());
    size_t k = 0;
    while (k < text.size()) {
        if (text[k] == '<') {
            size_t close = text.find('>', k + 1);
            if (close == std::string_view::npos) {
                fail(file, lineNo, "unterminated control name starting at '" +
                                        std::string(text.substr(k)) + "'");
            }
            std::string_view name = text.substr(k + 1, close - k - 1);
            const ControlName* found = nullptr;
            for (const auto& c : kControlNames) {
                if (c.name == name) {
                    found = &c;
                    break;
                }
            }
            if (found == nullptr) {
                fail(file, lineNo, "unknown control name '<" + std::string(name) + ">'");
            }
            out.push_back(found->byte);
            k = close + 1;
        } else {
            out.push_back(static_cast<uint8_t>(text[k]));
            ++k;
        }
    }
    return out;
}

// Parses one metadata line's content (the part after '#', trimmed) into "key: value" fields.
// Several keys may share a line (module spec example); a value runs from just after its key
// token to just before the next key token or end of line. Throws on a malformed `bytes:` value
// (VEC-01 negative) or a token that is not a recognized "key:" where one is expected.
void parseMetadataLine(std::string_view content, std::vector<VecField>& fields,
                        std::string_view file, int lineNo) {
    std::vector<std::string_view> tokens = splitWhitespace(content);
    size_t k = 0;
    while (k < tokens.size()) {
        if (!isKeyToken(tokens[k])) {
            fail(file, lineNo,
                 "expected 'key:' before '" + std::string(tokens[k]) + "'");
        }
        std::string key(tokens[k].substr(0, tokens[k].size() - 1));
        ++k;
        std::string value;
        while (k < tokens.size() && !isKeyToken(tokens[k])) {
            if (!value.empty()) {
                value += ' ';
            }
            value += std::string(tokens[k]);
            ++k;
        }
        if (key == "bytes" && !allDigits(value)) {
            fail(file, lineNo, "invalid bytes value '" + value + "'");
        }
        fields.push_back(VecField{std::move(key), std::move(value)});
    }
}

std::string firstFieldValue(const std::vector<VecField>& fields, std::string_view key) {
    for (const auto& f : fields) {
        if (f.key == key) {
            return f.value;
        }
    }
    return {};
}

std::vector<std::string> splitTags(const std::string& value) {
    std::vector<std::string> tags;
    std::string current;
    for (char c : value) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tags.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tags.push_back(current);
    }
    return tags;
}

} // namespace

std::string Vector::field(std::string_view key) const { return firstFieldValue(fields, key); }

bool Vector::hasTag(std::string_view tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::vector<Vector> parseVectors(std::string_view text, std::string_view fileForMessages) {
    std::vector<Vector> result;
    std::vector<std::string_view> lines = splitLines(text);
    size_t n = lines.size();
    size_t i = 0;

    while (i < n) {
        while (i < n && isBlank(lines[i])) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        if (lines[i][0] != '#') {
            fail(fileForMessages, static_cast<int>(i + 1),
                 "hex line without an id (no preceding '# id:' metadata)");
        }

        int blockLine = static_cast<int>(i + 1);
        std::vector<VecField> fields;
        while (i < n && !lines[i].empty() && lines[i][0] == '#') {
            std::string_view content = trim(lines[i].substr(1));
            parseMetadataLine(content, fields, fileForMessages, static_cast<int>(i + 1));
            ++i;
        }

        if (i >= n || isBlank(lines[i]) || lines[i][0] == '#') {
            fail(fileForMessages, blockLine, "record has no hex data line");
        }

        std::string id = firstFieldValue(fields, "id");
        if (id.empty()) {
            fail(fileForMessages, static_cast<int>(i + 1), "hex line without an id");
        }

        std::vector<uint8_t> bytes =
            decodeHexLine(lines[i], fileForMessages, static_cast<int>(i + 1));
        ++i;

        std::string textLine;
        bool hasText = false;
        if (i < n && !isBlank(lines[i]) && lines[i][0] != '#') {
            hasText = true;
            textLine = std::string(lines[i]);
            std::vector<uint8_t> decoded =
                decodeControlText(textLine, fileForMessages, static_cast<int>(i + 1));
            if (decoded != bytes) {
                fail(fileForMessages, static_cast<int>(i + 1),
                     "ASCII text line does not match the hex line");
            }
            ++i;
        }

        Vector v;
        v.id = std::move(id);
        v.fields = std::move(fields);
        v.bytes = std::move(bytes);
        v.text = std::move(textLine);
        v.hasText = hasText;
        v.file = std::string(fileForMessages);
        v.line = blockLine;
        for (const auto& f : v.fields) {
            if (f.key == "tags") {
                auto split = splitTags(f.value);
                v.tags.insert(v.tags.end(), split.begin(), split.end());
            }
        }
        result.push_back(std::move(v));
    }

    return result;
}

std::vector<Vector> loadVectors(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open vector file: " + file.generic_string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseVectors(ss.str(), file.generic_string());
}

std::vector<std::filesystem::path> findVectorFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".vec") {
            continue;
        }
        bool underCaptured = false;
        for (const auto& part : entry.path()) {
            if (part == "captured") {
                underCaptured = true;
                break;
            }
        }
        if (!underCaptured) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<Vector> loadAllVectors(const std::filesystem::path& root) {
    std::vector<Vector> all;
    for (const auto& file : findVectorFiles(root)) {
        std::vector<Vector> vectors = loadVectors(file);
        all.insert(all.end(), std::make_move_iterator(vectors.begin()),
                   std::make_move_iterator(vectors.end()));
    }
    return all;
}

} // namespace mc::test
