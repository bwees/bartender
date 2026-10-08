#include "portal_server.h"

#include <DNSServer.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>

#include <algorithm>
#include <optional>
#include <string>

#include "portal.h"
#include "sd_fs.h"
#include "state_lock.h"

namespace {

constexpr size_t kChunkBytes = 4096;
constexpr size_t kMaxAssetBytes = 512 * 1024;
constexpr size_t kMaxBundleFileBytes = 1024 * 1024;
constexpr size_t kMaxJsonBytes = 64 * 1024;
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kDnsTtlSeconds = 30;
const IPAddress kApAddress(192, 168, 4, 1);

PortalState state;
DNSServer dnsServer;
httpd_handle_t server = nullptr;
uint8_t* chunkBuffer = nullptr;
portal::Lockout* lockout = nullptr;

portal::Method methodOf(int method) {
  switch (method) {
    case HTTP_GET: return portal::Method::get;
    case HTTP_HEAD: return portal::Method::head;
    case HTTP_POST: return portal::Method::post;
    case HTTP_PUT: return portal::Method::put;
    case HTTP_DELETE: return portal::Method::del;
    default: return portal::Method::other;
  }
}

const char* statusLine(int status) {
  switch (status) {
    case 201: return "201 Created";
    case 302: return "302 Found";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 415: return "415 Unsupported Media Type";
    case 500: return "500 Internal Server Error";
    default: return "200 OK";
  }
}

esp_err_t send(httpd_req_t* req, const portal::Response& response) {
  httpd_resp_set_status(req, statusLine(response.status));
  httpd_resp_set_type(req, response.contentType.c_str());
  if (!response.location.empty())
    httpd_resp_set_hdr(req, "Location", response.location.c_str());
  return httpd_resp_send(req, response.body.data(), response.body.size());
}

esp_err_t sendStatic(httpd_req_t* req, const portal::StaticFile& file) {
  File handle;
  {
    SdGuard guard;
    handle = SD.open(file.path.c_str(), FILE_READ);
  }
  if (!handle) return send(req, portal::captiveRedirect());

  httpd_resp_set_type(req, file.contentType.c_str());
  httpd_resp_set_hdr(req, "Cache-Control", file.cacheControl.c_str());
  if (file.gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

  bool ok = true;
  while (ok) {
    int read;
    {
      SdGuard guard;
      read = handle.read(chunkBuffer, kChunkBytes);
    }
    if (read <= 0) break;
    ok = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(chunkBuffer), read) == ESP_OK;
  }
  {
    SdGuard guard;
    handle.close();
  }
  if (!ok) return ESP_FAIL;
  return httpd_resp_send_chunk(req, nullptr, 0);
}

std::optional<std::string> receiveJson(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > kMaxJsonBytes) return std::nullopt;
  std::string body(req->content_len, '\0');
  size_t offset = 0;
  while (offset < body.size()) {
    const int received = httpd_req_recv(req, body.data() + offset, body.size() - offset);
    if (received <= 0) return std::nullopt;
    offset += received;
  }
  return body;
}

bool ensureParentDirectories(const std::string& path) {
  size_t slash = path.find('/', 1);
  while (slash != std::string::npos) {
    const std::string directory = path.substr(0, slash);
    {
      SdGuard guard;
      if (!SD.exists(directory.c_str()) && !SD.mkdir(directory.c_str())) return false;
    }
    slash = path.find('/', slash + 1);
  }
  return true;
}

