//
// Created by PAWEL on 15/10/2026.
//

#include "Json.h"

namespace HTTPServer {
    std::string Json::dump(const int indent) const { // Serialize the Json value to a string with optional indentation
        std::string out; // String buffer to accumulate the serialized JSON output
        _dump(out, indent, 0); // Recursively serialize the JSON tree starting at depth 0
        return out; // Return the serialized JSON string
    }

    Json Json::parse(const std::string& text) { // Parse a JSON string into a Json object hierarchy
        return Parser(text).parseDocument(); // Construct a Parser instance and execute document parsing
    }

    bool Json::isNull()   const { return std::holds_alternative<Null>(_value); } // Return true if the value is null
    bool Json::isBool()   const { return std::holds_alternative<bool>(_value); } // Return true if the value is boolean
    bool Json::isNumber() const { return std::holds_alternative<double>(_value); } // Return true if the value is numeric
    bool Json::isString() const { return std::holds_alternative<std::string>(_value); } // Return true if the value is a string
    bool Json::isArray()  const { return std::holds_alternative<Array>(_value); } // Return true if the value is an array
    bool Json::isObject() const { return std::holds_alternative<Object>(_value); } // Return true if the value is an object

    bool Json::asBool() const                { return std::get<bool>(_value); } // Return underlying boolean value
    double Json::asNumber() const            { return std::get<double>(_value); } // Return underlying double numeric value
    const std::string& Json::asString() const { return std::get<std::string>(_value); } // Return const reference to underlying string
    const Json::Array& Json::asArray() const   { return std::get<Array>(_value); } // Return const reference to underlying Array vector
    const Json::Object& Json::asObject() const { return std::get<Object>(_value); } // Return const reference to underlying Object map

    const Json& Json::operator[](const std::string& key) const { return asObject().at(key); } // Access an object member by key
    const Json& Json::operator[](const size_t i) const         { return asArray().at(i); } // Access an array element by index

    void Json::_newline(std::string& out, const int indent, const int depth) { // Append a newline and indentation spaces based on indentation level and nesting depth
        if (indent < 0) return; // If indent is negative, formatting is compact, so do not insert newlines or indentation
        out += '\n'; // Append a newline character to break the line
        out.append(static_cast<size_t>(indent) * depth, ' '); // Append the required number of spaces corresponding to the current depth level
    }

    std::string Json::_numberToString(const double n) { // Format a double value into an accurate JSON-compliant string representation
        if (!std::isfinite(n)) return "null"; // JSON does not support NaN or Infinity; replace with null per standard practice
        if (n == std::floor(n) && std::fabs(n) < 1e15) // Check if the number is an integer representable within standard integer limits
            return std::to_string(static_cast<long long>(n)); // Format directly as an integer string without decimals
        char buf[32]; // Fixed-size character buffer for snprintf float formatting
        std::snprintf(buf, sizeof(buf), "%.15g", n); // Format with 15 significant digits (standard double precision)
        if (std::strtod(buf, nullptr) != n) // If 15 digits lost precision on round-trip conversion back to double
            std::snprintf(buf, sizeof(buf), "%.17g", n); // Re-format with 17 significant digits to guarantee lossless round-trip precision
        return buf; // Return the formatted numeric string
    }

