// Copyright (c) 2025 Husarnet sp. z o.o.
// Authors: listed in project_root/README.md
// License: specified in project_root/LICENSE.txt
#include "husarnet/config_manager.h"

#include <algorithm>
#include <cctype>

#include <etl/string.h>
#include <sockets.h>

#include "husarnet/dashboardapi/response.h"
#include "husarnet/husarnet_config.h"
#include "husarnet/licensing.h"

#include "ngsocket_messages.h"

namespace {
  std::string extractHostFromUrl(const std::string& url)
  {
    if(url.empty()) {
      return {};
    }

    size_t start = 0;
    auto schemePos = url.find("://");
    if(schemePos != std::string::npos) {
      start = schemePos + 3;
    }

    if(start >= url.size()) {
      return {};
    }

    auto hostPort = url.substr(start);
    auto slashPos = hostPort.find('/');
    if(slashPos != std::string::npos) {
      hostPort = hostPort.substr(0, slashPos);
    }

    // Trim whitespace just in case
    auto trimIt = std::find_if_not(hostPort.begin(), hostPort.end(), [](unsigned char ch) {
      return std::isspace(ch) != 0;
    });
    hostPort.erase(hostPort.begin(), trimIt);
    while(!hostPort.empty() && std::isspace(static_cast<unsigned char>(hostPort.back())) != 0) {
      hostPort.pop_back();
    }

    return hostPort;
  }
}  // namespace

ConfigManager::ConfigManager(HooksManager* hooksManager, const ConfigEnv* configEnv, HusarnetAddress ourIp)
    : hooksManager(hooksManager),
      configEnv(configEnv),
      ourIp(ourIp),
      nextLicenseUpdate(std::chrono::steady_clock::now()),
      nextGetConfigUpdate(std::chrono::steady_clock::now())
{
  std::lock_guard lgFast(this->mutexFast);
  if(!configEnv->getEnableControlplane()) {
    this->allowEveryone = true;
    LOG_WARNING("ConfigManagerDev: control plane is disabled, any peer will be let through")
  }
}

void ConfigManager::getLicense()
{
  auto tldAddress = this->configEnv->getTldFqdn();
  auto [statusCode, bytes] = Port::httpGet(tldAddress, "/license.json");

  if(statusCode != 200) {
    LOG_ERROR("failed to download license file (using tld address %s)", tldAddress.c_str());
    return;
  }

  auto licenseJson = json::parse(bytes, nullptr, false);
  if(!isLicenseValid(licenseJson)) {
    LOG_ERROR("license file downloaded from %s is invalid", tldAddress.c_str());
    return;
  }

  this->storeLicense(licenseJson);
  this->updateLicenseData();
  this->nextLicenseUpdate = std::chrono::steady_clock::now() + licenseRefreshPeriod;
}

void ConfigManager::storeLicense(const nlohmann::json& jsonDoc)
{
  std::lock_guard lgSlow(this->mutexSlow);
  this->cacheJson[CACHE_KEY_LICENSE] = jsonDoc;
  this->cacheDirty.store(true, std::memory_order_relaxed);
}

