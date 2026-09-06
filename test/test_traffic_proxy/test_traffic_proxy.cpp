/**
 * @file test_traffic_proxy.cpp
 * @brief Unit tests for the Traffic Proxy module
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <vector>

// =============================================================================
// Local type definitions for native testing (avoid ESP32 dependencies)
// =============================================================================

namespace adversary {

struct DNSQuery {
    uint32_t timestamp = 0;
    uint8_t clientMac[6] = {0};
    char domain[128] = {0};
    
    void reset() {
        timestamp = 0;
        memset(clientMac, 0, 6);
        memset(domain, 0, sizeof(domain));
    }
};

struct HTTPCapture {
    uint32_t timestamp = 0;
    uint8_t clientMac[6] = {0};
    char method[8] = {0};
    char host[64] = {0};
    char path[128] = {0};
    char cookies[256] = {0};
    char postData[512] = {0};
    uint16_t postLen = 0;
    
    void reset() {
        timestamp = 0;
        memset(clientMac, 0, 6);
        memset(method, 0, sizeof(method));
        memset(host, 0, sizeof(host));
        memset(path, 0, sizeof(path));
        memset(cookies, 0, sizeof(cookies));
        memset(postData, 0, sizeof(postData));
        postLen = 0;
    }
};

struct TrafficStats {
    uint32_t totalPackets = 0;
    uint32_t bytesForwarded = 0;
    uint32_t dnsQueries = 0;
    uint32_t httpRequests = 0;
    uint32_t httpsConnections = 0;
    uint32_t uniqueDomains = 0;
    
    void reset() {
        totalPackets = 0;
        bytesForwarded = 0;
        dnsQueries = 0;
        httpRequests = 0;
        httpsConnections = 0;
        uniqueDomains = 0;
    }
};

// =============================================================================
// DNS Parsing Functions (extracted for testing)
// =============================================================================

/**
 * @brief Parse DNS query name from DNS packet data
 * @param data DNS packet data (starting at DNS header)
 * @param len Length of DNS packet
 * @param domain Output buffer for domain name
 * @param domainLen Size of output buffer
 * @return true if successfully parsed
 */
bool parseDNSQuery(const uint8_t* data, uint16_t len, char* domain, size_t domainLen) {
    // DNS header is 12 bytes, question section starts at offset 12
    if (len < 17) return false;
    
    const uint8_t* ptr = data + 12;  // Skip header
    size_t remaining = len - 12;
    size_t domainPos = 0;
    
    // Parse DNS name (labels)
    while (remaining > 0 && *ptr != 0) {
        uint8_t labelLen = *ptr++;
        remaining--;
        
        if (labelLen > remaining || labelLen > 63) {
            return false;  // Invalid
        }
        
        // Add dot separator
        if (domainPos > 0 && domainPos < domainLen - 1) {
            domain[domainPos++] = '.';
        }
        
        // Copy label
        for (uint8_t i = 0; i < labelLen && domainPos < domainLen - 1; i++) {
            domain[domainPos++] = (char)*ptr++;
            remaining--;
        }
    }
    
    domain[domainPos] = '\0';
    return domainPos > 0;
}

// =============================================================================
// HTTP Parsing Functions (extracted for testing)
// =============================================================================

/**
 * @brief Parse HTTP request to extract method, host, path, cookies
 */
