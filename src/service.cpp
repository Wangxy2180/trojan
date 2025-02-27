/*
 * This file is part of the trojan project.
 * Trojan is an unidentifiable mechanism that helps you bypass GFW.
 * Copyright (C) 2017-2019  GreaterFire, wongsyrone, UzminTid
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

#include "service.h"
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <fstream>
#ifdef _WIN32
#include <wincrypt.h>
#include <tchar.h>
#endif // _WIN32
#include <openssl/opensslv.h>
#include "serversession.h"
#include "clientsession.h"
#include "forwardsession.h"
#include "ssldefaults.h"
#include "sslsession.h"
using namespace std;
using namespace boost::asio::ip;
using namespace boost::asio::ssl;

Service::Service(Config &config, bool test) :
    config(config),
    socket_acceptor(io_service),
    ssl_context(context::sslv23),
    auth(nullptr),
    udp_socket(io_service) {
    if (!test) {
        tcp::resolver resolver(io_service);
        tcp::endpoint listen_endpoint = *resolver.resolve(tcp::resolver::query(config.local_addr, to_string(config.local_port)));
        socket_acceptor.open(listen_endpoint.protocol());
        socket_acceptor.set_option(tcp::acceptor::reuse_address(true));
        // 就是这句话，他需要sudo权限
        socket_acceptor.bind(listen_endpoint);

        socket_acceptor.listen();
        if (config.run_type == Config::FORWARD) {
            auto udp_bind_endpoint = udp::endpoint(listen_endpoint.address(), listen_endpoint.port());
            udp_socket.open(udp_bind_endpoint.protocol());
            udp_socket.bind(udp_bind_endpoint);
        }
    }
    Log::log("service ctor2",Log::FATAL);
    Log::level = config.log_level;
    auto native_context = ssl_context.native_handle();
    ssl_context.set_options(context::default_workarounds | context::no_sslv2 | context::no_sslv3 | context::single_dh_use);
    if (config.ssl.curves != "") {
        SSL_CTX_set1_curves_list(native_context, config.ssl.curves.c_str());
    }
    if (config.run_type == Config::SERVER) {
        Log::log("cert:"+config.ssl.cert,Log::FATAL);
    // 就是下边这句出问题了
        ssl_context.use_certificate_chain_file(config.ssl.cert);
        ssl_context.set_password_callback([this](size_t, context_base::password_purpose) {
            return this->config.ssl.key_password;
        });
        Log::log("key :"+config.ssl.key,Log::FATAL);
        ssl_context.use_private_key_file(config.ssl.key, context::pem);

        if (config.ssl.prefer_server_cipher) {
            SSL_CTX_set_options(native_context, SSL_OP_CIPHER_SERVER_PREFERENCE);
        }
        if (config.ssl.alpn != "") {
            SSL_CTX_set_alpn_select_cb(native_context, [](SSL*, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *config) -> int {
                if (SSL_select_next_proto((unsigned char**)out, outlen, (unsigned char*)(((Config*)config)->ssl.alpn.c_str()), ((Config*)config)->ssl.alpn.length(), in, inlen) != OPENSSL_NPN_NEGOTIATED) {
                    return SSL_TLSEXT_ERR_NOACK;
                }
                return SSL_TLSEXT_ERR_OK;
            }, &config);
        }
        if (config.ssl.reuse_session) {
            SSL_CTX_set_timeout(native_context, config.ssl.session_timeout);
            if (!config.ssl.session_ticket) {
                SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
            }
        } else {
            SSL_CTX_set_session_cache_mode(native_context, SSL_SESS_CACHE_OFF);
            SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
        }
        if (config.ssl.plain_http_response != "") {
            ifstream ifs(config.ssl.plain_http_response, ios::binary);
            if (!ifs.is_open()) {
                throw runtime_error(config.ssl.plain_http_response + ": " + strerror(errno));
            }
            plain_http_response = string(istreambuf_iterator<char>(ifs), istreambuf_iterator<char>());
        }
        if (config.ssl.dhparam == "") {
            ssl_context.use_tmp_dh(boost::asio::const_buffer(SSLDefaults::g_dh2048_sz, SSLDefaults::g_dh2048_sz_size));
        } else {
            ssl_context.use_tmp_dh_file(config.ssl.dhparam);
        }
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_CTX_set_ecdh_auto(native_context, 1);
#endif
        if (config.mysql.enabled) {
#ifdef ENABLE_MYSQL
            auth = new Authenticator(config);
#else // ENABLE_MYSQL
            Log::log_with_date_time("MySQL is not supported", Log::WARN);
#endif // ENABLE_MYSQL
        }
    } else {
        if (config.ssl.sni == "") {
            config.ssl.sni = config.remote_addr;
        }
        if (config.ssl.verify) {
            ssl_context.set_verify_mode(verify_peer);
            if (config.ssl.cert == "") {
                ssl_context.set_default_verify_paths();
#ifdef _WIN32
                HCERTSTORE h_store = CertOpenSystemStore(0, _T("ROOT"));
                if (h_store) {
                    X509_STORE *store = SSL_CTX_get_cert_store(native_context);
                    PCCERT_CONTEXT p_context = NULL;
                    while ((p_context = CertEnumCertificatesInStore(h_store, p_context))) {
                        const unsigned char *encoded_cert = p_context->pbCertEncoded;
                        X509 *x509 = d2i_X509(NULL, &encoded_cert, p_context->cbCertEncoded);
                        if (x509) {
                            X509_STORE_add_cert(store, x509);
                            X509_free(x509);
                        }
                    }
                    CertCloseStore(h_store, 0);
                }
#endif // _WIN32
            } else {
                ssl_context.load_verify_file(config.ssl.cert);
            }
            if (config.ssl.verify_hostname) {
                ssl_context.set_verify_callback(rfc2818_verification(config.ssl.sni));
            }
            X509_VERIFY_PARAM *param = X509_VERIFY_PARAM_new();
            X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_PARTIAL_CHAIN);
            SSL_CTX_set1_param(native_context, param);
            X509_VERIFY_PARAM_free(param);
        } else {
            ssl_context.set_verify_mode(verify_none);
        }
        if (config.ssl.alpn != "") {
            SSL_CTX_set_alpn_protos(native_context, (unsigned char*)(config.ssl.alpn.c_str()), config.ssl.alpn.length());
        }
        if (config.ssl.reuse_session) {
            SSL_CTX_set_session_cache_mode(native_context, SSL_SESS_CACHE_CLIENT);
            SSLSession::set_callback(native_context);
            if (!config.ssl.session_ticket) {
                SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
            }
        } else {
            SSL_CTX_set_options(native_context, SSL_OP_NO_TICKET);
        }
    }
    if (config.ssl.cipher != "") {
        SSL_CTX_set_cipher_list(native_context, config.ssl.cipher.c_str());
    }
    if (!test) {
        if (config.tcp.no_delay) {
            socket_acceptor.set_option(tcp::no_delay(true));
        }
        if (config.tcp.keep_alive) {
            socket_acceptor.set_option(boost::asio::socket_base::keep_alive(true));
        }
#ifdef TCP_FASTOPEN
        if (config.tcp.fast_open) {
            using fastopen = boost::asio::detail::socket_option::integer<IPPROTO_TCP, TCP_FASTOPEN>;
            boost::system::error_code ec;
            socket_acceptor.set_option(fastopen(config.tcp.fast_open_qlen), ec);
        }
#else // TCP_FASTOPEN
        if (config.tcp.fast_open) {
            Log::log_with_date_time("TCP_FASTOPEN is not supported", Log::WARN);
        }
#endif // TCP_FASTOPEN
#ifndef TCP_FASTOPEN_CONNECT
        if (config.tcp.fast_open) {
            Log::log_with_date_time("TCP_FASTOPEN_CONNECT is not supported", Log::WARN);
        }
#endif // TCP_FASTOPEN_CONNECT
    }
    if (Log::keylog) {
#ifdef ENABLE_SSL_KEYLOG
        SSL_CTX_set_keylog_callback(native_context, [](const SSL*, const char *line) {
            fprintf(Log::keylog, "%s\n", line);
            fflush(Log::keylog);
        });
#else // ENABLE_SSL_KEYLOG
        Log::log_with_date_time("SSL KeyLog is not supported", Log::WARN);
#endif // ENABLE_SSL_KEYLOG
    }
}

void Service::run() {
    async_accept();
    if (config.run_type == Config::FORWARD) {
        udp_async_read();
    }
    tcp::endpoint local_endpoint = socket_acceptor.local_endpoint();
    string rt;
    if (config.run_type == Config::SERVER) {
        rt = "server";
    } else if (config.run_type == Config::FORWARD) {
        rt = "forward";
    } else {
        rt = "client";
    }
    Log::log_with_date_time(string("trojan service (") + rt + ") started at " + local_endpoint.address().to_string() + ':' + to_string(local_endpoint.port()), Log::WARN);
    io_service.run();
    Log::log_with_date_time("trojan service stopped", Log::WARN);
}

void Service::stop() {
    boost::system::error_code ec;
    socket_acceptor.cancel(ec);
    if (udp_socket.is_open()) {
        udp_socket.cancel(ec);
        udp_socket.close(ec);
    }
    io_service.stop();
}

void Service::async_accept() {
    shared_ptr<Session>session(nullptr);
    if (config.run_type == Config::SERVER) {
        session = make_shared<ServerSession>(config, io_service, ssl_context, auth, plain_http_response);
    } else if (config.run_type == Config::FORWARD) {
        session = make_shared<ForwardSession>(config, io_service, ssl_context);
    } else {
        session = make_shared<ClientSession>(config, io_service, ssl_context);
    }
    socket_acceptor.async_accept(session->accept_socket(), [this, session](const boost::system::error_code error) {
        if (error == boost::asio::error::operation_aborted) {
            // got cancel signal, stop calling myself
            return;
        }
        if (!error) {
            boost::system::error_code ec;
            auto endpoint = session->accept_socket().remote_endpoint(ec);
            if (!ec) {
                Log::log_with_endpoint(endpoint, "incoming connection");
                session->start();
            }
        }
        async_accept();
    });
}

void Service::udp_async_read() {
    udp_socket.async_receive_from(boost::asio::buffer(udp_read_buf, MAX_LENGTH), udp_recv_endpoint, [this](const boost::system::error_code error, size_t length) {
        if (error == boost::asio::error::operation_aborted) {
            // got cancel signal, stop calling myself
            return;
        }
        if (error) {
            stop();
            throw runtime_error(error.message());
        }
        string data((const char *)udp_read_buf, length);
        for (auto it = udp_sessions.begin(); it != udp_sessions.end();) {
            auto next = ++it;
            --it;
            if (it->expired()) {
                udp_sessions.erase(it);
            } else if (it->lock()->process(udp_recv_endpoint, data)) {
                udp_async_read();
                return;
            }
            it = next;
        }
        Log::log_with_endpoint(tcp::endpoint(udp_recv_endpoint.address(), udp_recv_endpoint.port()), "new UDP session");
        auto session = make_shared<UDPForwardSession>(config, io_service, ssl_context, udp_recv_endpoint, [this](const udp::endpoint &endpoint, const string &data) {
            boost::system::error_code ec;
            udp_socket.send_to(boost::asio::buffer(data), endpoint, 0, ec);
            if (ec == boost::asio::error::no_permission) {
                Log::log_with_endpoint(tcp::endpoint(endpoint.address(), endpoint.port()), "dropped a UDP packet due to firewall policy or rate limit");
            } else if (ec) {
                throw runtime_error(ec.message());
            }
        });
        udp_sessions.emplace_back(session);
        session->start();
        session->process(udp_recv_endpoint, data);
        udp_async_read();
    });
}

boost::asio::io_service &Service::service() {
    return io_service;
}

void Service::reload_cert() {
    if (config.run_type == Config::SERVER) {
        Log::log_with_date_time("reloading certificate and private key. . . ", Log::WARN);
        ssl_context.use_certificate_chain_file(config.ssl.cert);
        ssl_context.use_private_key_file(config.ssl.key, context::pem);
        boost::system::error_code ec;
        socket_acceptor.cancel(ec);
        async_accept();
        Log::log_with_date_time("certificate and private key reloaded", Log::WARN);
    } else {
        Log::log_with_date_time("cannot reload certificate and private key: wrong run_type", Log::ERROR);
    }
}

Service::~Service() {
    if (auth) {
        delete auth;
        auth = nullptr;
    }
}