// TODO: discuss: might also be renamed to updateControlPlaneData
void ConfigManager::updateLicenseData()
{
  std::lock_guard lgSlow(this->mutexSlow);
  std::lock_guard lgFast(this->mutexFast);
  const auto& licenseJson = this->cacheJson[CACHE_KEY_LICENSE];
  if(licenseJson.is_discarded() || licenseJson.is_null()) {
    LOG_ERROR("ConfigManagerDev: no license information available");
    return;
  }

  const auto& ebServerIps = licenseJson[LICENSE_EB_SERVERS_KEY].get<std::vector<std::string>>();
  this->ebAddresses.clear();
  for(size_t idx = 0; idx < ebServerIps.size(); ++idx) {
    auto& ipStr = ebServerIps[idx];
    const auto ip = HusarnetAddress::parse(ipStr);
    this->ebAddresses.push_back(ip);
    this->allowedPeers.insert(ip);
    LOG_INFO(
        "ConfigManagerDev: license EB address[%u] %s mappedV4=%d",
        static_cast<unsigned>(idx),
        ipStr.c_str(),
        ip.isMappedV4());
  }

  const auto& apiServerIps = licenseJson[LICENSE_API_SERVERS_KEY].get<std::vector<std::string>>();

  this->apiAddresses.clear();
  for(size_t idx = 0; idx < apiServerIps.size(); ++idx) {
    auto& ipStr = apiServerIps[idx];
    const auto ip = HusarnetAddress::parse(ipStr);
    this->apiAddresses.push_back(ip);
    this->allowedPeers.insert(ip);
    LOG_INFO(
        "ConfigManagerDev: license API address[%u] %s mappedV4=%d",
        static_cast<unsigned>(idx),
        ipStr.c_str(),
        ip.isMappedV4());
  }

  const auto baseServerIps = licenseJson[LICENSE_BASE_SERVER_ADDRESSES_KEY].get<std::vector<std::string>>();

  this->baseAddresses.clear();
  for(size_t idx = 0; idx < baseServerIps.size(); ++idx) {
    auto& ip = baseServerIps[idx];
    auto parsed = InternetAddress::parse(ip);
    this->baseAddresses.push_back(parsed);
    LOG_INFO(
        "ConfigManagerDev: license base address[%u] %s mappedV4=%d",
        static_cast<unsigned>(idx),
        ip.c_str(),
        parsed.isMappedV4());
  }

  if(licenseJson.contains(LICENSE_DASHBOARD_URL_KEY) && licenseJson[LICENSE_DASHBOARD_URL_KEY].is_string()) {
    auto url = licenseJson[LICENSE_DASHBOARD_URL_KEY].get<std::string>();
    this->dashboardHost = extractHostFromUrl(url);
    LOG_INFO(
        "ConfigManagerDev: license dashboard host %s from %s",
        this->dashboardHost.c_str(),
        url.c_str());
  } else {
    this->dashboardHost.clear();
  }
}

HusarnetAddress ConfigManager::getApiAddress() const
{
  std::lock_guard lgFast(this->mutexFast);
  if(this->apiAddresses.empty()) {
    return {};
  }
  auto addr = this->apiAddresses[0];
  return addr;
}

HusarnetAddress ConfigManager::getEbAddress() const
{
  std::lock_guard lgFast(this->mutexFast);
  if(this->ebAddresses.empty()) {
    return {};
  }
  auto addr = this->ebAddresses[0];
  return addr;
}

void ConfigManager::getGetConfig()
{
  auto now = std::chrono::steady_clock::now();
  auto processResponse = [&](dashboardapi::Response response, const char* sourceLabel) {
    if(response.isSuccessful()) {
      this->storeGetConfig(response.getPayloadJson());
      this->updateGetConfigData();
      this->nextGetConfigUpdate = now + getConfigRefreshPeriod;
      LOG_INFO("ConfigManagerDev: dashboard fetch via %s succeeded", sourceLabel);
      return true;
    }

    LOG_ERROR(
        "ConfigManagerDev: dashboard fetch via %s failed, details: %s",
        sourceLabel,
        response.toString().c_str());
    this->nextGetConfigUpdate = now + getConfigRetryPeriod;
    return false;
  };

  auto apiAddress = this->getApiAddress();
  if(apiAddress.isInvalid()) {
    LOG_WARNING("ConfigManagerDev: dashboard API address list empty, evaluating fallback options");
#ifdef PSVITA_PLATFORM
    if(!this->dashboardHost.empty()) {
      LOG_WARNING(
          "ConfigManagerDev: using dashboard host fallback %s because API address list is empty",
          this->dashboardHost.c_str());
      processResponse(dashboardapi::getConfig(this->dashboardHost), "hostname fallback");
      return;
    }
#endif
    this->nextGetConfigUpdate = now + getConfigRetryPeriod;
    return;
  }

  LOG_INFO(
      "ConfigManagerDev: evaluating dashboard API %s mappedV4=%d",
      apiAddress.toString().c_str(),
      apiAddress.isMappedV4());

#ifdef PSVITA_PLATFORM
  if(!apiAddress.isMappedV4()) {
    if(!this->dashboardHost.empty()) {
      LOG_WARNING(
          "ConfigManagerDev: dashboard API %s requires IPv6; attempting hostname fallback %s",
          apiAddress.toString().c_str(),
          this->dashboardHost.c_str());
      processResponse(dashboardapi::getConfig(this->dashboardHost), "hostname fallback");
      return;
    }

    static bool warnedAboutIpv6Dashboard = false;
    if(!warnedAboutIpv6Dashboard) {
      LOG_WARNING(
          "ConfigManagerDev: dashboard API address %s requires IPv6 which is unavailable on this platform; skipping fetch",
          apiAddress.toString().c_str());
      warnedAboutIpv6Dashboard = true;
    }
    this->nextGetConfigUpdate = now + getConfigRetryPeriod;
    return;
  }
#endif

  processResponse(dashboardapi::getConfig(apiAddress), "api address");
}

