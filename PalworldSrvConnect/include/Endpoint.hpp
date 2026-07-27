#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PalworldSrvConnect
{
    struct ParsedEndpoint
    {
        std::wstring host{};
        std::optional<std::uint16_t> port{};
    };

    struct SrvRecord
    {
        std::wstring target{};
        std::uint16_t port{};
        std::uint16_t priority{};
        std::uint16_t weight{};
        std::uint32_t ttl{};
    };

    auto ParseEndpoint(std::wstring_view input) -> std::optional<ParsedEndpoint>;
    auto IsIpLiteral(std::wstring_view host) -> bool;
    auto ShouldAttemptSrv(const ParsedEndpoint& endpoint) -> bool;
    auto BuildSrvQuery(std::wstring_view host) -> std::wstring;
    auto FormatEndpoint(std::wstring_view host, std::uint16_t port) -> std::wstring;

    // RFC 2782-style selection: lowest priority first, then weighted choice.
    // The caller supplies a random ticket to keep this function deterministic in tests.
    auto ChooseSrvRecord(const std::vector<SrvRecord>& records, std::uint32_t ticket) -> std::optional<std::size_t>;
}