    void Json::_escape(const std::string& s, std::string& out) { // Escape special characters and control codes in a string according to JSON specification
        out += '"'; // Prepend opening double quotation mark
        for (const unsigned char c : s) { // Iterate over each byte in the string
            switch (c) { // Check for characters requiring backslash escape sequences
                case '"':  out += "\\\""; break; // Escape double quotation mark
                case '\\': out += "\\\\"; break; // Escape reverse solidus (backslash)
                case '\b': out += "\\b";  break; // Escape backspace control character
                case '\f': out += "\\f";  break; // Escape form feed control character
                case '\n': out += "\\n";  break; // Escape line feed (newline) control character
                case '\r': out += "\\r";  break; // Escape carriage return control character
                case '\t': out += "\\t";  break; // Escape horizontal tab control character
                default:
                    if (c < 0x20) { // Check if character is an unprintable ASCII control character (< 0x20)
                        char buf[7]; // Buffer for 6-character "\u00XX" escape sequence plus null terminator
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c); // Format the control character as a 4-digit hexadecimal unicode escape
                        out += buf; // Append the unicode escape sequence to the output string
                    } else {
                        out += static_cast<char>(c); // Append regular printable character directly
                    }
            }
        }
        out += '"'; // Append closing double quotation mark
    }

    void Json::_dump(std::string& out, const int indent, const int depth) const { // Recursively serialize this Json value to JSON text at the given nesting depth
        if (std::holds_alternative<Null>(_value)) { // Check if value is null
            out += "null"; // Append literal null to output
        } else if (auto* b = std::get_if<bool>(&_value)) { // Check if value is boolean
            out += *b ? "true" : "false"; // Append boolean literal ("true" or "false")
        } else if (auto* n = std::get_if<double>(&_value)) { // Check if value is numeric
            out += _numberToString(*n); // Append formatted numeric string
        } else if (auto* s = std::get_if<std::string>(&_value)) { // Check if value is a string
            _escape(*s, out); // Escape and append quoted string
        } else if (auto* a = std::get_if<Array>(&_value)) { // Check if value is an array
            if (a->empty()) { out += "[]"; return; } // Handle empty array directly without linebreaks
            out += '['; // Append opening square bracket
            bool first = true; // Flag to track first element for comma separation
            for (const auto& item : *a) { // Iterate over all elements in the array
                if (!first) out += ','; // Prepend comma before every element except the first
                first = false; // Mark first element as processed
                _newline(out, indent, depth + 1); // Append newline and indentation for the current element
                item._dump(out, indent, depth + 1); // Recursively dump the element at the increased depth level
            }
            _newline(out, indent, depth); // Append newline and indentation matching the closing bracket depth
            out += ']'; // Append closing square bracket
        } else if (auto* o = std::get_if<Object>(&_value)) { // Check if value is an object
            if (o->empty()) { out += "{}"; return; } // Handle empty object directly without linebreaks
            out += '{'; // Append opening curly brace
            bool first = true; // Flag to track first entry for comma separation
            for (const auto& [key, value] : *o) { // Iterate over all key-value pairs in the object
                if (!first) out += ','; // Prepend comma before every key-value pair except the first
                first = false; // Mark first pair as processed
                _newline(out, indent, depth + 1); // Append newline and indentation for the current key-value pair
                _escape(key, out); // Escape and output the object key as a quoted JSON string
                out += indent >= 0 ? ": " : ":"; // Append colon separator with trailing space if pretty-printing
                value._dump(out, indent, depth + 1); // Recursively dump the associated value at the increased depth level
            }
            _newline(out, indent, depth); // Append newline and indentation matching the closing brace depth
            out += '}'; // Append closing curly brace
        }
    }

    Json::Parser::Parser(const std::string& s) : _s(s) {} // Construct parser referencing the input JSON string

    Json Json::Parser::parseDocument() { // Parse the entire JSON document from beginning to end
        Json v = _parseValue(0); // Parse the top-level JSON value starting at recursion depth 0
        _skipWs(); // Skip any trailing whitespace characters following the parsed value
        if (_pos != _s.size()) _fail("unexpected trailing characters"); // If input contains unparsed characters after the root value, raise an error
        return v; // Return the successfully parsed root JSON value
    }

    void Json::Parser::_fail(const std::string& msg) const { // Throw a parse error with character position context
        throw std::runtime_error("JSON parse error at position " + std::to_string(_pos) + ": " + msg); // Form and throw runtime_error detailing position and message
    }

    bool Json::Parser::_isDigit(const char c) { return c >= '0' && c <= '9'; } // Return true if character is an ASCII digit between '0' and '9'

    void Json::Parser::_skipWs() { // Advance the cursor past any whitespace characters
        while (_pos < _s.size() && (_s[_pos] == ' ' || _s[_pos] == '\t' || _s[_pos] == '\n' || _s[_pos] == '\r')) // Loop while current char is a space, tab, newline, or carriage return
            ++_pos; // Advance position index
    }

    void Json::Parser::_consume(const char expected) { // Skip whitespace and ensure next character matches expected character
        _skipWs(); // Advance past any preceding whitespace
        if (_pos >= _s.size() || _s[_pos] != expected) // Check if end of input reached or character does not match
            _fail(std::string("expected '") + expected + "'"); // Fail with an error reporting expected character
        ++_pos; // Advance cursor past the matched character
    }

    void Json::Parser::_expectWord(const char* word) { // Match and consume an exact keyword literal
        const size_t len = std::strlen(word); // Determine length of expected keyword
        if (_s.compare(_pos, len, word) != 0) _fail("invalid literal"); // Compare substring at current cursor; fail if mismatch
        _pos += len; // Advance cursor past the keyword length
    }

    Json Json::Parser::_parseValue(const int depth) { // Parse any JSON value according to the starting token character
        if (depth > MAX_DEPTH) _fail("nesting too deep"); // Enforce recursion limit to prevent stack overflow
        _skipWs(); // Skip leading whitespace
        if (_pos >= _s.size()) _fail("unexpected end of input"); // Fail if end of input reached before value
        switch (const char c = _s[_pos]) { // Inspect next character to determine value type
            case '{': return _parseObject(depth); // Parse JSON object starting with '{'
            case '[': return _parseArray(depth); // Parse JSON array starting with '['
            case '"': return Json(_parseString()); // Parse JSON string starting with '"'
            case 't': _expectWord("true");  return Json(true); // Parse boolean true literal
            case 'f': _expectWord("false"); return Json(false); // Parse boolean false literal
            case 'n': _expectWord("null");  return Json(nullptr); // Parse null literal
            default:
                if (c == '-' || _isDigit(c)) return _parseNumber(); // Parse numeric literal if starting with minus or digit
                _fail("unexpected character"); // Throw error for unrecognized character
        }
    }

    Json Json::Parser::_parseObject(const int depth) { // Parse a JSON object into an Object key-value map
        ++_pos; // Consume the opening '{' brace
        Object obj; // Map to store parsed object key-value pairs
        _skipWs(); // Skip whitespace following opening brace
        if (_pos < _s.size() && _s[_pos] == '}') { ++_pos; return Json(std::move(obj)); } // Handle empty object "{}" immediately
        while (true) { // Loop to parse comma-separated key-value pairs
            _skipWs(); // Skip whitespace before key
            if (_pos >= _s.size() || _s[_pos] != '"') _fail("expected string key"); // Ensure member name begins with a double quote
            std::string key = _parseString(); // Parse member name string
            _consume(':'); // Consume colon separator between key and value
            obj[std::move(key)] = _parseValue(depth + 1); // Parse member value recursively and insert into object map
            _skipWs(); // Skip whitespace after value
            if (_pos < _s.size() && _s[_pos] == ',') { ++_pos; continue; } // If comma follows, consume it and continue parsing pairs
            _consume('}'); // Otherwise, consume closing '}' brace
            break; // Exit loop once closing brace is consumed
        }
        return Json(std::move(obj)); // Return parsed object wrapped in Json value
    }

    Json Json::Parser::_parseArray(const int depth) { // Parse a JSON array into an Array vector
        ++_pos; // Consume the opening '[' bracket
        Array arr; // Vector to store parsed array elements
        _skipWs(); // Skip whitespace following opening bracket
        if (_pos < _s.size() && _s[_pos] == ']') { ++_pos; return Json(std::move(arr)); } // Handle empty array "[]" immediately
        while (true) { // Loop to parse comma-separated array items
            arr.push_back(_parseValue(depth + 1)); // Parse array item value recursively and append to vector
            _skipWs(); // Skip whitespace after item
            if (_pos < _s.size() && _s[_pos] == ',') { ++_pos; continue; } // If comma follows, consume it and continue parsing items
            _consume(']'); // Otherwise, consume closing ']' bracket
            break; // Exit loop once closing bracket is consumed
        }
        return Json(std::move(arr)); // Return parsed array wrapped in Json value
    }

    unsigned Json::Parser::_parseHex4() { // Parse exactly 4 hexadecimal characters into an unsigned integer
        if (_pos + 4 > _s.size()) _fail("incomplete \\u escape"); // Fail if fewer than 4 characters remain in input
        unsigned v = 0; // Accumulator for the 16-bit hex value
        for (int i = 0; i < 4; ++i) { // Process 4 hexadecimal digits
            const char c = _s[_pos++]; // Read next digit character and advance cursor
            v <<= 4; // Shift accumulated value left by 4 bits
            if      (c >= '0' && c <= '9') v |= c - '0'; // Add numeric value of digit ('0'-'9')
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; // Add value of lowercase hex letter ('a'-'f')
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; // Add value of uppercase hex letter ('A'-'F')
            else _fail("invalid hex digit in \\u escape"); // Fail if character is not a valid hex digit
        }
        return v; // Return decoded 16-bit unsigned integer value
    }

    void Json::Parser::_appendUtf8(std::string& out, const unsigned cp) { // Encode a Unicode code point as UTF-8 bytes and append to the output string
        if (cp < 0x80) { // Code points 0x0000 - 0x007F: encode as 1 byte
            out += static_cast<char>(cp); // Direct 7-bit ASCII byte
        } else if (cp < 0x800) { // Code points 0x0080 - 0x07FF: encode as 2 bytes
            out += static_cast<char>(0xC0 | (cp >> 6)); // Leading byte: 110xxxxx
            out += static_cast<char>(0x80 | (cp & 0x3F)); // Continuation byte: 10xxxxxx
        } else if (cp < 0x10000) { // Code points 0x0800 - 0xFFFF: encode as 3 bytes
            out += static_cast<char>(0xE0 | (cp >> 12)); // Leading byte: 1110xxxx
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); // First continuation byte: 10xxxxxx
            out += static_cast<char>(0x80 | (cp & 0x3F)); // Second continuation byte: 10xxxxxx
        } else { // Code points 0x10000 - 0x10FFFF: encode as 4 bytes
            out += static_cast<char>(0xF0 | (cp >> 18)); // Leading byte: 11110xxx
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); // First continuation byte: 10xxxxxx
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); // Second continuation byte: 10xxxxxx
            out += static_cast<char>(0x80 | (cp & 0x3F)); // Third continuation byte: 10xxxxxx
        }
    }

    std::string Json::Parser::_parseString() { // Parse a quoted JSON string with escape sequence handling
        ++_pos; // Consume opening '"' quotation mark
        std::string out; // String buffer to accumulate decoded characters
        while (true) { // Loop until closing quotation mark is encountered
            if (_pos >= _s.size()) _fail("unterminated string"); // Fail if end of input is reached before closing quote
            const unsigned char c = static_cast<unsigned char>(_s[_pos++]); // Read current character and advance cursor
            if (c == '"') return out; // Return decoded string upon encountering closing quote
            if (c < 0x20) _fail("unescaped control character in string"); // Unescaped control characters (< 0x20) are forbidden in JSON strings
            if (c != '\\') { out += static_cast<char>(c); continue; } // Append unescaped regular character and continue
            if (_pos >= _s.size()) _fail("unterminated escape"); // Fail if backslash is at the end of input
            switch (_s[_pos++]) { // Process escape sequence character
                case '"':  out += '"';  break; // Escaped quotation mark
                case '\\': out += '\\'; break; // Escaped backslash
                case '/':  out += '/';  break; // Escaped forward slash
                case 'b':  out += '\b'; break; // Escaped backspace
                case 'f':  out += '\f'; break; // Escaped form feed
                case 'n':  out += '\n'; break; // Escaped line feed
                case 'r':  out += '\r'; break; // Escaped carriage return
                case 't':  out += '\t'; break; // Escaped horizontal tab
                case 'u': { // Unicode escape sequence \uXXXX
                    unsigned cp = _parseHex4(); // Parse 4-digit hexadecimal code point
                    if (cp >= 0xD800 && cp <= 0xDBFF) { // Check if code point is a UTF-16 high surrogate
                        if (_s.compare(_pos, 2, "\\u") != 0) _fail("lone high surrogate"); // High surrogate must be followed by low surrogate "\u"
                        _pos += 2; // Consume "\u" prefix
                        const unsigned lo = _parseHex4(); // Parse 4-digit hex for low surrogate
                        if (lo < 0xDC00 || lo > 0xDFFF) _fail("invalid low surrogate"); // Validate low surrogate range (0xDC00 - 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); // Decode surrogate pair into Unicode scalar value
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) { // Unpaired low surrogate encountered
                        _fail("lone low surrogate"); // Fail on invalid lone low surrogate
                    }
                    _appendUtf8(out, cp); // Encode code point to UTF-8 and append to output string
                    break;
                }
                default: _fail("invalid escape sequence"); // Fail on unrecognized escape character
            }
        }
    }

    void Json::Parser::_requireDigits() { // Ensure at least one digit is present at current position and consume all contiguous digits
        if (_pos >= _s.size() || !_isDigit(_s[_pos])) _fail("expected digit"); // Fail if current character is not an ASCII digit
        while (_pos < _s.size() && _isDigit(_s[_pos])) ++_pos; // Consume all following contiguous digit characters
    }

    Json Json::Parser::_parseNumber() { // Parse a JSON number literal into a numeric Json instance
        const size_t start = _pos; // Record starting index of the numeric literal
        if (_s[_pos] == '-') ++_pos; // Consume leading minus sign if present
        if (_pos < _s.size() && _s[_pos] == '0') ++_pos; // Leading zero (cannot be followed by digits without decimal point)
        else _requireDigits(); // Non-zero number must start with one or more digits
        if (_pos < _s.size() && _s[_pos] == '.') { ++_pos; _requireDigits(); } // Parse optional fractional part starting with '.' followed by digits
        if (_pos < _s.size() && (_s[_pos] == 'e' || _s[_pos] == 'E')) { // Parse optional exponent part starting with 'e' or 'E'
            ++_pos; // Consume 'e' or 'E'
            if (_pos < _s.size() && (_s[_pos] == '+' || _s[_pos] == '-')) ++_pos; // Consume optional '+' or '-' sign in exponent
            _requireDigits(); // Consume required exponent digits
        }
        return Json(std::strtod(_s.substr(start, _pos - start).c_str(), nullptr)); // Convert numeric substring to double and return as Json value
    }
} // HTTPServer
