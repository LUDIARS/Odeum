#include "signing.hpp"
#include "authentication.hpp"
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
}); }
