#include "check.hpp"
#include "core/flv_feed.hpp"
#include <initializer_list>
using namespace odeum::program;

namespace {
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

void tags() {
    check(flv_header() == bytes({'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0}), "FLV header");
    const FlvTag audio{FlvTagType::audio, 0x01020304, bytes({0xAF, 0x01, 0x21})};
    check(flv_serialize(audio) == bytes({8, 0, 0, 3, 2, 3, 4, 1, 0, 0, 0, 0xAF, 0x01, 0x21, 0, 0, 0, 14}), "Tag header, extended timestamp and previous size");

    check(aac_sequence_header(48000, 2).body == bytes({0xAF, 0x00, 0x11, 0x90}), "AAC-LC 48 kHz stereo AudioSpecificConfig");
    check(aac_sequence_header(44100, 1).body == bytes({0xAF, 0x00, 0x12, 0x08}), "AAC-LC 44.1 kHz mono AudioSpecificConfig");
    check(aac_frame(bytes({0x21, 0x10}), 40) == FlvTag{FlvTagType::audio, 40, bytes({0xAF, 0x01, 0x21, 0x10})}, "Raw AAC tag");

    const AvcParameterSets high{bytes({0x67, 0x64, 0x00, 0x28, 0xAC}), bytes({0x68, 0xEE, 0x3C, 0x80})};
    check(avc_sequence_header(high).body == bytes({0x17, 0, 0, 0, 0, 1, 0x64, 0x00, 0x28, 0xFF, 0xE1, 0, 5, 0x67, 0x64, 0x00, 0x28, 0xAC,
                                                   1, 0, 4, 0x68, 0xEE, 0x3C, 0x80, 0xFD, 0xF8, 0xF8, 0x00}), "High profile AVCDecoderConfigurationRecord");
    const AvcParameterSets baseline{bytes({0x67, 0x42, 0xE0, 0x1E}), bytes({0x68, 0xCE})};
    check(avc_sequence_header(baseline).body == bytes({0x17, 0, 0, 0, 0, 1, 0x42, 0xE0, 0x1E, 0xFF, 0xE1, 0, 4, 0x67, 0x42, 0xE0, 0x1E,
                                                       1, 0, 2, 0x68, 0xCE}), "Baseline record has no extension");

    const auto access_unit = bytes({0, 0, 0, 1, 0x09, 0xF0, 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x28, 0xAC, 0, 0, 1, 0x68, 0xEE, 0x3C, 0x80,
                                    0, 0, 1, 0x65, 0x88, 0x84});
    const auto sets = find_parameter_sets(access_unit);
    check(sets && *sets == high, "Parameter sets found in Annex-B");
    check(avc_frame(access_unit, true, 40, 66) == FlvTag{FlvTagType::video, 40, bytes({0x17, 1, 0, 0, 0x42, 0, 0, 0, 3, 0x65, 0x88, 0x84})},
          "Keyframe as AVCC without AUD/SPS/PPS");
    check(avc_frame(bytes({0, 0, 1, 0x41, 0x9A}), false, 0, -1).body == bytes({0x27, 1, 0xFF, 0xFF, 0xFF, 0, 0, 0, 2, 0x41, 0x9A}),
          "Inter frame with negative composition time");

    const auto meta = metadata_tag({1920, 1080, 30, 8000, 128, 48000, 2});
    check(meta.type == FlvTagType::script && meta.body.size() > 16, "onMetaData tag");
    check(std::vector<std::byte>(meta.body.begin(), meta.body.begin() + 16) ==
          bytes({2, 0, 13, '@', 's', 'e', 't', 'D', 'a', 't', 'a', 'F', 'r', 'a', 'm', 'e'}), "Metadata opens with @setDataFrame");
}

void feed() {
    FlvFeed feed({1920, 1080, 30, 8000, 128, 48000, 2}, 48000, 2);
    const auto key = bytes({0, 0, 0, 1, 0x67, 0x64, 0x00, 0x28, 0xAC, 0, 0, 0, 1, 0x68, 0xEE, 0x3C, 0x80, 0, 0, 0, 1, 0x65, 0x88});
    odeum::presenter::EncodedFrame inter{bytes({0, 0, 0, 1, 0x41, 0x9A}), 1000000, 1000000, false};
    check(feed.video(inter).empty(), "Frames before the first keyframe are dropped");
    check(feed.audio(bytes({0x21}), 1000000).empty(), "Audio waits for the video origin");
    odeum::presenter::EncodedFrame first{key, 1066000, 1033000, true};
    auto tags = feed.video(first);
    check(tags.size() == 4 && tags[0].type == FlvTagType::script && tags[1].body[1] == std::byte{0} && tags[2].type == FlvTagType::audio,
          "Metadata and both sequence headers precede the first keyframe");
    check(tags[3].timestamp_ms == 0 && tags[3].body[4] == std::byte{33}, "Timeline starts at the keyframe's decode time");
    inter.timestamp_us = 1100000;
    inter.decode_timestamp_us = 1066000;
    tags = feed.video(inter);
    check(tags.size() == 1 && tags[0].timestamp_ms == 33, "Following frames on the same timeline");
    auto audio = feed.audio(bytes({0x21}), 1053000);
    check(audio.size() == 1 && audio[0].timestamp_ms == 20, "Audio on the video timeline");
    feed.restart();
    check(feed.video(inter).empty(), "After a reconnection frames wait for a keyframe");
    check(feed.video(first).size() == 4, "And the headers are sent again");
}
}

int main() { return run([] { tags(); feed(); }); }
