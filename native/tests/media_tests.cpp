#include "check.hpp"
#include "media.hpp"
using namespace odeum::relay;
int main() { return run([] {
    rtc::binary pli(12,std::byte{0}); pli[0]=std::byte{0x81}; pli[1]=std::byte{206}; pli[3]=std::byte{2};
    check(contains_keyframe_request(pli),"PLI recognized");
    rtc::binary fir(20,std::byte{0}); fir[0]=std::byte{0x84}; fir[1]=std::byte{206}; fir[3]=std::byte{4};
    check(contains_keyframe_request(fir),"FIR recognized");
    pli.resize(8); check(!contains_keyframe_request(pli),"Truncated feedback rejected");
    check(public_candidate("candidate:1 1 UDP 1 10.0.0.1 5000 typ host","198.51.100.1")==
        "candidate:1 1 UDP 1 198.51.100.1 5000 typ host","Public address mapping");
    auto relay="candidate:1 1 UDP 1 192.0.2.2 5000 typ relay";
    check(public_candidate(relay,"198.51.100.1")==relay,"TURN candidate preserved");
    auto access=WebAccess::from_environment(4400);
    check(access.allows_host("localhost:4400"),"Loopback health host");
    check(!access.allows_host("localhost.evil.test:4400"),"Suffix confusion rejected");
    check(!access.allows_host("user@localhost:4400"),"Userinfo rejected");
    check(!access.allows_host("localhost:bad"),"Invalid port rejected");
    check(access.allows_origin(""),"Native client without Origin");
    check(!access.allows_origin("https://unconfigured.invalid"),"Unknown Origin rejected");
}); }