void ConfigManager::updateGetConfigData()
{
  std::lock_guard lgSlow(this->mutexSlow);
  std::lock_guard lgFast(this->mutexFast);
  LOG_INFO("ConfigManagerDev: updateGetConfigData started")
  const auto& latestConfig = this->cacheJson[CACHE_KEY_GETCONFIG];

  if(latestConfig.contains(GETCONFIG_KEY_PEERS) && latestConfig[GETCONFIG_KEY_PEERS].is_array()) {
    this->allowedPeers.clear();

    etl::string<EMAIL_MAX_LENGTH> previousOwner = this->claimedBy;
    // upack ClaimInfo
    this->claimed = latestConfig[GETCONFIG_KEY_IS_CLAIMED].get<bool>();
    if(this->claimed) {
      auto claimInfo = latestConfig[GETCONFIG_KEY_CLAIMINFO];
      auto ownerStr = claimInfo[GETCONFIG_KEY_CLAIMINFO_OWNER].get<std::string>();
      auto hostnameStr = claimInfo[GETCONFIG_KEY_CLAIMINFO_HOSTNAME].get<std::string>();
      this->claimedBy = etl::string<EMAIL_MAX_LENGTH>(ownerStr.c_str());
      this->hostname = etl::string<HOSTNAME_MAX_LENGTH>(hostnameStr.c_str());

      auto featureFlags = latestConfig[GETCONFIG_KEY_FEATUREFLAGS];
      // legacy sync hostname feature
      auto shouldSyncHostname = featureFlags[GETCONFIG_KEY_FEATUREFLAGS_SYNCHOSTNAME].get<bool>();
      if(shouldSyncHostname) {
        Port::setSelfHostname(hostnameStr);
      }
    } else {
      this->claimedBy = "";
    }

    if(previousOwner.empty() && !this->claimedBy.empty()) {
      this->hooksManager->scheduleHook(HookType::claimed);
    }
    // TODO: add unclaimed hook

    std::map<std::string, HusarnetAddress> hostsEntries;
    for(auto& peerInfo : latestConfig[GETCONFIG_KEY_PEERS]) {
      // unpack PeerInfo structure
      auto addrStr = peerInfo[GETCONFIG_KEY_PEERINFO_IP].get<std::string>();
      auto peerHostname = peerInfo[GETCONFIG_KEY_PEERINFO_HOSTNAME].get<std::string>();
      auto aliases = peerInfo[GETCONFIG_KEY_PEERINFO_ALIASES].get<std::vector<std::string>>();

      LOG_INFO("ConfigManagerDev: parse %s", addrStr.c_str())
      auto addr = HusarnetAddress::parse(addrStr);
      this->allowedPeers.insert(addr);  // TODO: handle set is full
      hostsEntries.insert({peerHostname, addr});
      for(auto& alias : aliases) {
        hostsEntries.insert({alias, addr});
      }
      // TODO it would be best if the lock was freed before updateHostsFile
      // but that's for later
    }
    // add also our own address as husarnet-local
    hostsEntries.insert({"husarnet-local", this->ourIp});
    Port::updateHostsFile(hostsEntries);
  }

  LOG_INFO("ConfigManagerDev: updateGetConfigData finished")
}

void ConfigManager::storeGetConfig(const json& jsonDoc)
{
  std::lock_guard lgSlow(this->mutexSlow);
  this->cacheJson[CACHE_KEY_GETCONFIG] = jsonDoc;
  this->cacheDirty.store(true, std::memory_order_relaxed);
}

bool ConfigManager::readUserConfig()
{
  auto contents = Port::readStorage(StorageKey::config);
  if(contents.empty()) {
    LOG_INFO("ConfigManagerDev: saved config.json is empty/nonexistent");
    return false;
  }
  auto parsedContents = json::parse(contents, nullptr, false);
  if(parsedContents.is_discarded()) {
    LOG_INFO("ConfigManagerDev: saved config.json is not a valid JSON, not reading");
    return false;
  }
  this->storeUserConfig(parsedContents);
  return true;
}