esp_err_t receiveAsset(httpd_req_t* req, const std::string& path) {
  char contentType[64] = {};
  httpd_req_get_hdr_value_str(req, "Content-Type", contentType, sizeof(contentType));
  if (!portal::isJpegContentType(contentType))
    return send(req, portal::json(415, "{\"error\":\"expected image/jpeg\"}"));
  if (req->content_len == 0 || req->content_len > kMaxAssetBytes)
    return send(req, portal::json(400, "{\"error\":\"body size out of range\"}"));

  size_t remaining = req->content_len;
  const int head = httpd_req_recv(req, reinterpret_cast<char*>(chunkBuffer),
                                  std::min(remaining, kChunkBytes));
  if (head <= 0) return send(req, portal::json(400, "{\"error\":\"body truncated\"}"));
  const auto rejection = portal::jpegRejection(
      portal::inspectJpeg(std::string(reinterpret_cast<char*>(chunkBuffer), head)));
  if (rejection) return send(req, portal::json(415, "{\"error\":\"" + *rejection + "\"}"));

  if (!ensureParentDirectories(path))
    return send(req, portal::json(500, "{\"error\":\"cannot create directory\"}"));

  const std::string temporary = path + ".tmp";
  File handle;
  {
    SdGuard guard;
    handle = SD.open(temporary.c_str(), FILE_WRITE);
  }
  if (!handle) return send(req, portal::json(500, "{\"error\":\"cannot open asset\"}"));

  int received = head;
  bool ok = true;
  while (ok && received > 0) {
    {
      SdGuard guard;
      ok = handle.write(chunkBuffer, received) == static_cast<size_t>(received);
    }
    remaining -= received;
    if (!ok || remaining == 0) break;
    received = httpd_req_recv(req, reinterpret_cast<char*>(chunkBuffer),
                              std::min(remaining, kChunkBytes));
    if (received <= 0) ok = false;
  }

  bool committed = false;
  {
    SdGuard guard;
    handle.close();
    if (ok) {
      SD.remove(path.c_str());
      committed = SD.rename(temporary.c_str(), path.c_str());
    }
    if (!committed) SD.remove(temporary.c_str());
  }
  if (!committed) return send(req, portal::json(500, "{\"error\":\"asset write failed\"}"));
  // Replacing art leaves the library untouched, so the revision is the only word
  // the HMI gets that the bytes behind a path it is already showing have changed.
  bumpStateRevision();
  return send(req, portal::json(201, "{\"ok\":true}"));
}

esp_err_t receiveBundleFile(httpd_req_t* req, const std::string& path) {
  if (req->content_len > kMaxBundleFileBytes)
    return send(req, portal::json(400, "{\"error\":\"body size out of range\"}"));
  if (!ensureParentDirectories(path))
    return send(req, portal::json(500, "{\"error\":\"cannot create directory\"}"));

  const std::string temporary = path + ".tmp";
  File handle;
  {
    SdGuard guard;
    handle = SD.open(temporary.c_str(), FILE_WRITE);
  }
  if (!handle) return send(req, portal::json(500, "{\"error\":\"cannot open file\"}"));

  size_t remaining = req->content_len;
  bool ok = true;
  while (ok && remaining > 0) {
    const int received = httpd_req_recv(req, reinterpret_cast<char*>(chunkBuffer),
                                        std::min(remaining, kChunkBytes));
    if (received <= 0) {
      ok = false;
      break;
    }
    {
      SdGuard guard;
      ok = handle.write(chunkBuffer, received) == static_cast<size_t>(received);
    }
    remaining -= received;
  }

  bool committed = false;
  {
    SdGuard guard;
    handle.close();
    if (ok) {
      // FAT rename refuses an existing destination, so the target goes first.
      SD.remove(path.c_str());
      committed = SD.rename(temporary.c_str(), path.c_str());
    }
    if (!committed) SD.remove(temporary.c_str());
  }
  if (!committed) return send(req, portal::json(500, "{\"error\":\"bundle write failed\"}"));
  return send(req, portal::json(201, "{\"ok\":true}"));
}

