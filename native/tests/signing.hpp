#pragma once
#include "check.hpp"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>

struct SigningKey {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{EVP_PKEY_Q_keygen(nullptr,nullptr,"ED25519"), EVP_PKEY_free};
    SigningKey() { check(key != nullptr,"Key generation"); }
    static std::string base64(const unsigned char* data, std::size_t size) {
        std::string result(4*((size+2)/3)+1,'\0');
        auto n=EVP_EncodeBlock(reinterpret_cast<unsigned char*>(result.data()),data,static_cast<int>(size)); result.resize(n);
        while (result.ends_with('=')) result.pop_back();
        for (auto& c:result) { if(c=='+') c='-'; else if(c=='/') c='_'; } return result;
    }
    static std::string base64(const std::string& s) { return base64(reinterpret_cast<const unsigned char*>(s.data()),s.size()); }
    std::string pem() const {
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()),BIO_free);
        check(PEM_write_bio_PUBKEY(bio.get(),key.get())==1,"Public key PEM");
        char* data=nullptr; auto n=BIO_get_mem_data(bio.get(),&data); return {data,static_cast<std::size_t>(n)};
    }
    std::string sign(const odeum::Json& claims, std::string kid="key") const {
        auto input=base64(odeum::Json{{"alg","EdDSA"},{"kid",kid}}.dump())+"."+base64(claims.dump());
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
        check(EVP_DigestSignInit(ctx.get(),nullptr,nullptr,nullptr,key.get())==1,"Sign init");
        unsigned char bytes[64]; std::size_t n=sizeof(bytes);
        check(EVP_DigestSign(ctx.get(),bytes,&n,reinterpret_cast<const unsigned char*>(input.data()),input.size())==1,"Sign");
        return input+"."+base64(bytes,n);
    }
};