void ConfigManager::storeUserConfig(const json& jsonDoc)
{
  bool changed = false;
  {
    std::lock_guard lgSlow(this->mutexSlow);
    if(jsonDoc.contains(USERCONFIG_KEY_WHITELIST) && jsonDoc[USERCONFIG_KEY_WHITELIST].is_array()) {
      if(!this->userConfigJson.contains(USERCONFIG_KEY_WHITELIST) ||
         this->userConfigJson[USERCONFIG_KEY_WHITELIST] != jsonDoc[USERCONFIG_KEY_WHITELIST]) {
        this->userConfigJson[USERCONFIG_KEY_WHITELIST] = jsonDoc[USERCONFIG_KEY_WHITELIST];
        changed = true;
      }
    } else if(this->userConfigJson.contains(USERCONFIG_KEY_WHITELIST)) {
      this->userConfigJson.erase(USERCONFIG_KEY_WHITELIST);
      changed = true;
    }
    if(changed) {
      this->configDirty.store(true, std::memory_order_relaxed);
    }
  }
  if(changed) {
    this->updateUserConfigData();
  }
}

void ConfigManager::updateUserConfigData()
{
  std::lock_guard lgSlow(this->mutexSlow);
  std::lock_guard lgFast(this->mutexFast);

  this->userWhitelist.clear();

  if(!this->userConfigJson.contains(USERCONFIG_KEY_WHITELIST) ||
     !this->userConfigJson[USERCONFIG_KEY_WHITELIST].is_array()) {
    return;
  }

  auto entries = this->userConfigJson[USERCONFIG_KEY_WHITELIST].get<std::vector<std::string>>();
  for(auto& entry : entries) {
    auto addr = HusarnetAddress::parse(entry);
    if(addr.isFC94()) {
      this->userWhitelist.insert(addr);
    } else {
      LOG_WARNING("user whitelist contains invalid Husarnet address %s", entry.c_str());
    }
  }
}

bool ConfigManager::readCache()
{
  auto contents = Port::readStorage(StorageKey::cache);
  if(contents.empty()) {
    LOG_INFO("ConfigManagerDev: saved cache.json is empty/nonexistent");
    return false;
  }
  auto parsedContents = json::parse(contents, nullptr, false);
  if(parsedContents.is_discarded()) {
    LOG_INFO("ConfigManagerDev: saved cache.json is not a valid JSON, not reading");
    return false;
  }

  this->storeCache(parsedContents);
  return true;
}

void ConfigManager::storeCache(const nlohmann::json& jsonDoc)
{
  std::lock_guard lgSlow(this->mutexSlow);
  if(jsonDoc.contains(CACHE_KEY_GETCONFIG) && jsonDoc[CACHE_KEY_GETCONFIG].is_object()) {
    this->cacheJson[CACHE_KEY_GETCONFIG] = jsonDoc[CACHE_KEY_GETCONFIG];
  }
  if(jsonDoc.contains(CACHE_KEY_LICENSE) && jsonDoc[CACHE_KEY_LICENSE].is_object()) {
    this->cacheJson[CACHE_KEY_LICENSE] = jsonDoc[CACHE_KEY_LICENSE];
  }
}

bool ConfigManager::isPeerAllowed(const HusarnetAddress& address) const
{
  std::lock_guard lgFast(this->mutexFast);
  if(this->allowEveryone) {
    return true;
  }
  for(auto& apiAddr : this->apiAddresses) {
    if(apiAddr == address) {
      return true;
    }
  }
  for(auto& ebAddr : this->ebAddresses) {
    if(ebAddr == address) {
      return true;
    }
  }
  if(this->userWhitelist.contains(address)) {
    return true;
  }
  return this->allowedPeers.contains(address);
}

bool ConfigManager::isClaimed() const
{
  std::lock_guard lgFast(this->mutexFast);
  return this->claimed;
}

etl::vector<InternetAddress, BASE_ADDRESSES_LIMIT> ConfigManager::getBaseAddresses() const
{
  std::lock_guard lgFast(this->mutexFast);
  return this->baseAddresses;
}

etl::vector<HusarnetAddress, MULTICAST_DESTINATIONS_LIMIT> ConfigManager::getMulticastDestinations(HusarnetAddress id)
{
  std::lock_guard lgFast(this->mutexFast);
  // TODO: figure out if this check was even relevant
  //  if(!id == deviceIdFromIpAddress(multicastDestination)) {
  //    return {};
  //  }
  auto result = etl::vector<HusarnetAddress, MULTICAST_DESTINATIONS_LIMIT>();
  for(auto& peer : this->allowedPeers) {
    result.push_back(peer);
  }
  return result;
}

