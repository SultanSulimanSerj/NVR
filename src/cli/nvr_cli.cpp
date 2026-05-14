// Standalone CLI client that talks to the running daemon via HTTP API.
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;

int runScript(const std::vector<const char*>& argv) {
    if (argv.empty() || !argv[0]) return 2;
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto p : argv) {
        if (!p) break;
        cargv.push_back(const_cast<char*>(p));
    }
    cargv.push_back(nullptr);

    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, cargv.data(), environ) != 0) {
        std::perror("posix_spawnp");
        return 127;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        std::perror("waitpid");
        return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

namespace {

using json = nlohmann::json;

struct Args {
    std::string base_url{"http://127.0.0.1:8080"};
    std::string token_file{"/etc/nvr-prototype/cli.token"};
};

size_t writeCb(char* p, size_t s, size_t n, std::string* out) {
    out->append(p, s * n);
    return s * n;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    auto s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::pair<long, std::string> http(const std::string& method, const std::string& url,
                                   const std::string& token, const std::string& body = "") {
    CURL* c = curl_easy_init();
    std::string out;
    struct curl_slist* hdrs = nullptr;
    if (!token.empty()) hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token).c_str());
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    if (method == "POST")   curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "POST");
    if (method == "PATCH")  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PATCH");
    if (method == "DELETE") curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    auto rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        std::cerr << "curl: " << curl_easy_strerror(rc) << "\n";
        return {0, ""};
    }
    return {code, out};
}

int cmdLogin(const Args& a, int argc, char** argv) {
    if (argc < 4) { std::cerr << "usage: nvr-cli login <user> <password>\n"; return 2; }
    json body = {{"login", argv[2]}, {"password", argv[3]}};
    auto [code, resp] = http("POST", a.base_url + "/api/v1/auth/login", {}, body.dump());
    if (code != 200) { std::cerr << "login failed: " << code << "\n" << resp; return 1; }
    auto j = json::parse(resp);
    std::ofstream o(a.token_file, std::ios::trunc);
    o << j["token"].get<std::string>();
    std::cout << "OK; token saved.\n";
    return 0;
}

int cmdStatus(const Args& a) {
    auto tok = slurp(a.token_file);
    auto [code, resp] = http("GET", a.base_url + "/api/v1/system", tok);
    std::cout << resp << "\n";
    return code == 200 ? 0 : 1;
}

int cmdCameras(const Args& a, int argc, char** argv) {
    auto tok = slurp(a.token_file);
    std::string url = a.base_url + "/api/v1/cameras";
    if (argc < 3 || std::string(argv[2]) == "list") {
        auto [code, resp] = http("GET", url, tok);
        std::cout << resp << "\n";
        return code == 200 ? 0 : 1;
    }
    if (std::string(argv[2]) == "add") {
        if (argc < 5) { std::cerr << "usage: nvr-cli cameras add <id> <rtsp_url>\n"; return 2; }
        json body = {{"id", argv[3]}, {"name", argv[3]},
                     {"rtsp_url", argv[4]}, {"preferred_hw", "auto"}};
        auto [code, resp] = http("POST", url, tok, body.dump());
        std::cout << resp << "\n";
        return code == 201 ? 0 : 1;
    }
    if (std::string(argv[2]) == "del") {
        if (argc < 4) { std::cerr << "usage: nvr-cli cameras del <id>\n"; return 2; }
        auto [code, resp] = http("DELETE", url + "/" + argv[3], tok);
        return code == 204 ? 0 : 1;
    }
    std::cerr << "unknown subcommand\n"; return 2;
}

int cmdUsers(const Args& a, int argc, char** argv) {
    auto tok = slurp(a.token_file);
    std::string url = a.base_url + "/api/v1/users";
    if (argc < 3 || std::string(argv[2]) == "list") {
        auto [code, resp] = http("GET", url, tok);
        std::cout << resp << "\n";
        return code == 200 ? 0 : 1;
    }
    if (std::string(argv[2]) == "add") {
        if (argc < 6) { std::cerr << "usage: nvr-cli users add <login> <password> <role>\n"; return 2; }
        json body = {{"login", argv[3]}, {"password", argv[4]}, {"role", argv[5]}};
        auto [code, resp] = http("POST", url, tok, body.dump());
        std::cout << resp << "\n";
        return code == 201 ? 0 : 1;
    }
    if (std::string(argv[2]) == "del") {
        if (argc < 4) { std::cerr << "usage: nvr-cli users del <login>\n"; return 2; }
        auto [code, resp] = http("DELETE", url + "/" + argv[3], tok);
        return code == 204 ? 0 : 1;
    }
    return 2;
}

void usage() {
    std::cerr <<
        "nvr-cli — control nvr-prototype daemon.\n\n"
        "Commands:\n"
        "  login <user> <password>           Authenticate and save token.\n"
        "  status                            Show system info.\n"
        "  cameras list|add|del              Manage cameras.\n"
        "  users   list|add|del              Manage users.\n"
        "  metrics                           Show /metrics.\n"
        "  backup [path]                     Run nvr-backup helper.\n"
        "  restore <file>                    Run nvr-restore helper.\n"
        "Env:\n"
        "  NVR_URL       (default: http://127.0.0.1:8080)\n"
        "  NVR_TOKEN_FILE (default: /etc/nvr-prototype/cli.token)\n";
}

}

int main(int argc, char** argv) {
    Args a;
    if (auto* e = std::getenv("NVR_URL"))        a.base_url   = e;
    if (auto* e = std::getenv("NVR_TOKEN_FILE")) a.token_file = e;

    if (argc < 2) { usage(); return 2; }
    std::string cmd = argv[1];
    if (cmd == "login")    return cmdLogin(a, argc, argv);
    if (cmd == "status")   return cmdStatus(a);
    if (cmd == "cameras")  return cmdCameras(a, argc, argv);
    if (cmd == "users")    return cmdUsers(a, argc, argv);
    if (cmd == "metrics") {
        auto [code, resp] = http("GET", a.base_url + "/metrics", "");
        std::cout << resp;
        return code == 200 ? 0 : 1;
    }
    if (cmd == "backup") {
        if (argc >= 3)
            return runScript({"/usr/bin/nvr_backup.sh", argv[2], nullptr});
        return runScript({"/usr/bin/nvr_backup.sh", nullptr});
    }
    if (cmd == "restore") {
        if (argc < 3) { std::cerr << "missing path\n"; return 2; }
        return runScript({"/usr/bin/nvr_restore.sh", argv[2], nullptr});
    }
    usage();
    return 2;
}
