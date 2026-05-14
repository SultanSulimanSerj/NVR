#include "nvr/store/Crypto.hpp"

#include "nvr/Logger.hpp"

#include <sodium.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace nvr::store {

namespace {
std::once_flag g_sodium_init;
void ensureSodium() {
    std::call_once(g_sodium_init, [] {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium init failed");
        }
    });
}
}

MasterKey loadOrCreateMasterKey(const fs::path& path) {
    ensureSodium();
    MasterKey k;

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    if (fs::exists(path)) {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(k.bytes.data()),
                static_cast<std::streamsize>(k.bytes.size()));
        if (in.gcount() != static_cast<std::streamsize>(k.bytes.size())) {
            throw std::runtime_error("master key file truncated: " + path.string());
        }
        return k;
    }

    randombytes_buf(k.bytes.data(), k.bytes.size());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write master key " + path.string());
    out.write(reinterpret_cast<const char*>(k.bytes.data()),
              static_cast<std::streamsize>(k.bytes.size()));
    out.close();
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write);
    NVR_INFO("crypto", "master key created at %s", path.c_str());
    return k;
}

std::vector<uint8_t> encrypt(const MasterKey& key,
                              const std::string& plaintext,
                              const std::string& aad) {
    ensureSodium();
    std::vector<uint8_t> nonce(crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    randombytes_buf(nonce.data(), nonce.size());

    std::vector<uint8_t> out(nonce.size() + plaintext.size() +
                              crypto_aead_chacha20poly1305_ietf_ABYTES);
    std::copy(nonce.begin(), nonce.end(), out.begin());

    unsigned long long ct_len = 0;
    crypto_aead_chacha20poly1305_ietf_encrypt(
        out.data() + nonce.size(), &ct_len,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
        nullptr,
        nonce.data(),
        key.bytes.data());

    out.resize(nonce.size() + static_cast<size_t>(ct_len));
    return out;
}

std::optional<std::string> decrypt(const MasterKey& key,
                                    const std::vector<uint8_t>& blob,
                                    const std::string& aad) {
    ensureSodium();
    const size_t npub = crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
    if (blob.size() <= npub + crypto_aead_chacha20poly1305_ietf_ABYTES) return std::nullopt;

    std::string plain;
    plain.resize(blob.size() - npub - crypto_aead_chacha20poly1305_ietf_ABYTES + 1);
    unsigned long long pt_len = 0;

    int rc = crypto_aead_chacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char*>(plain.data()), &pt_len,
        nullptr,
        blob.data() + npub, blob.size() - npub,
        reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
        blob.data(),
        key.bytes.data());
    if (rc != 0) return std::nullopt;
    plain.resize(static_cast<size_t>(pt_len));
    return plain;
}

std::string hashPassword(const std::string& plain) {
    ensureSodium();
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, plain.c_str(), plain.size(),
                           crypto_pwhash_OPSLIMIT_INTERACTIVE,
                           crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("crypto_pwhash_str failed (out of memory)");
    }
    return std::string(out);
}

bool verifyPassword(const std::string& plain, const std::string& hash) {
    ensureSodium();
    return crypto_pwhash_str_verify(hash.c_str(), plain.c_str(), plain.size()) == 0;
}

std::string randomHex(size_t bytes) {
    ensureSodium();
    std::vector<uint8_t> buf(bytes);
    randombytes_buf(buf.data(), buf.size());
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : buf) oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

}