json ConfigManager::getDataForStatus() const
{
  std::lock_guard lgSlow(this->mutexSlow);
  json combined;
  combined[STATUS_KEY_USERCONFIG] = this->userConfigJson;

  if(this->cacheJson.contains(CACHE_KEY_GETCONFIG)) {
    combined[STATUS_KEY_APICONFIG] = this->cacheJson[CACHE_KEY_GETCONFIG];
  } else {
    combined[STATUS_KEY_APICONFIG] = json({});
  }

  if(this->cacheJson.contains(CACHE_KEY_LICENSE)) {
    combined[STATUS_KEY_LICENSE] = this->cacheJson[CACHE_KEY_LICENSE];
  } else {
    combined[STATUS_KEY_LICENSE] = json({});
  }

  combined[STATUS_KEY_ENVIRONMENT] = {
      {STATUS_KEY_ENVIRONMENT_INSTANCE_FQDN, this->configEnv->getTldFqdn()},
      {STATUS_KEY_ENVIRONMENT_LOG_VERBOSITY, this->configEnv->getLogVerbosity()}};

  return combined;
}

bool ConfigManager::writeConfig()
{
  std::lock_guard lgSlow(this->mutexSlow);
  return Port::writeStorage(StorageKey::config, this->userConfigJson.dump(JSON_INDENT_SPACES));
}

bool ConfigManager::writeCache()
{
  std::lock_guard lgSlow(this->mutexSlow);
  return Port::writeStorage(StorageKey::cache, this->cacheJson.dump(JSON_INDENT_SPACES));
}

void ConfigManager::periodicThread()
{
  // start with disk reads and slurp json docs into ram
  if(this->readCache()) {
    LOG_INFO("ConfigManagerDev: cache read from disk successful")
  }
  if(this->readUserConfig()) {
    LOG_INFO("ConfigManagerDev: user config read from disk successful")
    updateUserConfigData();
  }

  this->getLicense();

  while(true) {
    std::unique_lock lk(this->cvMutex);
    this->cv.wait_for(lk, std::chrono::milliseconds(periodicThreadIntervalMs));

    TimePoint now = std::chrono::steady_clock::now();

    if(now >= this->nextLicenseUpdate) {
      LOG_DEBUG("ConfigManagerDev: periodic thread: will redownload the license")
      this->getLicense();
    }

    if(this->configEnv->getEnableControlplane() && now >= this->nextGetConfigUpdate) {
      LOG_DEBUG("ConfigManagerDev: periodic thread: will request the config from the control plane");
      this->getGetConfig();
    }

    if(this->configDirty.exchange(false, std::memory_order_acq_rel)) {
      if(this->writeConfig()) {
        LOG_DEBUG("ConfigManagerDev: config write successful")
      } else {
        LOG_ERROR("ConfigManagerDev: config write failed")
        this->configDirty.store(true, std::memory_order_relaxed);
      }
    }

    if(this->cacheDirty.exchange(false, std::memory_order_acq_rel)) {
      if(this->writeCache()) {
        LOG_DEBUG("ConfigManagerDev: cache write successful")
      } else {
        LOG_ERROR("ConfigManagerDev: cache write failed")
        this->cacheDirty.store(true, std::memory_order_relaxed);
      }
    }
  }
}

void ConfigManager::waitInit() const
{
  LOG_INFO("ConfigManagerDev: wait init started")
  // we need to at least have license information, like for example base server to connect to.
  // wait until license is downloaded and base addresses are known
  while(this->baseAddresses.empty()) {
    Port::threadSleep(20);
  }
  LOG_INFO("ConfigManagerDev: wait init finished")
}

bool ConfigManager::userWhitelistAdd(const HusarnetAddress& address)
{
  std::lock_guard lock(this->mutexFast);
  if(this->userWhitelist.contains(address)) {
    LOG_ERROR("ConfigManagerDev: IP is already whitelisted")
    return false;
  }
  if(this->userWhitelist.full()) {
    LOG_ERROR("ConfigManagerDev: userWhitelist is full")
    return false;
  }

  this->userWhitelist.insert(address);
  return true;
}

bool ConfigManager::userWhitelistRm(const HusarnetAddress& address)
{
  std::lock_guard lock(this->mutexFast);
  if(this->userWhitelist.contains(address)) {
    this->userWhitelist.erase(address);
    return true;
  }
  LOG_ERROR("ConfigManagerDev: IP is not on the whitelist")
  return false;
}

void ConfigManager::triggerGetConfig()
{
  LOG_INFO("ConfigManagerDev: get_config ordered by control plane, resetting timer")
  this->nextGetConfigUpdate = std::chrono::steady_clock::now();
  this->cv.notify_one();
}