bool parseHTTPRequest(const uint8_t* data, uint16_t len, HTTPCapture& capture) {
    if (len < 16) return false;
    
    const char* text = reinterpret_cast<const char*>(data);
    capture.reset();
    
    // Check for HTTP request
    bool isHTTP = (strncmp(text, "GET ", 4) == 0 ||
                   strncmp(text, "POST ", 5) == 0 ||
                   strncmp(text, "HEAD ", 5) == 0 ||
                   strncmp(text, "PUT ", 4) == 0);
    
    if (!isHTTP) return false;
    
    // Extract method
    const char* space = strchr(text, ' ');
    if (space && (space - text) < 8) {
        size_t methodLen = space - text;
        strncpy(capture.method, text, methodLen);
        capture.method[methodLen] = '\0';
        
        // Extract path
        const char* pathStart = space + 1;
        const char* pathEnd = strchr(pathStart, ' ');
        if (pathEnd) {
            size_t pathLen = pathEnd - pathStart;
            if (pathLen > sizeof(capture.path) - 1) {
                pathLen = sizeof(capture.path) - 1;
            }
            strncpy(capture.path, pathStart, pathLen);
            capture.path[pathLen] = '\0';
        }
    }
    
    // Extract Host header
    const char* hostHeader = strstr(text, "Host: ");
    if (!hostHeader) hostHeader = strstr(text, "host: ");
    if (hostHeader) {
        hostHeader += 6;
        const char* hostEnd = strstr(hostHeader, "\r\n");
        if (hostEnd) {
            size_t hostLen = hostEnd - hostHeader;
            if (hostLen > sizeof(capture.host) - 1) {
                hostLen = sizeof(capture.host) - 1;
            }
            strncpy(capture.host, hostHeader, hostLen);
            capture.host[hostLen] = '\0';
        }
    }
    
    // Extract Cookie header
    const char* cookieHeader = strstr(text, "Cookie: ");
    if (!cookieHeader) cookieHeader = strstr(text, "cookie: ");
    if (cookieHeader) {
        cookieHeader += 8;
        const char* cookieEnd = strstr(cookieHeader, "\r\n");
        if (cookieEnd) {
            size_t cookieLen = cookieEnd - cookieHeader;
            if (cookieLen > sizeof(capture.cookies) - 1) {
                cookieLen = sizeof(capture.cookies) - 1;
            }
            strncpy(capture.cookies, cookieHeader, cookieLen);
            capture.cookies[cookieLen] = '\0';
        }
    }
    
    return true;
}

} // namespace adversary

using namespace adversary;

// =============================================================================
// Sample DNS Packets
// =============================================================================

// DNS query for "example.com"
// Header (12 bytes) + Question section
static const uint8_t DNS_QUERY_EXAMPLE_COM[] = {
    // DNS Header (12 bytes)
    0x00, 0x01,  // Transaction ID
    0x01, 0x00,  // Flags: Standard query
    0x00, 0x01,  // Questions: 1
    0x00, 0x00,  // Answer RRs: 0
    0x00, 0x00,  // Authority RRs: 0
    0x00, 0x00,  // Additional RRs: 0
    // Question: example.com
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',  // "example" (7 chars)
    0x03, 'c', 'o', 'm',                       // "com" (3 chars)
    0x00,                                       // End of name
    0x00, 0x01,  // Type: A
    0x00, 0x01   // Class: IN
};

// DNS query for "www.google.com"
static const uint8_t DNS_QUERY_GOOGLE[] = {
    // DNS Header (12 bytes)
    0x00, 0x02,  // Transaction ID
    0x01, 0x00,  // Flags
    0x00, 0x01,  // Questions: 1
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Other counts
    // Question: www.google.com
    0x03, 'w', 'w', 'w',
    0x06, 'g', 'o', 'o', 'g', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x01, 0x00, 0x01
};

// Too short DNS packet
static const uint8_t DNS_TOO_SHORT[] = {
    0x00, 0x01, 0x01, 0x00, 0x00, 0x01
};

// =============================================================================
// Sample HTTP Requests
// =============================================================================

static const char HTTP_GET_REQUEST[] = 
    "GET /index.html HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "User-Agent: Mozilla/5.0\r\n"
    "Cookie: session=abc123; user=test\r\n"
    "\r\n";

static const char HTTP_POST_REQUEST[] = 
    "POST /login HTTP/1.1\r\n"
    "Host: secure.example.com\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Cookie: csrf=xyz789\r\n"
    "\r\n"
    "username=admin&password=secret";

static const char HTTP_HEAD_REQUEST[] =
    "HEAD /api/status HTTP/1.1\r\n"
    "host: api.example.com\r\n"
    "\r\n";

static const char NOT_HTTP[] = "This is not an HTTP request\r\n";

// =============================================================================
// DNS Parsing Tests
// =============================================================================

void test_dns_parse_example_com() {
    char domain[128] = {0};
    bool result = parseDNSQuery(DNS_QUERY_EXAMPLE_COM, sizeof(DNS_QUERY_EXAMPLE_COM), 
                                 domain, sizeof(domain));
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("example.com", domain);
}

void test_dns_parse_google() {
    char domain[128] = {0};
    bool result = parseDNSQuery(DNS_QUERY_GOOGLE, sizeof(DNS_QUERY_GOOGLE), 
                                 domain, sizeof(domain));
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("www.google.com", domain);
}

void test_dns_parse_too_short() {
    char domain[128] = {0};
    bool result = parseDNSQuery(DNS_TOO_SHORT, sizeof(DNS_TOO_SHORT), 
                                 domain, sizeof(domain));
    TEST_ASSERT_FALSE(result);
}

