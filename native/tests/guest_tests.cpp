#include "check.hpp"
#include "admission.hpp"
#include "http_query.hpp"
#include "invitations.hpp"
using namespace odeum;
using namespace odeum::relay;

int main() { return run([] {
    // Join codes: Crockford normalisation, exactly 10 symbols.
    check(normalize_join_code("abcd-efgh-12") == "ABCDEFGH12", "Lowercase and hyphens normalised");
    check(normalize_join_code("O0IL1 ABCDE") == "00111ABCDE", "Ambiguous symbols folded");
    check(!normalize_join_code("ABCDEFGHU2"), "U is not Crockford");
    check(!normalize_join_code("ABCDEFGH1"), "Too short");
    check(!normalize_join_code("ABCDEFGH123"), "Too long");
    check(valid_overlay_key(std::string(42, 'A') + "A"), "Canonical 43-character key");
    check(!valid_overlay_key(std::string(42, 'A') + "B"), "Noncanonical trailing bits");
    check(!valid_overlay_key(std::string(42, 'A') + "+"), "Not base64url");
    check(sha256_base64url("abc") == "ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0", "SHA-256 base64url");

    // Digests resolve to the live session only while attached.
    Invitations invitations;
    const std::string key = std::string(42, 'k') + "A";
    rejects([&] { invitations.resolve_join("ABCDEFGH12", 0); }, "session_not_live");
    invitations.attach("room", sha256_base64url("ABCDEFGH12"), sha256_base64url(key));
    check(invitations.resolve_join("abcd-efgh-12", 0) == "room", "Join code resolves");
    check(invitations.resolve_overlay(key, 0) == "room", "Overlay key resolves");
    rejects([&] { invitations.resolve_overlay("ABCDEFGH12", 0); }, "session_not_live");
    rejects([&] { invitations.attach("other", sha256_base64url("ABCDEFGH12"), sha256_base64url("x")); }, "invite_conflict");
    invitations.attach("room", sha256_base64url("ABCDEFGH12"), sha256_base64url(key));
    invitations.detach("room");
    rejects([&] { invitations.resolve_join("ABCDEFGH12", 0); }, "session_not_live");
    invitations.attach("other", sha256_base64url("ABCDEFGH12"), sha256_base64url(key));
    check(invitations.resolve_join("ABCDEFGH12", 0) == "other", "Released digest can move to a new session");
    invitations.attach("empty", "", "");
    check(invitations.resolve_join("ABCDEFGH12", 0) == "other", "Rooms without invite claims attach nothing");

    // Failed lookups are throttled across the relay; successes are not counted.
    Invitations limited;
    limited.attach("room", sha256_base64url("ABCDEFGH12"), sha256_base64url(key));
    for (std::size_t i = 0; i < Invitations::max_failures_per_minute; ++i) rejects([&] { limited.resolve_join("ZZZZZZZZZZ", 1000); }, "session_not_live");
    rejects([&] { limited.resolve_join("ABCDEFGH12", 1000); }, "rate_limited");
    check(limited.resolve_join("ABCDEFGH12", 61000) == "room", "Window expires after a minute");

    // Guest identity: random subject, trimmed and bounded display name.
    auto guest = guest_ticket("room", "  ねこ ");
    check(guest.role == Role::viewer && guest.sid == "room" && guest.name == "ねこ", "Guest is a viewer");
    check(guest.sub.starts_with("guest:") && guest.sub != guest_ticket("room", "x").sub, "Fresh guest subject");
    check(guest.invite_join.empty(), "Guests carry no invitation");
    rejects([&] { guest_ticket("room", "   "); }, "invalid_name");
    rejects([&] { guest_ticket("room", std::string(33, 'a')); }, "invalid_name");
    rejects([&] { guest_ticket("room", "a\nb"); }, "invalid_name");
    check(guest_ticket("room", std::string(32, 'a')).name.size() == 32, "32 characters accepted");
    check(overlay_ticket("room").role == Role::overlay, "Overlay role");

    // Query parsing.
    auto target = parse_target("/v1/guest?code=ABCD-EFGH-12&name=%E3%81%AD%E3%81%93+san");
    check(target && target->path == "/v1/guest" && target->query.at("name") == "ねこ san", "Percent decoding");
    check(parse_target("/join") && parse_target("/join")->query.empty(), "No query");
    check(!parse_target("/v1/guest?code=a&code=b"), "Duplicate parameters rejected");
    check(!parse_target("/v1/guest?code=%G1"), "Malformed escape rejected");
    check(!parse_target("relative"), "Origin-form targets only");
}); }
