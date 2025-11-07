// PS Vita port implementation
// Adapted for IPv4-only

#include "husarnet/ports/port.h"
#include "husarnet/logging.h"

#include <psp2/net/netctl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <vector>
#include <map>
#include <ctime>
#include <cstring>
#include <errno.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace {

const char* const STORAGE_DIR = "ux0:data/HUSARNPSV";

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

Port::HttpResult httpRequest(const IpAddress& ip, const std::string& hostHeader, const std::string& path, const std::string& method, const std::string& body, const char* contentType) {
    if (!ip.isMappedV4()) {
        LOG_ERROR("httpRequest: IPv6 not implemented on PS Vita (host: %s)", hostHeader.c_str());
        return Port::HttpResult{-1, ""};
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("httpRequest: socket() failed (%s)", strerror(errno));
        return Port::HttpResult{-1, ""};
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);

    uint32_t hostOrder = 0;
    memcpy(&hostOrder, ip.data.data() + 12, sizeof(hostOrder));
    server_addr.sin_addr.s_addr = htonl(hostOrder);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("httpRequest: connect() to %s failed (%s)", hostHeader.c_str(), strerror(errno));
        close(sock);
        return Port::HttpResult{-1, ""};
    }

    std::string requestPath = path.empty() ? std::string("/") : path;

    std::string request;
    request.reserve(method.size() + requestPath.size() + body.size() + 64);
    request.append(method);
    request.push_back(' ');
    request.append(requestPath);
    request.append(" HTTP/1.1\r\nHost: ");
    request.append(hostHeader);
    request.append("\r\nConnection: close\r\n");
    if (contentType && !body.empty()) {
        request.append("Content-Type: ");
        request.append(contentType);
        request.append("\r\n");
    }
    if (!body.empty()) {
        request.append("Content-Length: ");
        request.append(std::to_string(body.size()));
        request.append("\r\n");
    }
    request.append("\r\n");
    request.append(body);

    size_t totalWritten = 0;
    while (totalWritten < request.size()) {
        ssize_t chunk = write(sock, request.data() + totalWritten, request.size() - totalWritten);
        if (chunk <= 0) {
            LOG_ERROR("httpRequest: write() failed (%s)", strerror(errno));
            close(sock);
            return Port::HttpResult{-1, ""};
        }
        totalWritten += static_cast<size_t>(chunk);
    }

    std::string response;
    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(sock, buffer, sizeof(buffer))) > 0) {
        response.append(buffer, bytes_read);
    }

    if (bytes_read < 0) {
        LOG_ERROR("httpRequest: read() failed (%s)", strerror(errno));
        close(sock);
        return Port::HttpResult{-1, ""};
    }

    close(sock);

    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        LOG_ERROR("httpRequest: response missing header terminator");
        return Port::HttpResult{-1, ""};
    }

    std::string headers = response.substr(0, header_end);
    std::string body_resp = response.substr(header_end + 4);

    size_t status_start = headers.find(' ');
    if (status_start == std::string::npos) {
        LOG_ERROR("httpRequest: malformed status line");
        return Port::HttpResult{-1, ""};
    }
    size_t status_end = headers.find(' ', status_start + 1);
    if (status_end == std::string::npos) {
        LOG_ERROR("httpRequest: malformed status code");
        return Port::HttpResult{-1, ""};
    }

    int status_code = std::stoi(headers.substr(status_start + 1, status_end - status_start - 1));
    return Port::HttpResult{status_code, body_resp};
}

}  // namespace

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
    // Process socket events and queue outgoing packets
    OsSocket::runOnce(20);  // process socket events for at most 20 ms
    static_cast<Tun*>(tuntap)->processQueuedPackets();
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
    struct hostent* host = gethostbyname(url.c_str());
    if (!host || !host->h_addr_list[0]) {
        LOG_ERROR("httpGet: failed to resolve %s", url.c_str());
        return {-1, ""};
    }

    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    IpAddress ip = IpAddress::fromBinary4(ntohl(addr.s_addr));
    return httpRequest(ip, url, path, "GET", "", nullptr);
}

HttpResult httpGet(const IpAddress& ip, const std::string& path) {
    return httpRequest(ip, ip.toString(), path, "GET", "", nullptr);
}

HttpResult httpPost(const std::string& url, const std::string& path, const std::string& body) {
    struct hostent* host = gethostbyname(url.c_str());
    if (!host || !host->h_addr_list[0]) {
        LOG_ERROR("httpPost: failed to resolve %s", url.c_str());
        return {-1, ""};
    }

    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    IpAddress ip = IpAddress::fromBinary4(ntohl(addr.s_addr));
    return httpRequest(ip, url, path, "POST", body, "application/json");
}

HttpResult httpPost(const IpAddress& ip, const std::string& path, const std::string& body) {
    return httpRequest(ip, ip.toString(), path, "POST", body, "application/json");
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