#ifndef WFX_HTTP_ROUTE_TRIE_HPP
#define WFX_HTTP_ROUTE_TRIE_HPP

#include "route_segment.hpp"

#include <string_view>

namespace WFX::Http {

using namespace WFX::Utils; // For 'MoveOnlyFunction', 'Logger'

struct RouteTrie {
    TrieNode root;

    // To register a new route ("/path/<id:int>", etc)
    void Insert(std::string_view fullRoute, HttpCallbackType handler);

    // To match a full route string and extract any parameters
    const HttpCallbackType* Match(std::string_view requestPath, PathSegments& outParams) const;
};

} // namespace WFX::Http

#endif // WFX_HTTP_ROUTE_TRIE_HPP