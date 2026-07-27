#include "SrvResolver.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <limits>
#include <vector>

namespace PalworldSrvConnect
{
    namespace
    {
        constexpr std::uint32_t negative_cache_ttl_seconds = 30;

        auto Lowercase(std::wstring_view input) -> std::wstring
        {
            std::wstring result{input};
            std::transform(result.begin(), result.end(), result.begin(), [](const wchar_t value) {
                return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
            });
            return result;
        }

        auto NormalizeDnsName(std::wstring value) -> std::wstring
        {
            if (value == L".") return value;
            while (!value.empty() && value.back() == L'.') value.pop_back();
            return value;
        }
    }

    SrvResolver::SrvResolver() : m_rng{std::random_device{}()}
    {
        WSADATA data{};
        m_winsock_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    SrvResolver::~SrvResolver()
    {
        if (m_winsock_ready) WSACleanup();
    }

    auto SrvResolver::ResolveInput(const std::wstring_view input) -> Resolution
    {
        const auto endpoint = ParseEndpoint(input);
        if (!endpoint || !ShouldAttemptSrv(*endpoint)) return {};
        return ResolveHost(endpoint->host);
    }

    auto SrvResolver::ResolveHost(const std::wstring_view host, const bool) -> Resolution
    {
        const auto endpoint = ParseEndpoint(host);
        if (!endpoint || !ShouldAttemptSrv(*endpoint)) return {};

        const auto query = BuildSrvQuery(endpoint->host);
        if (auto cached = FindCached(query)) return *cached;

        auto [resolution, ttl] = Query(query);
        PutCached(query, resolution, ttl);
        return resolution;
    }

    auto SrvResolver::Query(const std::wstring_view query) -> std::pair<Resolution, std::uint32_t>
    {
        Resolution resolution{};
        resolution.query = std::wstring{query};

        PDNS_RECORDW record_list = nullptr;
        const DNS_STATUS status = DnsQuery_W(resolution.query.c_str(), DNS_TYPE_SRV, DNS_QUERY_STANDARD, nullptr, &record_list, nullptr);
        if (status != ERROR_SUCCESS)
        {
            resolution.status = (status == DNS_INFO_NO_RECORDS || status == DNS_ERROR_RCODE_NAME_ERROR)
                                        ? ResolutionStatus::NotFound
                                        : ResolutionStatus::QueryFailed;
            return {resolution, negative_cache_ttl_seconds};
        }

        std::vector<SrvRecord> records{};
        std::uint32_t minimum_ttl = std::numeric_limits<std::uint32_t>::max();
        for (auto* record = record_list; record != nullptr; record = record->pNext)
        {
            if (record->wType != DNS_TYPE_SRV || record->Data.SRV.pNameTarget == nullptr) continue;

            SrvRecord value{};
            value.target = NormalizeDnsName(record->Data.SRV.pNameTarget);
            value.port = record->Data.SRV.wPort;
            value.priority = record->Data.SRV.wPriority;
            value.weight = record->Data.SRV.wWeight;
            value.ttl = std::max<std::uint32_t>(1, record->dwTtl);
            if (value.target.empty() || value.port == 0) continue;
            minimum_ttl = std::min(minimum_ttl, value.ttl);
            records.push_back(std::move(value));
        }
        DnsRecordListFree(record_list, DnsFreeRecordList);

        if (records.empty())
        {
            resolution.status = ResolutionStatus::NotFound;
            return {resolution, negative_cache_ttl_seconds};
        }

        std::uint32_t ticket{};
        {
            std::scoped_lock lock{m_mutex};
            ticket = m_rng();
        }
        const auto selected_index = ChooseSrvRecord(records, ticket);
        if (!selected_index)
        {
            resolution.status = ResolutionStatus::NotFound;
            return {resolution, negative_cache_ttl_seconds};
        }

        const auto& selected = records[*selected_index];
        if (selected.target == L".")
        {
            resolution.status = ResolutionStatus::ServiceUnavailable;
            return {resolution, selected.ttl};
        }

        resolution.status = ResolutionStatus::Resolved;
        resolution.target = ResolveTargetToIpv4(selected.target).value_or(selected.target);
        resolution.port = selected.port;
        return {resolution, minimum_ttl == std::numeric_limits<std::uint32_t>::max() ? negative_cache_ttl_seconds : minimum_ttl};
    }

    auto SrvResolver::ResolveTargetToIpv4(const std::wstring_view target) const -> std::optional<std::wstring>
    {
        if (!m_winsock_ready) return std::nullopt;

        ADDRINFOW hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        PADDRINFOW addresses = nullptr;
        const std::wstring target_string{target};
        if (GetAddrInfoW(target_string.c_str(), nullptr, &hints, &addresses) != 0) return std::nullopt;

        std::optional<std::wstring> result{};
        for (auto* address = addresses; address != nullptr; address = address->ai_next)
        {
            if (address->ai_family != AF_INET || address->ai_addr == nullptr) continue;
            const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
            wchar_t buffer[INET_ADDRSTRLEN]{};
            if (InetNtopW(AF_INET, &ipv4->sin_addr, buffer, static_cast<DWORD>(std::size(buffer))) != nullptr)
            {
                result = buffer;
                break;
            }
        }
        FreeAddrInfoW(addresses);
        return result;
    }

    auto SrvResolver::FindCached(const std::wstring_view query) -> std::optional<Resolution>
    {
        const auto key = Lowercase(query);
        const auto now = std::chrono::steady_clock::now();
        std::scoped_lock lock{m_mutex};

        const auto found = m_cache.find(key);
        if (found == m_cache.end()) return std::nullopt;
        if (found->second.expires_at <= now)
        {
            m_cache.erase(found);
            return std::nullopt;
        }
        return found->second.resolution;
    }

    void SrvResolver::PutCached(std::wstring query, const Resolution& resolution, const std::uint32_t ttl_seconds)
    {
        CacheEntry entry{};
        entry.resolution = resolution;
        entry.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds{std::max<std::uint32_t>(1, ttl_seconds)};

        std::scoped_lock lock{m_mutex};
        m_cache[Lowercase(query)] = std::move(entry);
    }
}