// SvelteKit names every chunk by content hash, so an in-place redeploy would leave
// the previous build's files on the card forever. The whole tree goes first.
bool removeTree(const std::string& path) {
  File dir;
  {
    SdGuard guard;
    dir = SD.open(path.c_str());
  }
  if (!dir) return true;
  const bool isDirectory = dir.isDirectory();
  if (!isDirectory) {
    {
      SdGuard guard;
      dir.close();
    }
    SdGuard guard;
    return SD.remove(path.c_str());
  }

  while (true) {
    File child;
    {
      SdGuard guard;
      child = dir.openNextFile();
    }
    if (!child) break;
    std::string childPath;
    {
      SdGuard guard;
      childPath = child.path();
      child.close();
    }
    if (!removeTree(childPath)) {
      SdGuard guard;
      dir.close();
      return false;
    }
  }

  SdGuard guard;
  dir.close();
  return SD.rmdir(path.c_str());
}

esp_err_t clearBundle(httpd_req_t* req, const std::string& path) {
  if (!removeTree(path))
    return send(req, portal::json(500, "{\"error\":\"could not clear bundle\"}"));
  return send(req, portal::json(200, "{\"ok\":true}"));
}

esp_err_t removeAsset(httpd_req_t* req, const std::string& path) {
  bool present;
  bool removed;
  {
    SdGuard guard;
    present = SD.exists(path.c_str());
    removed = present ? SD.remove(path.c_str()) : true;
  }
  if (!removed) return send(req, portal::json(500, "{\"error\":\"could not remove asset\"}"));
  if (present) bumpStateRevision();
  return send(req, portal::json(200, "{\"ok\":true}"));
}

esp_err_t putRecipes(httpd_req_t* req) {
  const auto body = receiveJson(req);
  if (!body) return send(req, portal::json(400, "{\"error\":\"missing body\"}"));
  int version = 0;
  auto parsed = bartender::parseLibrary(*body, version);
  if (!parsed) return send(req, portal::json(400, "{\"error\":\"invalid recipes\"}"));
  if (!bartender::saveLibrary(*state.fs, portal::kRecipesPath, *parsed))
    return send(req, portal::json(500, "{\"error\":\"write failed\"}"));
  {
    StateGuard guard;
    *state.library = *parsed;
  }
  bumpStateRevision();
  StateGuard guard;
  return send(req, portal::json(200, bartender::serializeLibrary(*state.library)));
}

esp_err_t putConfig(httpd_req_t* req) {
  const auto body = receiveJson(req);
  if (!body) return send(req, portal::json(400, "{\"error\":\"missing body\"}"));
  int version = 0;
  auto parsed = bartender::parseConfig(*body, version);
  if (!parsed) return send(req, portal::json(400, "{\"error\":\"invalid config\"}"));
  if (!bartender::saveConfig(*state.fs, portal::kConfigPath, *parsed))
    return send(req, portal::json(500, "{\"error\":\"write failed\"}"));
  {
    StateGuard guard;
    *state.config = *parsed;
  }
  bumpStateRevision();
  StateGuard guard;
  return send(req, portal::json(200, bartender::serializeConfig(*state.config)));
}

esp_err_t serveStatic(httpd_req_t* req) {
  const auto decision = portal::decideStatic(req->uri, [](const std::string& path) {
    SdGuard guard;
    return SD.exists(path.c_str());
  });
  switch (decision.outcome) {
    case portal::StaticOutcome::file: return sendStatic(req, decision.file);
    case portal::StaticOutcome::bootstrap: return send(req, portal::bootstrap());
    case portal::StaticOutcome::redirect: break;
  }
  return send(req, portal::captiveRedirect());
}

