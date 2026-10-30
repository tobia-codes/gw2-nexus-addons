#pragma once
// Text.h — PURE text helpers for API / wiki strings: game and wiki markup to plain text,
// UTF-8-safe truncation.

#include <cstddef>
#include <string>

namespace Trailpack { namespace Core { namespace Text {

// Game markup ("<c=@flavor>...</c>", "<br>") to plain text: tags dropped, <br> -> '\n',
// surrounding whitespace trimmed.
std::string StripGameMarkup(const std::string& s);

// One wiki line to plain text: HTML tags (and <sup>...</sup> notes) dropped, [[Link|Label]] ->
// Label, [[Link]] -> Link, [[File:...]] / [[Category:...]] dropped, '''bold''' / ''italic''
// marks dropped, common entities decoded (&nbsp; &amp; &lt; &gt; &quot; &#39;), a leading ':'
// bullet and repeated whitespace removed.
std::string StripWikiMarkup(const std::string& s);

// At most `maxBytes` bytes of `s`, cut only at a UTF-8 character boundary.
std::string TruncateUtf8(const std::string& s, size_t maxBytes);

// Trim spaces, tabs and line breaks at both ends.
std::string Trim(const std::string& s);

}}} // namespace Trailpack::Core::Text
