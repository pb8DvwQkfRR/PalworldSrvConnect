#pragma once

#include "Endpoint.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PalworldSrvConnect
{
    enum class ResolutionStatus
    {
        NotApplicable,
        NotFound,
        Resolved,
        ServiceUnavailable,
        QueryFailed,
    };

    struct Resolution
    {
        ResolutionStatus status{ResolutionStatus::NotApplicable};
        std::wstring query{};
        std::wstring target{};
        std::uint16_t port{};
    };

    class SrvResolver
    {
      public:
        SrvResolver();
        ~SrvResolver();

        SrvResolver(const SrvResolver&) = delete;
        auto operator=(const SrvResolver&) -> SrvResolver& = delete;

        auto ResolveInput(std::wstring_view input) -> Resolution;
        auto ResolveHost(std::wstring_view host, bool = false) -> Resolution;

      private:
        struct CacheEntry
        {
            Resolution resolution{};
            std::chrono::steady_clock::time_point expires_at{};
        };

        auto Query(std::wstring_view query) -> std::pair<Resolution, std::uint32_t>;
        auto ResolveTargetToIpv4(std::wstring_view target) const -> std::optional<std::wstring>;
        auto FindCached(std::wstring_view query) -> std::optional<Resolution>;
        void PutCached(std::wstring query, const Resolution& resolution, std::uint32_t ttl_seconds);

        std::unordered_map<std::wstring, CacheEntry> m_cache{};
        std::mutex m_mutex{};
        std::mt19937 m_rng{};
        bool m_winsock_ready{};
    };
}
