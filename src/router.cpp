#include "router.hpp"

#include <cassert>

void Router::route(
    std::string_view pattern, std::function<Response(const Request&, const RouteParams&)> handler)
{
    routes_.push_back(Route { Route::Pattern::parse(pattern), Method::Get, std::move(handler) });
}

void Router::route(std::string_view pattern, Method method,
    std::function<Response(const Request&, const RouteParams&)> handler)
{
    routes_.push_back(Route { Route::Pattern::parse(pattern), method, std::move(handler) });
}

Response Router::operator()(const Request& request) const
{
    for (const auto& route : routes_) {
        const auto params = route.pattern.match(request.url.path);
        if (params) {
            auto resp = route.handler(request, *params);
            if (resp.body.size() > 0) {
                resp.headers.set("Content-Length", std::to_string(resp.body.size()));
            }
            return resp;
        }
    }
    // No matching route
    return Response(StatusCode::NotFound, "Not Found");
}

Router::Route::Pattern Router::Route::Pattern::parse(std::string_view str)
{
    Pattern pattern { std::string(str), {} };
    for (const auto& part : split(str, '/')) {
        if (!part.empty() && part[0] == ':') {
            if (part.back() == '*') {
                pattern.parts.push_back(
                    Part { Part::Type::PlaceholderPath, part.substr(1, part.size() - 2) });
            } else {
                pattern.parts.push_back(Part { Part::Type::Placeholder, part.substr(1) });
            }
        } else {
            pattern.parts.push_back(Part { Part::Type::Literal, part });
        }
    }
    return pattern;
}

std::optional<Router::RouteParams> Router::Route::Pattern::match(std::string_view urlPath) const
{
    size_t cursor = 0;
    std::unordered_map<std::string_view, std::string_view> params;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].type == Part::Type::Literal || parts[i].type == Part::Type::Placeholder) {
            const auto slash = std::min(urlPath.find('/', cursor), urlPath.size());
            const auto urlPart = urlPath.substr(cursor, slash - cursor);
            if (parts[i].type == Part::Type::Literal) {
                if (parts[i].str != urlPart) {
                    return std::nullopt;
                }
            } else {
                assert(parts[i].type == Part::Type::Placeholder);
                params[parts[i].str] = urlPart;
            }
            // We have reached the end of urlPath, but there are pattern parts left
            if (cursor >= urlPath.size() && i < parts.size() - 1) {
                return std::nullopt;
            }
            cursor = slash + 1;
        } else {
            assert(parts[i].type == Part::Type::PlaceholderPath);
            params[parts[i].str] = urlPath.substr(cursor);
            return params;
        }
    }
    // Not the whole urlPath has been consumed => no complete match
    if (cursor < urlPath.size()) {
        return std::nullopt;
    }
    return params;
}