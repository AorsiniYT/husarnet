// Copyright (c) 2025 Husarnet sp. z o.o.
// Authors: listed in project_root/README.md
// License: specified in project_root/LICENSE.txt

#include "response.h"

#include <vector>

#include "husarnet/husarnet_config.h"
#include "husarnet/logging.h"

#include "etl/string.h"
#include "etl/string_view.h"
#include "port_interface.h"
#include "proxy.h"

namespace dashboardapi {
  Response::Response(int code, const std::string& bytes) : statusCode(code)
  {
    if(code == 200) {
      try {
        jsonDoc = nlohmann::json::parse(bytes);
        return;
      } catch(const nlohmann::json::parse_error& ex) {
        LOG_ERROR("dashboardapi::Response: failed to parse payload as JSON (code 200): %s", ex.what());
        jsonDoc = nlohmann::json::object({
            {"type", "parse_error"},
            {"errors", std::vector<std::string>{"invalid JSON payload"}},
        });
        jsonDoc["raw_body"] = bytes;
        return;
      }
    }

    jsonDoc = nlohmann::json::object({
        {"type", "http_error"},
        {"status_code", code},
        {"errors", std::vector<std::string>{"unexpected HTTP status"}},
    });
    if(!bytes.empty()) {
      jsonDoc["raw_body"] = bytes;
    }
  }
  bool Response::isSuccessful() const
  {
    if(this->statusCode != 200) {
      return false;
    }
    auto typeIt = jsonDoc.find("type");
    if(typeIt == jsonDoc.end() || !typeIt->is_string()) {
      return false;
    }
    return *typeIt == "success";
  }

  nlohmann::json& Response::getPayloadJson()
  {
    if(!this->jsonDoc.contains("payload")) {
      this->jsonDoc["payload"] = nlohmann::json::object();
    }
    return this->jsonDoc["payload"];
  }

  std::string Response::toString()
  {
    std::string result{};
    const auto typeStr = jsonDoc.value("type", std::string("unknown"));
    if(typeStr == "user_error") {
      result += "invalid request: ";
    } else if(typeStr == "server_error") {
      result += "server error: ";
    } else if(typeStr == "parse_error") {
      result += "parse error: ";
    } else if(typeStr == "http_error") {
      result += "http error: ";
    } else if(jsonDoc.contains("type") && jsonDoc["type"].is_null()) {
      result += "unknown error type null";
      return result;
    } else {
      result += "unknown api error: ";
    }

    if(jsonDoc.contains("errors") && jsonDoc["errors"].is_array()) {
      auto errors = jsonDoc["errors"].get<std::vector<std::string>>();
      for(auto& err : errors) {
        result += err;
        result.push_back(' ');
      }
    }

    if(auto rawIt = jsonDoc.find("raw_body"); rawIt != jsonDoc.end() && rawIt->is_string()) {
      result += "raw=";
      result += rawIt->get<std::string>();
    }

    return result;
  }

  Response getConfig(HusarnetAddress apiAddress)
  {
    auto [statusCode, bytes] = Port::httpGet(apiAddress, "/device/get_config");
    return {statusCode, bytes};
  }

  Response getConfig(const std::string& host)
  {
    auto [statusCode, bytes] = Port::httpGet(host, "/device/get_config");
    return {statusCode, bytes};
  }

  // FIXME:
  //  there _should_ be a compile-time function to calculate those in etl::base64
  //  documentation even mentions such construct, unfortunately I couldn't find it in the lib sources

  // TODO: generalize it
  Response postHeartbeat(HusarnetAddress apiAddress, Identity* identity)
  {
    std::string path("/device/manage/heartbeat");

    std::string body(R"({"user_agent":")");
    body.append(HUSARNET_USER_AGENT);
    body.append("\"}");

    auto encodedPK = Proxy::encodePublicKey(identity);
    auto encodedSig = Proxy::encodeSignature(identity, body);

    // build query string
    path.append("?pk=");
    path.append(encodedPK.begin(), encodedPK.size());
    path.append("&sig=");
    path.append(encodedSig.begin(), encodedSig.size());

    auto [statusCode, bytes] = Port::httpPost(apiAddress, path, body);
    return {statusCode, bytes};
  }

  // Used only for platforms without CLI support (ESP-32).
  // On fat platforms claims are done via the CLI.
  Response postClaim(
      HusarnetAddress apiAddress,
      Identity* identity,
      const etl::string_view& code,
      const etl::string_view& hostname)
  {
    std::string path("/device/manage/claim");

    std::string body(R"({"code":")");
    body.append(code.data(), code.size());
    body.append(R"(","hostname":")");
    body.append(hostname.data(), hostname.size());
    body.append(R"("})");

    auto encodedPK = Proxy::encodePublicKey(identity);
    auto encodedSig = Proxy::encodeSignature(identity, body);

    // build query string
    path.append("?pk=");
    path.append(encodedPK.begin(), encodedPK.size());
    path.append("&sig=");
    path.append(encodedSig.begin(), encodedSig.size());

    auto [statusCode, bytes] = Port::httpPost(apiAddress, path, body);
    return {statusCode, bytes};
  }
}  // namespace dashboardapi