void test_dns_parse_empty() {
    char domain[128] = {0};
    bool result = parseDNSQuery(nullptr, 0, domain, sizeof(domain));
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// HTTP Parsing Tests
// =============================================================================

void test_http_parse_get_request() {
    HTTPCapture capture;
    bool result = parseHTTPRequest(
        reinterpret_cast<const uint8_t*>(HTTP_GET_REQUEST),
        strlen(HTTP_GET_REQUEST), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("GET", capture.method);
    TEST_ASSERT_EQUAL_STRING("/index.html", capture.path);
    TEST_ASSERT_EQUAL_STRING("example.com", capture.host);
    TEST_ASSERT_EQUAL_STRING("session=abc123; user=test", capture.cookies);
}

void test_http_parse_post_request() {
    HTTPCapture capture;
    bool result = parseHTTPRequest(
        reinterpret_cast<const uint8_t*>(HTTP_POST_REQUEST),
        strlen(HTTP_POST_REQUEST), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("POST", capture.method);
    TEST_ASSERT_EQUAL_STRING("/login", capture.path);
    TEST_ASSERT_EQUAL_STRING("secure.example.com", capture.host);
    TEST_ASSERT_EQUAL_STRING("csrf=xyz789", capture.cookies);
}

void test_http_parse_head_request() {
    HTTPCapture capture;
    bool result = parseHTTPRequest(
        reinterpret_cast<const uint8_t*>(HTTP_HEAD_REQUEST),
        strlen(HTTP_HEAD_REQUEST), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("HEAD", capture.method);
    TEST_ASSERT_EQUAL_STRING("/api/status", capture.path);
    TEST_ASSERT_EQUAL_STRING("api.example.com", capture.host);
}

// =============================================================================
// POST Body Parsing Tests
// =============================================================================

static const char HTTP_POST_WITH_BODY[] = 
    "POST /login HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Content-Type: application/x-www-form-urlencoded\r\n"
    "Content-Length: 35\r\n"
    "\r\n"
    "username=admin&password=supersecret";

static const char HTTP_POST_JSON_BODY[] = 
    "POST /api/auth HTTP/1.1\r\n"
    "Host: api.example.com\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 40\r\n"
    "\r\n"
    "{\"username\":\"admin\",\"password\":\"secret\"}";

// Parser with POST body support (mirrors traffic_proxy.cpp)
bool parseHTTPRequestWithBody(const uint8_t* data, uint16_t len, HTTPCapture& capture) {
    if (!parseHTTPRequest(data, len, capture)) return false;
    
    const char* text = reinterpret_cast<const char*>(data);
    
    // Extract POST body (for POST and PUT requests)
    if (strcmp(capture.method, "POST") == 0 || strcmp(capture.method, "PUT") == 0) {
        // Find Content-Length header
        uint16_t contentLength = 0;
        const char* clHeader = strstr(text, "Content-Length: ");
        if (!clHeader) clHeader = strstr(text, "content-length: ");
        if (clHeader) {
            clHeader += 16;
            contentLength = (uint16_t)atoi(clHeader);
        }
        
        // Find end of headers
        const char* bodyStart = strstr(text, "\r\n\r\n");
        if (bodyStart) {
            bodyStart += 4;
            
            size_t headerLen = bodyStart - text;
            if (headerLen < len) {
                size_t availableBodyLen = len - headerLen;
                size_t bodyLen = contentLength > 0 ? 
                    (contentLength < availableBodyLen ? contentLength : availableBodyLen) : 
                    availableBodyLen;
                
                if (bodyLen > sizeof(capture.postData) - 1) {
                    bodyLen = sizeof(capture.postData) - 1;
                }
                
                if (bodyLen > 0) {
                    memcpy(capture.postData, bodyStart, bodyLen);
                    capture.postData[bodyLen] = '\0';
                    capture.postLen = (uint16_t)bodyLen;
                }
            }
        }
    }
    
    return true;
}

void test_http_parse_post_body_form_data() {
    HTTPCapture capture;
    bool result = parseHTTPRequestWithBody(
        reinterpret_cast<const uint8_t*>(HTTP_POST_WITH_BODY),
        strlen(HTTP_POST_WITH_BODY), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("POST", capture.method);
    TEST_ASSERT_EQUAL_STRING("/login", capture.path);
    TEST_ASSERT_EQUAL_STRING("example.com", capture.host);
    TEST_ASSERT_EQUAL(35, capture.postLen);
    TEST_ASSERT_EQUAL_STRING("username=admin&password=supersecret", capture.postData);
}

void test_http_parse_post_body_json() {
    HTTPCapture capture;
    bool result = parseHTTPRequestWithBody(
        reinterpret_cast<const uint8_t*>(HTTP_POST_JSON_BODY),
        strlen(HTTP_POST_JSON_BODY), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("POST", capture.method);
    TEST_ASSERT_EQUAL_STRING("/api/auth", capture.path);
    TEST_ASSERT_EQUAL(40, capture.postLen);  // Fixed: actual JSON body length
    TEST_ASSERT_TRUE(strstr(capture.postData, "admin") != NULL);
    TEST_ASSERT_TRUE(strstr(capture.postData, "secret") != NULL);
}

void test_http_parse_get_has_no_body() {
    HTTPCapture capture;
    bool result = parseHTTPRequestWithBody(
        reinterpret_cast<const uint8_t*>(HTTP_GET_REQUEST),
        strlen(HTTP_GET_REQUEST), capture);
    
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0, capture.postLen);
    TEST_ASSERT_EQUAL_STRING("", capture.postData);
}

void test_http_parse_not_http() {
    HTTPCapture capture;
    bool result = parseHTTPRequest(
        reinterpret_cast<const uint8_t*>(NOT_HTTP),
        strlen(NOT_HTTP), capture);
    
    TEST_ASSERT_FALSE(result);
}

void test_http_parse_too_short() {
    HTTPCapture capture;
    const char* short_data = "GET";
    bool result = parseHTTPRequest(
        reinterpret_cast<const uint8_t*>(short_data),
        strlen(short_data), capture);
    
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// Data Structure Tests
// =============================================================================

void test_dns_query_reset() {
    DNSQuery query;
    query.timestamp = 12345;
    query.clientMac[0] = 0xAA;
    strcpy(query.domain, "test.com");
    
    query.reset();
    
    TEST_ASSERT_EQUAL(0, query.timestamp);
    TEST_ASSERT_EQUAL(0, query.clientMac[0]);
    TEST_ASSERT_EQUAL_STRING("", query.domain);
}

void test_http_capture_reset() {
    HTTPCapture capture;
    capture.timestamp = 12345;
    strcpy(capture.method, "GET");
    strcpy(capture.host, "test.com");
    strcpy(capture.path, "/path");
    strcpy(capture.cookies, "cookie=value");
    
    capture.reset();
    
    TEST_ASSERT_EQUAL(0, capture.timestamp);
    TEST_ASSERT_EQUAL_STRING("", capture.method);
    TEST_ASSERT_EQUAL_STRING("", capture.host);
    TEST_ASSERT_EQUAL_STRING("", capture.path);
    TEST_ASSERT_EQUAL_STRING("", capture.cookies);
}

void test_traffic_stats_reset() {
    TrafficStats stats;
    stats.totalPackets = 100;
    stats.dnsQueries = 50;
    stats.httpRequests = 30;
    stats.uniqueDomains = 10;
    
    stats.reset();
    
    TEST_ASSERT_EQUAL(0, stats.totalPackets);
    TEST_ASSERT_EQUAL(0, stats.dnsQueries);
    TEST_ASSERT_EQUAL(0, stats.httpRequests);
    TEST_ASSERT_EQUAL(0, stats.uniqueDomains);
}

// =============================================================================
// Test Runner
// =============================================================================

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    
    // DNS parsing tests
    RUN_TEST(test_dns_parse_example_com);
    RUN_TEST(test_dns_parse_google);
    RUN_TEST(test_dns_parse_too_short);
    RUN_TEST(test_dns_parse_empty);
    
    // HTTP parsing tests
    RUN_TEST(test_http_parse_get_request);
    RUN_TEST(test_http_parse_post_request);
    RUN_TEST(test_http_parse_head_request);
    RUN_TEST(test_http_parse_not_http);
    RUN_TEST(test_http_parse_too_short);
    
    // POST body parsing tests
    RUN_TEST(test_http_parse_post_body_form_data);
    RUN_TEST(test_http_parse_post_body_json);
    RUN_TEST(test_http_parse_get_has_no_body);
    
    // Data structure tests
    RUN_TEST(test_dns_query_reset);
    RUN_TEST(test_http_capture_reset);
    RUN_TEST(test_traffic_stats_reset);
    
    return UNITY_END();
}
