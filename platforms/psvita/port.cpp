// PS Vita port implementation
// Adapted for IPv4-only

#include "husarnet/ports/psvita/port.h"
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

// Missing types for PS Vita
enum class StorageKey { id, config, cache, daemonApiToken, defaults };
struct HttpResult { int status; std::string body; };
class OsSocket {
public:
    static void runOnce(int ms) { (void)ms; }
};

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
    // PS Vita does not support HTTP requests
    (void)ip; (void)hostHeader; (void)path; (void)method; (void)body; (void)contentType;
    LOG_ERROR("httpRequest: not implemented on PS Vita");
    return Port::HttpResult{-1, ""};
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

std::map<EnvKey, std::string> getEnvironmentDefaultsFromIniFile() {
    std::map<EnvKey, std::string> m;
    // Placeholder
    return m;
}

std::map<EnvKey, std::string> getEnvironmentOverrides() {
    std::map<EnvKey, std::string> m;
    // Placeholder
    return m;
}

void notifyReady() {
    // Placeholder
}

void log(const LogLevel level, const std::string& message) {
    sceClibPrintf("[%d] %s\n", (int)level, message.c_str());
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

UpperLayer* startTun(const IpAddress& myAddress, const std::string& interfaceName) {
    (void)interfaceName;
    
    // Convert HusarnetAddress (fc94::/16) to LwIP IPv6 address structure
    ip6_addr_t ip;
    memcpy(&ip.addr, myAddress.data.data(), 16);

    auto tunTap = new Tun(ip, 32);
    return tunTap;
}

void processSocketEvents(void* tuntap) {
    // Process socket events and queue outgoing packets
    // OsSocket::runOnce(20);  // process socket events for at most 20 ms
    static_cast<Tun*>(tuntap)->processQueuedPackets();
}

std::string getSelfHostname() {
    return selfHostname;
}

bool setSelfHostname(const std::string& newHostname) {
    selfHostname = newHostname;
    return true;
}

void updateHostsFile(const std::map<std::string, IpAddress>& data) {
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
    (void)url; (void)path;
    LOG_ERROR("httpGet: not implemented on PS Vita");
    return {-1, ""};
}

HttpResult httpGet(const IpAddress& ip, const std::string& path) {
    (void)ip; (void)path;
    LOG_ERROR("httpGet: not implemented on PS Vita");
    return {-1, ""};
}

HttpResult httpPost(const std::string& url, const std::string& path, const std::string& body) {
    (void)url; (void)path; (void)body;
    LOG_ERROR("httpPost: not implemented on PS Vita");
    return {-1, ""};
}

HttpResult httpPost(const IpAddress& ip, const std::string& path, const std::string& body) {
    (void)ip; (void)path; (void)body;
    LOG_ERROR("httpPost: not implemented on PS Vita");
    return {-1, ""};
}

std::string readStorage(StorageKey key) {
    (void)key;
    LOG_INFO("readStorage: not implemented on PS Vita");
    return "";
}

bool writeStorage(StorageKey key, const std::string& data) {
    (void)key; (void)data;
    LOG_INFO("writeStorage: not implemented on PS Vita");
    return false;
}

}  // namespace Port