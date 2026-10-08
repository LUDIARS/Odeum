#include "signing.hpp"
#include "authentication.hpp"
#include "invitations.hpp"
using namespace odeum;
using namespace odeum::relay;
int main() { return run([] {
    SigningKey key, other;
    Json keys={{"key",key.pem()}};
    TicketVerifier verifier(keys);
    Json claims={{"iss","glab"},{"aud","odeum-relay"},{"sub","u"},{"name","名前"},{"role","viewer"},{"sid","s"},{"exp",1100},{"jti","nonce"}};
    auto wire=key.sign(claims);
    check(verifier.verify(wire,1000).sid=="s","Valid ticket");
    rejects([&] { verifier.verify(other.sign(claims),1000); },"invalid_ticket");
    rejects([&] { verifier.verify(key.sign(claims,"missing"),1000); },"invalid_ticket");
    auto wrong=claims; wrong["aud"]="elsewhere"; rejects([&] { verifier.verify(key.sign(wrong),1000); });
    rejects([&] { verifier.verify(wire,1100); });
    wrong=claims; wrong["exp"]=1301; rejects([&] { verifier.verify(key.sign(wrong),1000); });
    wrong=claims; wrong["name"]=std::string(65,'n'); rejects([&] { verifier.verify(key.sign(wrong),1000); });
    Authentication auth(keys); auth.consume(wire,1000);
    rejects([&] { auth.consume(wire,1000); },"replayed_ticket");
    Authentication endpoint(keys);
    rejects([&] { authorize_sessions(endpoint,"Bearer "+wire,1000); },"forbidden");
    claims["role"]="service"; claims.erase("sid"); claims["jti"]="service-nonce";
    auto service=key.sign(claims);
    check(authorize_sessions(endpoint,"Bearer "+service,1000).role==Role::service,"Service ticket");
    rejects([&] { authorize_sessions(endpoint,"Bearer "+service,1000); },"replayed_ticket");
    rejects([&] { authorize_sessions(endpoint,"",1000); },"invalid_ticket");
    claims["role"]="presenter"; claims["sid"]="s"; claims["jti"]="presenter-nonce";
    rejects([&] { authorize_sessions(endpoint,"Bearer "+key.sign(claims),1000); },"forbidden");

    // Presenter tickets may carry GLab-held invitation digests; other roles may not.
    const auto join=sha256_base64url("ABCDEFGH12"), overlay=sha256_base64url("overlay-key");
    claims["jti"]="invite-nonce"; claims["invite"]={{"join",join},{"overlay",overlay}};
    auto invited=verifier.verify(key.sign(claims),1000);
    check(invited.invite_join==join && invited.invite_overlay==overlay,"Presenter invite digests");
    claims.erase("invite");
    check(verifier.verify(key.sign(claims),1000).invite_join.empty(),"Invite is optional");
    auto bad=claims; bad["invite"]={{"join",join},{"overlay",join}}; rejects([&] { verifier.verify(key.sign(bad),1000); },"invalid_ticket");
    bad=claims; bad["invite"]={{"join",join}}; rejects([&] { verifier.verify(key.sign(bad),1000); },"invalid_ticket");
    bad=claims; bad["invite"]={{"join","short"},{"overlay",overlay}}; rejects([&] { verifier.verify(key.sign(bad),1000); },"invalid_ticket");
    bad=claims; bad["invite"]={{"join",join},{"overlay",overlay},{"extra",overlay}}; rejects([&] { verifier.verify(key.sign(bad),1000); },"invalid_ticket");
    bad=claims; bad["role"]="viewer"; bad["invite"]={{"join",join},{"overlay",overlay}};
    rejects([&] { verifier.verify(key.sign(bad),1000); },"invalid_ticket");
}); }
