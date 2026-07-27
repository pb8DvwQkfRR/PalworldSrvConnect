#include "Endpoint.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace PalworldSrvConnect;

namespace
{
    void Require(const bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    {
        const auto endpoint = ParseEndpoint(L" play.example.com:8211 ");
        Require(endpoint.has_value(), "domain endpoint parses");
        Require(endpoint->host == L"play.example.com", "domain host is preserved");
        Require(endpoint->port == 8211, "domain port parses");
        Require(!ShouldAttemptSrv(*endpoint), "an explicit port preserves the original connection path");
        Require(BuildSrvQuery(endpoint->host) == L"_palworld._udp.play.example.com", "default SRV owner is correct");
    }

    {
        const auto endpoint = ParseEndpoint(L"play.example.com");
        Require(endpoint.has_value(), "portless domain endpoint parses");
        Require(!endpoint->port.has_value(), "portless domain has no explicit port");
        Require(ShouldAttemptSrv(*endpoint), "portless domain triggers automatic SRV lookup");
    }

    {
        const auto endpoint = ParseEndpoint(L"_custom._udp.example.com");
        Require(endpoint.has_value(), "full SRV owner parses");
        Require(ShouldAttemptSrv(*endpoint), "portless full SRV owner triggers lookup");
        Require(BuildSrvQuery(endpoint->host) == L"_custom._udp.example.com", "full SRV owner is not prefixed twice");
    }

    {
        const auto endpoint = ParseEndpoint(L"[2001:db8::1]:8211");
        Require(endpoint.has_value(), "IPv6 endpoint parses");
        Require(endpoint->host == L"2001:db8::1", "IPv6 brackets are removed");
        Require(IsIpLiteral(endpoint->host), "IPv6 is an IP literal");
        Require(!ShouldAttemptSrv(*endpoint), "IP literals bypass SRV");
        Require(FormatEndpoint(endpoint->host, 8211) == L"[2001:db8::1]:8211", "IPv6 formatting restores brackets");
    }

    Require(!ParseEndpoint(L"example.com:0"), "zero port is rejected");
    Require(!ParseEndpoint(L"example.com:70000"), "oversized port is rejected");
    Require(!ParseEndpoint(L"example.com:not-a-port"), "non-numeric port is rejected");
    Require(IsIpLiteral(L"192.0.2.25"), "IPv4 is detected");

    {
        const std::vector<SrvRecord> records{
                {L"later.example.com", 8211, 10, 100, 60},
                {L"first-a.example.com", 8211, 0, 1, 60},
                {L"first-b.example.com", 8211, 0, 3, 60},
        };
        Require(ChooseSrvRecord(records, 0) == 1, "lowest priority wins");
        Require(ChooseSrvRecord(records, 1) == 2, "weighted selection enters second record");
        Require(ChooseSrvRecord(records, 3) == 2, "weighted selection covers full range");
    }

    {
        const std::vector<SrvRecord> records{
                {L"a.example.com", 8211, 0, 0, 60},
                {L"b.example.com", 8211, 0, 0, 60},
        };
        Require(ChooseSrvRecord(records, 0) == 0, "zero weights choose by uniform ticket");
        Require(ChooseSrvRecord(records, 1) == 1, "zero weights can select every record");
    }

    std::cout << "All endpoint tests passed.\n";
    return 0;
}
