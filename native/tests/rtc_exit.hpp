#pragma once
// For tests that create libdatachannel peer connections: wait for the library's worker threads
// before main returns. Without it, exit() runs OpenSSL's atexit cleanup while a worker may still
// be using OpenSSL (DTLS certificate generation starts with every PeerConnection), which crashed
// odeum.overlay about every other run (SIGSEGV in OPENSSL_cleanup).
#include <rtc/rtc.hpp>
#include <chrono>
#include <future>
#include <iostream>

inline int finish_rtc(int status) {
    if (rtc::Cleanup().wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        std::cerr << "libdatachannel did not finish its cleanup within 10 s\n";
        return 1;
    }
    return status;
}
