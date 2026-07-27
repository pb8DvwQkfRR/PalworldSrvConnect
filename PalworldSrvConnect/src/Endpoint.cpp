#include "Endpoint.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace PalworldSrvConnect
{
    namespace
    {
        auto IsSpace(const wchar_t value) -> bool
        {
            return std::iswspace(static_cast<wint_t>(value)) != 0;
        }

        auto Trim(std::wstring_view input) -> std::wstring
        {
            while (!input.empty() && IsSpace(input.front())) input.remove_prefix(1);
            while (!input.empty() && IsSpace(input.back())) input.remove_suffix(1);
            return std::wstring{input};
        }

        auto ParsePort(std::wstring_view input) -> std::optional<std::uint16_t>
        {
            if (input.empty()) return std::nullopt;

            std::uint32_t value = 0;
            for (const wchar_t character : input)
            {
                if (character < L'0' || character > L'9') return std::nullopt;
                value = value * 10u + static_cast<std::uint32_t>(character - L'0');
                if (value > std::numeric_limits<std::uint16_t>::max()) return std::nullopt;
            }
            if (value == 0) return std::nullopt;
            return static_cast<std::uint16_t>(value);
        }

        auto IsIpv4(std::wstring_view input) -> bool
        {
            int segments = 0;
            std::uint32_t value = 0;
            int digits = 0;

            for (std::size_t i = 0; i <= input.size(); ++i)
            {
                const wchar_t character = i < input.size() ? input[i] : L'.';
                if (character >= L'0' && character <= L'9')
                {
                    value = value * 10u + static_cast<std::uint32_t>(character - L'0');
                    ++digits;
                    if (digits > 3 || value > 255) return false;
                }
                else if (character == L'.')
                {
                    if (digits == 0) return false;
                    ++segments;
                    value = 0;
                    digits = 0;
                }
                else
                {
                    return false;
                }
            }
            return segments == 4;
        }

        auto LooksLikeIpv6(std::wstring_view input) -> bool
        {
            if (input.find(L':') == std::wstring_view::npos) return false;
            for (const wchar_t character : input)
            {
                if (std::iswxdigit(static_cast<wint_t>(character)) != 0 || character == L':' || character == L'.' || character == L'%')
                {
                    continue;
                }
                if (std::iswalnum(static_cast<wint_t>(character)) != 0 || character == L'_' || character == L'-')
                {
                    continue; // IPv6 zone identifier.
                }
                return false;
            }
            return true;
        }

        auto IsExplicitSrvName(std::wstring_view host) -> bool
        {
            if (host.empty() || host.front() != L'_') return false;
            std::wstring lower{host};
            std::transform(lower.begin(), lower.end(), lower.begin(), [](const wchar_t value) {
                return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
            });
            return lower.find(L"._udp.") != std::wstring::npos || lower.find(L"._tcp.") != std::wstring::npos;
        }
    }

    auto ParseEndpoint(std::wstring_view input) -> std::optional<ParsedEndpoint>
    {
        auto value = Trim(input);
        if (value.empty()) return std::nullopt;
        if (value.find(L"://") != std::wstring::npos) return std::nullopt;

        ParsedEndpoint endpoint{};

        if (!value.empty() && value.front() == L'[')
        {
            const auto close = value.find(L']');
            if (close == std::wstring::npos) return std::nullopt;
            endpoint.host = Trim(std::wstring_view{value}.substr(1, close - 1));

            const auto suffix = std::wstring_view{value}.substr(close + 1);
            if (!suffix.empty())
            {
                if (suffix.front() != L':') return std::nullopt;
                endpoint.port = ParsePort(suffix.substr(1));
                if (!endpoint.port) return std::nullopt;
            }
        }
        else
        {
            const auto colon_count = static_cast<std::size_t>(std::count(value.begin(), value.end(), L':'));
            if (colon_count == 1)
            {
                const auto colon = value.rfind(L':');
                endpoint.host = Trim(std::wstring_view{value}.substr(0, colon));
                endpoint.port = ParsePort(std::wstring_view{value}.substr(colon + 1));
                if (!endpoint.port) return std::nullopt;
            }
            else
            {
                endpoint.host = Trim(value);
            }
        }

        if (endpoint.host.empty()) return std::nullopt;
        return endpoint;
    }

    auto IsIpLiteral(std::wstring_view host) -> bool
    {
        return IsIpv4(host) || LooksLikeIpv6(host);
    }

    auto ShouldAttemptSrv(const ParsedEndpoint& endpoint) -> bool
    {
        if (endpoint.host.empty() || IsIpLiteral(endpoint.host)) return false;
        if (endpoint.port.has_value()) return false;
        return endpoint.host.find(L'.') != std::wstring::npos;
    }

    auto BuildSrvQuery(std::wstring_view host) -> std::wstring
    {
        if (IsExplicitSrvName(host)) return std::wstring{host};

        std::wstring query{L"_palworld._udp."};
        query.append(host);
        return query;
    }

    auto FormatEndpoint(std::wstring_view host, const std::uint16_t port) -> std::wstring
    {
        std::wstring result{};
        if (host.find(L':') != std::wstring_view::npos && !(host.starts_with(L'[') && host.ends_with(L']')))
        {
            result.push_back(L'[');
            result.append(host);
            result.push_back(L']');
        }
        else
        {
            result.append(host);
        }
        result.push_back(L':');
        result.append(std::to_wstring(port));
        return result;
    }

    auto ChooseSrvRecord(const std::vector<SrvRecord>& records, const std::uint32_t ticket) -> std::optional<std::size_t>
    {
        if (records.empty()) return std::nullopt;

        std::uint16_t minimum_priority = std::numeric_limits<std::uint16_t>::max();
        for (const auto& record : records) minimum_priority = std::min(minimum_priority, record.priority);

        std::vector<std::size_t> candidates{};
        std::uint64_t total_weight = 0;
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (records[i].priority != minimum_priority) continue;
            candidates.push_back(i);
            total_weight += records[i].weight;
        }

        if (candidates.empty()) return std::nullopt;
        if (total_weight == 0) return candidates[ticket % candidates.size()];

        const auto selected_weight = static_cast<std::uint64_t>(ticket) % total_weight;
        std::uint64_t cursor = 0;
        for (const auto index : candidates)
        {
            cursor += records[index].weight;
            if (selected_weight < cursor) return index;
        }
        return candidates.back();
    }
}
