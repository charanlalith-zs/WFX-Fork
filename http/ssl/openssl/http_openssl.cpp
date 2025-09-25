#ifdef WFX_HTTP_USE_OPENSSL

#include "http_openssl.hpp"
#include "config/config.hpp"
#include "utils/crypt/hash.hpp"
#include "utils/logger/logger.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace WFX::Http {

using namespace WFX::Utils; // For 'Logger'
using namespace WFX::Core;  // For 'Config'

// vvv Constructors and Destructors vvv
HttpOpenSSL::HttpOpenSSL()
{
    auto& logger    = Logger::GetInstance();
    auto& sslConfig = Config::GetInstance().sslConfig;

    // Start of pain and suffering :(
    GlobalOpenSSLInit();

    // Helper lambda to log OpenSSL error before exiting
    auto LogOpenSSLErrorAndExit = [&](const char* message) {
        std::string allErrors;
        unsigned long errCode;
        char errBuf[256];

        while((errCode = ERR_get_error()) != 0) {
            ERR_error_string_n(errCode, errBuf, sizeof(errBuf));
            if(!allErrors.empty())
                allErrors.append("; ");
            allErrors.append(errBuf);
        }

        if(allErrors.empty())
            logger.Fatal("[HttpOpenSSL]: ", message, ". No specific OpenSSL error code available");
        else
            logger.Fatal("[HttpOpenSSL]: ", message, ". OpenSSL Reason(s): ", allErrors);
    };

    // Use the default TLS method, which negotiates the highest common version
    const SSL_METHOD* method = TLS_server_method();
    ctx = SSL_CTX_new(method);
    if(!ctx)
        logger.Fatal("[HttpOpenSSL]: Failed to create SSL_CTX");

    // Level 2 provides 112-bit security disabling weak ciphers and RSA keys < 2048 bits
    SSL_CTX_set_security_level(ctx, 2);

    // Set the minimum protocol version
    if(SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1)
        LogOpenSSLErrorAndExit("Failed to set minimum TLS protocol version");

    // Load certificate and private key
    if(SSL_CTX_use_certificate_chain_file(ctx, sslConfig.certPath.c_str()) <= 0)
        LogOpenSSLErrorAndExit("Failed to load certificate chain file");
    
    if(SSL_CTX_use_PrivateKey_file(ctx, sslConfig.keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
        LogOpenSSLErrorAndExit("Failed to load private key");
    
    if(!SSL_CTX_check_private_key(ctx))
        LogOpenSSLErrorAndExit("Private key does not match certificate");

    // Enable stateless resumption instead of handshaking everytime
    unsigned char ticketKey[80];
    if(!RandomPool::GetInstance().GetBytes(ticketKey, sizeof(ticketKey)))
        logger.Fatal("[HttpOpenSSL]: Failed to generate session ticket encryption key");

    if(SSL_CTX_set_tlsext_ticket_keys(ctx, ticketKey, sizeof(ticketKey)) != 1)
        LogOpenSSLErrorAndExit("Failed to set session ticket keys");
    
    // Set modern cipher preferences
    // This list just defines the order of preference
    const char* tls13Ciphers = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    const char* tls12Ciphers = "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384";
    
    if(SSL_CTX_set_ciphersuites(ctx, tls13Ciphers) != 1)
        LogOpenSSLErrorAndExit("Failed to set TLSv1.3 ciphersuites");

    if(SSL_CTX_set_cipher_list(ctx, tls12Ciphers) != 1)
        LogOpenSSLErrorAndExit("Failed to set TLSv1.2 cipher list");

    // Set remaining essential options
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION | SSL_OP_CIPHER_SERVER_PREFERENCE);

    logger.Info("[HttpOpenSSL]: SSL context initialized successfully");
}

HttpOpenSSL::~HttpOpenSSL()
{
    if(ctx)
        SSL_CTX_free(ctx);
}

// vvv Main Functions vvv
void* HttpOpenSSL::Wrap(SSLSocket sock)
{
    SSL* ssl = SSL_new(ctx);
    if(!ssl)
        return nullptr;

#ifdef _WIN32
    BIO* bio = BIO_new_socket(sock, BIO_NOCLOSE);
    if(!bio) {
        SSL_free(ssl);
        return nullptr;
    }
    SSL_set_bio(ssl, bio, bio);
#else
    if(!SSL_set_fd(ssl, sock)) {
        SSL_free(ssl);
        return nullptr;
    }
#endif

    return ssl;
}

bool HttpOpenSSL::Handshake(void* conn)
{
    SSL* ssl = static_cast<SSL*>(conn);
    int r = SSL_accept(ssl);

    return (r == 1);
}

SSLResult HttpOpenSSL::Read(void* conn, char* buf, int len)
{
    SSL* ssl = static_cast<SSL*>(conn);
    int ret = SSL_read(ssl, buf, len);

    if(ret > 0)
        return { SSLError::SUCCESS, ret };

    int err = SSL_get_error(ssl, ret);
    switch(err) {
        case SSL_ERROR_WANT_READ:   return { SSLError::WANT_READ,  0 };
        case SSL_ERROR_WANT_WRITE:  return { SSLError::WANT_WRITE, 0 };
        case SSL_ERROR_ZERO_RETURN: return { SSLError::CLOSED,     0 };
        case SSL_ERROR_SYSCALL:     return { SSLError::SYSCALL,    0 };
        default:                    return { SSLError::FATAL,      0 };
    }
}

SSLResult HttpOpenSSL::Write(void* conn, const char* buf, int len)
{
    SSL* ssl = static_cast<SSL*>(conn);
    int ret = SSL_write(ssl, buf, len);

    if(ret > 0)
        return { SSLError::SUCCESS, ret };

    int err = SSL_get_error(ssl, ret);
    switch(err) {
        case SSL_ERROR_WANT_READ:   return { SSLError::WANT_READ,  0 };
        case SSL_ERROR_WANT_WRITE:  return { SSLError::WANT_WRITE, 0 };
        case SSL_ERROR_ZERO_RETURN: return { SSLError::CLOSED,     0 };
        case SSL_ERROR_SYSCALL:     return { SSLError::SYSCALL,    0 };
        default:                    return { SSLError::FATAL,      0 };
    }
}

SSLShutdownResult HttpOpenSSL::Shutdown(void* conn)
{
    SSL* ssl = static_cast<SSL*>(conn);
    if(!ssl)
        return SSLShutdownResult::DONE;

    int ret = SSL_shutdown(ssl);

    // SSL_shutdown return values:
    // 1  = success (both sides notified)
    // 0  = shutdown sent, waiting for peer
    // <0 = error, check SSL_get_error()
    if(ret == 1) {
        SSL_free(ssl);
        return SSLShutdownResult::DONE;
    }

    int err = SSL_get_error(ssl, ret);
    if(err == SSL_ERROR_WANT_READ)
        return SSLShutdownResult::WANT_READ;
    if(err == SSL_ERROR_WANT_WRITE)
        return SSLShutdownResult::WANT_WRITE;

    // Any other fatal error
    SSL_free(ssl);
    return SSLShutdownResult::FAILED;
}

// vvv Helper functions vvv
void HttpOpenSSL::GlobalOpenSSLInit()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        if(OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, nullptr) != 1)
            Logger::GetInstance().Fatal("[HttpOpenSSL]: Initialization failed");
    });
}

} // namespace WFX::Http

#endif // WFX_HTTP_USE_OPENSSL