esp_err_t dispatch(httpd_req_t* req) {
  const portal::Route resolved = portal::route({methodOf(req->method), req->uri});
  if (lockout->blocks(resolved.kind)) return send(req, portal::conflict());

  switch (resolved.kind) {
    case portal::RouteKind::captiveRedirect: return send(req, portal::captiveRedirect());
    case portal::RouteKind::staticFile: return serveStatic(req);
    case portal::RouteKind::health: return send(req, portal::health());
    case portal::RouteKind::getRecipes:
      { StateGuard guard; return send(req, portal::json(200, bartender::serializeLibrary(*state.library))); }
    case portal::RouteKind::putRecipes: return putRecipes(req);
    case portal::RouteKind::getConfig:
      { StateGuard guard; return send(req, portal::json(200, bartender::serializeConfig(*state.config))); }
    case portal::RouteKind::putConfig: return putConfig(req);
    case portal::RouteKind::uploadAsset: return receiveAsset(req, resolved.path);
    case portal::RouteKind::deleteAsset: return removeAsset(req, resolved.path);
    case portal::RouteKind::uploadWww: return receiveBundleFile(req, resolved.path);
    case portal::RouteKind::clearWww: return clearBundle(req, resolved.path);
    case portal::RouteKind::unsafeWwwPath:
      return send(req, portal::json(400, "{\"error\":\"unsafe bundle path\"}"));
    case portal::RouteKind::badRequest:
      return send(req, portal::json(400, "{\"error\":\"bad request\"}"));
    case portal::RouteKind::notFound:
      return send(req, portal::json(404, "{\"error\":\"not found\"}"));
    case portal::RouteKind::methodNotAllowed:
      return send(req, portal::json(405, "{\"error\":\"method not allowed\"}"));
  }
  return send(req, portal::captiveRedirect());
}

esp_err_t onServerError(httpd_req_t* req, httpd_err_code_t) {
  return send(req, portal::captiveRedirect());
}

void dnsTask(void*) {
  for (;;) {
    dnsServer.processNextRequest();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool startHttp() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.task_priority = 3;
  cfg.core_id = 0;
  cfg.stack_size = 8192;
  cfg.max_open_sockets = 10;
  cfg.max_uri_handlers = 8;
  cfg.lru_purge_enable = true;
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  cfg.keep_alive_enable = true;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&server, &cfg) != ESP_OK) return false;

  // IDF 5.1's esp_http_server has no HTTP_ANY wildcard, so the same catch-all is
  // registered once per method we answer. portal::route() still makes every routing
  // decision; this only widens the door.
  for (httpd_method_t method : {HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_DELETE}) {
    httpd_uri_t catchAll = {};
    catchAll.uri = "/*";
    catchAll.method = method;
    catchAll.handler = dispatch;
    if (httpd_register_uri_handler(server, &catchAll) != ESP_OK) return false;
  }

  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, onServerError);
  httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, onServerError);
  return true;
}

}  // namespace

bool portalStart(const PortalState& newState) {
  state = newState;
  static portal::Lockout policy(state.pouring);
  lockout = &policy;

  chunkBuffer = static_cast<uint8_t*>(
      heap_caps_malloc(kChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (chunkBuffer == nullptr) return false;

  // Both servers bind before the AP broadcasts: a probe fires within milliseconds
  // of the DHCP ACK, and a refused connection reads as "no internet" rather than
  // "sign in" on every OS. But binding a socket needs lwIP, which does not exist
  // until esp_netif is up — so the mode change has to lead, and only softAP(),
  // which actually starts advertising, is held back.
  // WiFi persists its config and RF calibration to NVS by default, and every such
  // write disables the flash cache. The RGB panel's bounce-buffer refill ISR cannot
  // run with the cache down, which desyncs the DMA and corrupts the display until a
  // restart. Nothing here needs to survive a reboot — all config lives on the card.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);

  if (!startHttp()) return false;
  dnsServer.setTTL(kDnsTtlSeconds);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  if (!dnsServer.start(kDnsPort, "*", kApAddress)) return false;
  xTaskCreatePinnedToCore(dnsTask, "portal-dns", 4096, nullptr, 3, nullptr, 0);

  if (!WiFi.softAP(state.config->wifi.ssid.c_str(), state.config->wifi.passphrase.c_str()))
    return false;
  // enableDhcpCaptivePortal() arrived after arduino-esp32 3.0.x. DHCP option 114 is
  // a nicety; the wildcard DNS above is what actually triggers the portal.
  return true;
}
