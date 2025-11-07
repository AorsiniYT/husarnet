// PS Vita port implementation
// Uses LwIP for IPv6 support since PS Vita kernel is IPv4-only
// Uses libcurl for HTTP/HTTPS requests

#include "husarnet/ports/port.h"
#include "husarnet/logging.h"

#include <psp2/net/netctl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/sysmodule.h>
#include <psp2/net/http.h>

#include <vector>
#include <map>
#include <ctime>
#include <cstring>
#include <errno.h>
#include <malloc.h>
#include <sys/types.h>
#include "/usr/local/vitasdk/arm-vita-eabi/include/sys/socket.h"

// CRITICAL FIX: Include curl.h BEFORE lwip/netif.h to avoid socket type conflicts
// The issue is that lwip/sockets.h (included by other lwip headers) conflicts with
// curl.h's expectations for socket types. By including curl before lwip,
// curl gets the system socket types correctly.
#include <curl/curl.h>

// LwIP headers for network interface (but NOT lwip/sockets.h to avoid conflicts with curl)
#include <lwip/netif.h>
#include <lwip/ip_addr.h>

namespace {

const char* const STORAGE_DIR = "ux0:data/HUSARNPSV";

// curl/http state
bool g_curl_initialized = false;
bool g_http_module_loaded = false;

// Callback for curl to write response data
static size_t curl_write_callback(void* ptr, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* response = (std::string*)userp;
    response->append((char*)ptr, realsize);
    return realsize;
}

// Helper to load and initialize HTTP module
static bool initHttpModule() {
    if (g_http_module_loaded) {
        return true;
    }

    // Load HTTP module
    int res = sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
    if (res < 0) {
        LOG_ERROR("sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP) failed: 0x%08X", res);
        return false;
    }

    // Initialize HTTP
    res = sceHttpInit(4 * 1024 * 1024);  // 4MB for HTTP
    if (res < 0) {
        LOG_ERROR("sceHttpInit failed: 0x%08X", res);
        return false;
    }

    g_http_module_loaded = true;
    LOG_INFO("HTTP module initialized");
    return true;
}

// Helper to initialize curl on first call
static bool initCurlIfNeeded() {
    if (g_curl_initialized) {
        return true;
    }

    if (!initHttpModule()) {
        return false;
    }

    // Initialize curl globally
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        LOG_ERROR("curl_global_init failed: %s", curl_easy_strerror(res));
        return false;
    }

    g_curl_initialized = true;
    LOG_INFO("curl initialized");
    return true;
}

// Perform HTTP GET request using libcurl
static Port::HttpResult performHttpGet(const std::string& url) {
    if (!initCurlIfNeeded()) {
        return Port::HttpResult{-1, ""};
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return Port::HttpResult{-1, ""};
    }

    std::string response_data;
    struct curl_slist* headers = nullptr;

    // Set common headers
    headers = curl_slist_append(headers, "User-Agent: Husarnet/2.0 (PS Vita)");
    headers = curl_slist_append(headers, "Accept: application/json");

    CURLcode res = CURLE_OK;

    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Skip SSL verification for now
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    LOG_INFO("Performing HTTP GET: %s", url.c_str());

    // Perform the request
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERROR("curl_easy_perform failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return Port::HttpResult{-1, ""};
    }

    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (http_code < 200 || http_code >= 300) {
        LOG_ERROR("HTTP request failed with code: %ld", http_code);
        return Port::HttpResult{(int)http_code, response_data};
    }

    LOG_INFO("HTTP GET success: got %zu bytes", response_data.size());
    return Port::HttpResult{200, response_data};
}

const char* storageFileName(StorageKey key) {
    switch (key) {
        case StorageKey::id:
            return "identity.txt";
        case StorageKey::config:
            return "config.json";
        case StorageKey::cache:
            return "cache.json";
        case StorageKey::daemonApiToken:
            return "daemon_api_token";
        case StorageKey::defaults:
            return "defaults.json";
        default:
            return "storage.dat";
    }
}

std::string storagePath(StorageKey key) {
    std::string path(STORAGE_DIR);
    path.push_back('/');
    path.append(storageFileName(key));
    return path;
}

bool ensureStorageDir() {
    SceIoStat stat{};
    if (sceIoGetstat(STORAGE_DIR, &stat) >= 0) {
        return true;
    }

    int res = sceIoMkdir(STORAGE_DIR, 0777);
    if (res < 0) {
        LOG_WARNING("Failed to create storage directory (%s): 0x%08X", STORAGE_DIR, res);
        return false;
    }

    LOG_INFO("Created storage directory: %s", STORAGE_DIR);
    return true;
}

}  // end anonymous namespace

namespace Port {

}  // namespace Port

