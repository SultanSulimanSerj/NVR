// Vendor-only: sign offline license.lic (see docs/LICENSE_TICKET.md).
// Build: cmake -DNVR_BUILD_LICENSE_TOOL=ON ...
#include "nvr/license_canonical.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sodium.h>
#include <string>
#include <vector>

using json = nlohmann::json;

static void usage() {
    std::cerr << "nvr_license_sign --secret <64-byte-sk-file> --fp <64-hex> --max-ch <N> "
                 "[--mod M] [--exp unix] --out <license.lic>\n";
}

static bool readFileBin(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz != static_cast<std::streamoff>(crypto_sign_SECRETKEYBYTES)) return false;
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<bool>(f);
}

int main(int argc, char** argv) {
    std::string secret_path, fp, out_path;
    int         max_ch = 0;
    uint32_t    mod    = 0;
    int64_t     expv   = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--secret" && i + 1 < argc) secret_path = argv[++i];
        else if (a == "--fp" && i + 1 < argc) fp = argv[++i];
        else if (a == "--max-ch" && i + 1 < argc) max_ch = std::stoi(argv[++i]);
        else if (a == "--mod" && i + 1 < argc) mod = static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (a == "--exp" && i + 1 < argc) expv = std::stoll(argv[++i]);
        else if ((a == "--out" || a == "-o") && i + 1 < argc) out_path = argv[++i];
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
    }

    if (secret_path.empty() || fp.empty() || out_path.empty() || max_ch <= 0) {
        usage();
        return 2;
    }

    if (sodium_init() < 0) {
        std::cerr << "sodium_init failed\n";
        return 1;
    }

    std::vector<unsigned char> sk;
    if (!readFileBin(secret_path, sk)) {
        std::cerr << "secret file must be exactly " << crypto_sign_SECRETKEYBYTES << " bytes\n";
        return 1;
    }

    const std::string msg = nvr::license_detail::canonicalLicensePayloadString(fp, expv, max_ch, mod);
    unsigned char       sig[crypto_sign_BYTES];
    if (crypto_sign_detached(sig, nullptr,
                             reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
                             sk.data()) != 0) {
        std::cerr << "sign failed\n";
        return 1;
    }

    char b64[sodium_base64_encoded_len(sizeof(sig), sodium_base64_VARIANT_ORIGINAL) + 1U];
    sodium_bin2base64(b64, sizeof(b64), sig, sizeof(sig), sodium_base64_VARIANT_ORIGINAL);

    json root;
    root["payload"] =
        json{{"fp", fp}, {"max_ch", max_ch}, {"mod", mod}, {"exp", expv}};
    root["sig"] = std::string(b64);

    std::ofstream o(out_path, std::ios::binary | std::ios::trunc);
    if (!o) {
        std::cerr << "cannot write " << out_path << '\n';
        return 1;
    }
    o << root.dump(2) << '\n';
    std::cout << "Wrote " << out_path << '\n';
    return 0;
}
