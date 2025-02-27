/*
 * This file is part of the trojan project.
 * Trojan is an unidentifiable mechanism that helps you bypass GFW.
 * Copyright (C) 2017-2019  GreaterFire
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <boost/property_tree/json_parser.hpp>
#include <openssl/sha.h>
using namespace std;
using namespace boost::property_tree;

void Config::load(const string &filename) {
    Log::log("Config::load()",Log::FATAL);
    ptree tree;
    read_json(filename, tree);
    populate(tree);
}

void Config::populate(const std::string &JSON) {
    istringstream s(JSON);
    ptree tree;
    read_json(s, tree);
    populate(tree);
}

void Config::populate(const ptree &tree) {
    string rt = tree.get("run_type", string("client"));
    if (rt == "server") {
        Log::log("Config::popilate server",Log::FATAL);
        run_type = SERVER;
    } else if (rt == "forward") {
        run_type = FORWARD;
    } else {
        run_type = CLIENT;
    }
    // 疯狂获取参数ing，两个参数分别是get(path, default_val)
    local_addr = tree.get("local_addr", string());
    local_port = tree.get("local_port", uint16_t());
    remote_addr = tree.get("remote_addr", string());
    remote_port = tree.get("remote_port", uint16_t());
    target_addr = tree.get("target_addr", string());
    target_port = tree.get("target_port", uint16_t());
    // Log::log(local_addr+", "+remote_addr,Log::FATAL);
    map<string, string>().swap(password);
    for (auto& item: tree.get_child("password")) {
        string p = item.second.get_value<string>();
        password[SHA224(p)] = p;
        // Log::log(p + ",,,",Log::FATAL);
    }
    append_payload = tree.get("append_payload", true);
    udp_timeout = tree.get("udp_timeout", 60);
    log_level = static_cast<Log::Level>(tree.get("log_level", 1));
    ssl.verify = tree.get("ssl.verify", true);
    ssl.verify_hostname = tree.get("ssl.verify_hostname", true);
    ssl.cert = tree.get("ssl.cert", string());
    ssl.key = tree.get("ssl.key", string());
    ssl.key_password = tree.get("ssl.key_password", string());
    ssl.cipher = tree.get("ssl.cipher", string());
    ssl.prefer_server_cipher = tree.get("ssl.prefer_server_cipher", true);
    ssl.sni = tree.get("ssl.sni", string());
    ssl.alpn = "";
    for (auto& item: tree.get_child("ssl.alpn")) {
        string proto = item.second.get_value<string>();
        ssl.alpn += (char)((unsigned char)(proto.length()));
        ssl.alpn += proto;
    }
    ssl.reuse_session = tree.get("ssl.reuse_session", true);
    ssl.session_ticket = tree.get("ssl.session_ticket", false);
    ssl.session_timeout = tree.get("ssl.session_timeout", long(600));
    ssl.plain_http_response = tree.get("ssl.plain_http_response", string());
    ssl.curves = tree.get("ssl.curves", string());
    ssl.dhparam = tree.get("ssl.dhparam", string());
    tcp.prefer_ipv4 = tree.get("tcp.prefer_ipv4", false);
    tcp.no_delay = tree.get("tcp.no_delay", true);
    tcp.keep_alive = tree.get("tcp.keep_alive", true);
    tcp.fast_open = tree.get("tcp.fast_open", false);
    tcp.fast_open_qlen = tree.get("tcp.fast_open_qlen", 20);
    mysql.enabled = tree.get("mysql.enabled", false);
    mysql.server_addr = tree.get("mysql.server_addr", string("127.0.0.1"));
    mysql.server_port = tree.get("mysql.server_port", uint16_t(3306));
    mysql.database = tree.get("mysql.database", string("trojan"));
    mysql.username = tree.get("mysql.username", string("trojan"));
    mysql.password = tree.get("mysql.password", string());
    // Log::log("here",Log::FATAL);
}

bool Config::sip003() {
    char *JSON = getenv("SS_PLUGIN_OPTIONS");
    if (JSON == NULL) {
        Log::log("sip003 JSON fail",Log::FATAL);
        return false;
    }
    populate(JSON);
    switch (run_type) {
        case SERVER:
            local_addr = getenv("SS_REMOTE_HOST");
            local_port = atoi(getenv("SS_REMOTE_PORT"));
            break;
        case CLIENT:
            throw runtime_error("SIP003 with wrong run_type");
            break;
        case FORWARD:
            remote_addr = getenv("SS_REMOTE_HOST");
            remote_port = atoi(getenv("SS_REMOTE_PORT"));
            local_addr = getenv("SS_LOCAL_HOST");
            local_port = atoi(getenv("SS_LOCAL_PORT"));
            break;
    }
    return true;
}

string Config::SHA224(const string &message) {
    uint8_t digest[SHA224_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA224_Init(&ctx);
    SHA224_Update(&ctx, message.c_str(), message.length());
    SHA224_Final(digest, &ctx);
    char mdString[(SHA224_DIGEST_LENGTH << 1) + 1];
    for (int i = 0; i < SHA224_DIGEST_LENGTH; ++i) {
        sprintf(mdString + (i << 1), "%02x", (unsigned int)digest[i]);
    }
    return string(mdString);
}