namespace Port {

static std::string selfHostname = "psvita";

void init() {
    // PS Vita specific initialization
}

void die(std::string msg) {
    LOG_ERROR("Fatal error: %s", msg.c_str());
    // Exit
}

void threadStart(std::function<void()> func, const char* /*name*/, int /*stack*/, int /*priority*/) {
    // PS Vita threading not implemented, run synchronously
    func();
}

void threadSleep(Time ms) {
    sceKernelDelayThread(ms * 1000); // us
}

etl::map<EnvKey, std::string, ENV_KEY_OPTIONS> getEnvironmentDefaultsFromIniFile() {
    etl::map<EnvKey, std::string, ENV_KEY_OPTIONS> m;
    // Placeholder
    return m;
}

etl::map<EnvKey, std::string, ENV_KEY_OPTIONS> getEnvironmentOverrides() {
    etl::map<EnvKey, std::string, ENV_KEY_OPTIONS> m;
    // Placeholder
    return m;
}

void notifyReady() {
    // Placeholder
}

void log(const LogLevel level, const std::string& message) {
    sceClibPrintf("[%d] %s\n", static_cast<int>(level), message.c_str());
}

Time getCurrentTime() {
    return sceKernelGetSystemTimeWide() / 1000; // ms
}

const std::string getHumanTime() {
    time_t now = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

IpAddress getIpAddressFromInterfaceName(const std::string& /*interfaceName*/) {
    return IpAddress();
}

std::vector<IpAddress> getLocalAddresses() {
    return {};
}

UpperLayer* startTun(const HusarnetAddress& myAddress, const std::string& interfaceName) {
    (void)interfaceName;
    
    // Convert HusarnetAddress (fc94::/16) to LwIP IPv6 address structure
    ip6_addr_t ip;
    memcpy(ip.addr, myAddress.data.data(), 16);

    auto tunTap = new Tun(ip, 32);
    return tunTap;
}

void processSocketEvents(void* tuntap) {
    // Process LwIP events and queue outgoing packets
    // No traditional OsSocket needed since we're using LwIP
    if (tuntap) {
        static_cast<Tun*>(tuntap)->processQueuedPackets();
    }
}

std::string getSelfHostname() {
    return selfHostname;
}

bool setSelfHostname(const std::string& newHostname) {
    selfHostname = newHostname;
    return true;
}

void updateHostsFile(const std::map<std::string, HusarnetAddress>& data) {
    // Placeholder
    (void)data;
}

IpAddress resolveToIp(const std::string& hostname) {
    // Placeholder DNS
    (void)hostname;
    return IpAddress();
}

bool runHook(HookType hookType) {
    (void)hookType;
    return true;
}

HttpResult httpGet(const std::string& url, const std::string& path) {
    // Construct full URL
    std::string full_url = "https://" + url + path;
    LOG_INFO("httpGet: %s", full_url.c_str());
    return performHttpGet(full_url);
}

HttpResult httpGet(const IpAddress& ip, const std::string& path) {
    // Construct URL using IP address string
    // The IpAddress::toString() method returns the IPv6 address in standard format
    std::string full_url = "https://[" + ip.toString() + "]:443" + path;
    LOG_INFO("httpGet (IP): %s", full_url.c_str());
    return performHttpGet(full_url);
}

HttpResult httpPost(const std::string& url, const std::string& path, const std::string& body) {
    // TODO: Implement HTTP POST with proper headers
    LOG_ERROR("httpPost: Not yet implemented on PS Vita");
    return {-1, ""};
}

HttpResult httpPost(const IpAddress& ip, const std::string& path, const std::string& body) {
    // TODO: Implement HTTP POST with proper headers
    LOG_ERROR("httpPost (with IP): Not yet implemented on PS Vita");
    return {-1, ""};
}

std::string readStorage(StorageKey key) {
    if (!ensureStorageDir()) {
        return "";
    }

    auto path = storagePath(key);

    SceIoStat stat{};
    if (sceIoGetstat(path.c_str(), &stat) < 0 || stat.st_size <= 0) {
        LOG_INFO("Storage file not found or empty: %s", path.c_str());
        return "";
    }

    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        LOG_WARNING("Failed to open storage file for read (%s): 0x%08X", path.c_str(), fd);
        return "";
    }

    size_t size = static_cast<size_t>(stat.st_size);
    std::string result;
    result.resize(size);

    size_t totalRead = 0;
    while (totalRead < size) {
        SceSSize chunk = sceIoRead(fd, &result[totalRead], size - totalRead);
        if (chunk < 0) {
            LOG_WARNING("Failed while reading storage file (%s): 0x%08X", path.c_str(), static_cast<int>(chunk));
            result.clear();
            break;
        }
        if (chunk == 0) {
            result.resize(totalRead);
            break;
        }
        totalRead += static_cast<size_t>(chunk);
    }

    sceIoClose(fd);
    LOG_INFO("Read %u bytes from storage: %s", static_cast<unsigned>(result.size()), path.c_str());
    return result;
}

bool writeStorage(StorageKey key, const std::string& data) {
    if (!ensureStorageDir()) {
        return false;
    }

    auto path = storagePath(key);
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        LOG_WARNING("Failed to open storage file for write (%s): 0x%08X", path.c_str(), fd);
        return false;
    }

    size_t totalWritten = 0;
    const char* buffer = data.data();
    size_t remaining = data.size();

    while (remaining > 0) {
        SceSSize chunk = sceIoWrite(fd, buffer + totalWritten, remaining);
        if (chunk < 0) {
            LOG_WARNING("Failed while writing storage file (%s): 0x%08X", path.c_str(), static_cast<int>(chunk));
            sceIoClose(fd);
            return false;
        }
        totalWritten += static_cast<size_t>(chunk);
        remaining -= static_cast<size_t>(chunk);
    }

    sceIoClose(fd);
    LOG_INFO("Wrote %u bytes to storage: %s", static_cast<unsigned>(data.size()), path.c_str());
    return true;
}

}  // namespace Port