#include "portal.h"

#include <algorithm>
#include <cctype>

namespace portal {
namespace {

const char* const kSpaShell = "/www/200.html";
const char* const kIndex = "/www/index.html";
const char* const kImmutablePrefix = "/www/_app/immutable/";
const char* const kApiPrefix = "/api/";
const char* const kAssetPrefix = "/api/assets/";
const char* const kWwwPrefix = "/api/www/";
const char* const kAssetRootPrefix = "/assets/";
const size_t kMaxBundlePathLength = 128;

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string lowered(const std::string& value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string extensionOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  return lowered(path.substr(dot));
}

bool isSafeIdentifier(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (char c : id) {
    const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    if (!allowed) return false;
  }
  return true;
}

bool isSafeBundleName(const std::string& relative) {
  if (relative.empty() || relative.size() > kMaxBundlePathLength) return false;
  if (relative.front() == '/' || relative.back() == '/') return false;
  if (relative.find("//") != std::string::npos) return false;
  for (char c : relative) {
    const bool allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                         c == '-' || c == '_' || c == '+' || c == '/';
    if (!allowed) return false;
  }
  return true;
}

StaticFile describe(const std::string& candidate, bool gzip) {
  StaticFile file;
  file.path = gzip ? candidate + ".gz" : candidate;
  file.contentType = contentTypeFor(candidate);
  file.cacheControl = cacheControlFor(candidate);
  file.gzip = gzip;
  return file;
}

}  // namespace

const char* const kRedirectLocation = "http://192.168.4.1/";
const char* const kWebRoot = "/www";
const char* const kAssetRoot = "/assets";
const char* const kRecipesPath = "/recipes.json";
const char* const kConfigPath = "/config.json";

const std::vector<std::string>& probePaths() {
  static const std::vector<std::string> paths = {
      "/generate_204",   "/gen_204",     "/hotspot-detect.html",
      "/library/test/success.html", "/canonical.html", "/success.txt",
      "/connecttest.txt", "/ncsi.txt",   "/redirect",
      "/fwlink"};
  return paths;
}

bool isProbePath(const std::string& path) {
  const std::string needle = lowered(path);
  for (const auto& probe : probePaths()) {
    if (needle == probe) return true;
  }
  return false;
}

std::string requestPath(const std::string& uri) {
  const size_t cut = uri.find_first_of("?#");
  return cut == std::string::npos ? uri : uri.substr(0, cut);
}

bool isSafePath(const std::string& path) {
  if (path.empty() || path[0] != '/') return false;
  if (path.find("..") != std::string::npos) return false;
  if (path.find('\\') != std::string::npos) return false;
  return path.find('\0') == std::string::npos;
}

Route route(const Request& request) {
  const std::string path = requestPath(request.uri);

  if (startsWith(path, kApiPrefix)) {
    if (path == "/api/health")
      return request.method == Method::get ? Route{RouteKind::health, ""}
                                           : Route{RouteKind::methodNotAllowed, ""};
    if (path == "/api/recipes") {
      if (request.method == Method::get) return {RouteKind::getRecipes, ""};
      if (request.method == Method::put) return {RouteKind::putRecipes, ""};
      return {RouteKind::methodNotAllowed, ""};
    }
    if (path == "/api/config") {
      if (request.method == Method::get) return {RouteKind::getConfig, ""};
      if (request.method == Method::put) return {RouteKind::putConfig, ""};
      return {RouteKind::methodNotAllowed, ""};
    }
    if (path == "/api/www") {
      if (request.method != Method::del) return {RouteKind::methodNotAllowed, ""};
      return {RouteKind::clearWww, kWebRoot};
    }
    if (startsWith(path, kWwwPrefix)) {
      if (request.method != Method::put) return {RouteKind::methodNotAllowed, ""};
      auto target = wwwUploadPath(path);
      if (!target) return {RouteKind::unsafeWwwPath, ""};
      return {RouteKind::uploadWww, *target};
    }
    if (startsWith(path, kAssetPrefix)) {
      if (request.method != Method::post && request.method != Method::del)
        return {RouteKind::methodNotAllowed, ""};
      auto target = assetPath(path);
      if (!target) return {RouteKind::badRequest, ""};
      const auto kind =
          request.method == Method::del ? RouteKind::deleteAsset : RouteKind::uploadAsset;
      return {kind, *target};
    }
    return {RouteKind::notFound, ""};
  }

  if (isProbePath(path)) return {RouteKind::captiveRedirect, ""};
  if (request.method == Method::get || request.method == Method::head)
    return {RouteKind::staticFile, ""};
  return {RouteKind::methodNotAllowed, ""};
}

Response captiveRedirect() {
  Response response;
  response.status = 302;
  response.contentType = "text/html";
  response.location = kRedirectLocation;
  response.body =
      "<html><body><a href=\"http://192.168.4.1/\">Bartender setup</a></body></html>";
  return response;
}

Response json(int status, const std::string& body) {
  Response response;
  response.status = status;
  response.contentType = "application/json";
  response.body = body;
  return response;
}

Response health() { return json(200, "{\"ok\":true}"); }

Response conflict() { return json(409, "{\"error\":\"pouring\"}"); }

std::string contentTypeFor(const std::string& path) {
  const std::string extension = extensionOf(path);
  if (extension == ".html" || extension == ".htm") return "text/html";
  if (extension == ".js" || extension == ".mjs") return "text/javascript";
  if (extension == ".css") return "text/css";
  if (extension == ".json" || extension == ".map") return "application/json";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".jpeg" || extension == ".jpg") return "image/jpeg";
  if (extension == ".webp") return "image/webp";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".txt") return "text/plain";
  if (extension == ".wasm") return "application/wasm";
  if (extension == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

std::string cacheControlFor(const std::string& path) {
  if (startsWith(path, kImmutablePrefix)) return "max-age=31536000, immutable";
  return "no-cache";
}

std::optional<StaticFile> resolveStatic(const std::string& uri, const PathExists& exists) {
  const std::string path = requestPath(uri);
  if (!isSafePath(path)) return std::nullopt;

  // Drink art lives at the card root, not under /www — the portal uploads it to
  // /assets/<uuid>.jpeg and the HMI reads the same files straight off SD. Without
  // this it resolves to /www/assets/... , which never exists, and the artwork the
  // portal just uploaded is write-only. No .html or index variants, and no SPA
  // fallback: an <img> that receives a page of HTML is worse than a failed load.
  if (startsWith(path, kAssetRootPrefix)) {
    if (exists(path)) return describe(path, false);
    return std::nullopt;
  }

  std::vector<std::string> candidates;
  if (path != "/") {
    const std::string base = kWebRoot + path;
    candidates.push_back(base);
    candidates.push_back(base + ".html");
    candidates.push_back(base + "/index.html");
  }
  candidates.push_back(kSpaShell);
  candidates.push_back(kIndex);

  for (const auto& candidate : candidates) {
    if (exists(candidate + ".gz")) return describe(candidate, true);
    if (exists(candidate)) return describe(candidate, false);
  }
  return std::nullopt;
}

bool bundleInstalled(const PathExists& exists) {
  return exists(kIndex) || exists(std::string(kIndex) + ".gz");
}

StaticDecision decideStatic(const std::string& uri, const PathExists& exists) {
  if (isProbePath(requestPath(uri))) return {StaticOutcome::redirect, {}};
  if (auto file = resolveStatic(uri, exists)) return {StaticOutcome::file, *file};
  if (!bundleInstalled(exists)) return {StaticOutcome::bootstrap, {}};
  return {StaticOutcome::redirect, {}};
}

std::optional<std::string> wwwUploadPath(const std::string& uri) {
  const std::string path = requestPath(uri);
  if (!startsWith(path, kWwwPrefix)) return std::nullopt;
  const std::string relative = path.substr(std::string(kWwwPrefix).size());
  if (!isSafeBundleName(relative)) return std::nullopt;
  const std::string target = std::string(kWebRoot) + "/" + relative;
  if (!isSafePath(target)) return std::nullopt;
  return target;
}

std::optional<std::string> assetPath(const std::string& uri) {
  const std::string path = requestPath(uri);
  if (!startsWith(path, kAssetPrefix)) return std::nullopt;
  const std::string id = path.substr(std::string(kAssetPrefix).size());
  if (!isSafeIdentifier(id)) return std::nullopt;
  return std::string(kAssetRoot) + "/" + id + ".jpeg";
}

bool isJpegContentType(const std::string& header) {
  std::string value = lowered(header.substr(0, header.find(';')));
  const size_t first = value.find_first_not_of(" \t");
  if (first == std::string::npos) return false;
  const size_t last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1) == "image/jpeg";
}

const int kMaxImageWidth = 800;
const int kMaxImageHeight = 480;

JpegInfo inspectJpeg(const std::string& prefix) {
  const auto byteAt = [&prefix](size_t index) {
    return static_cast<unsigned char>(prefix[index]);
  };
  if (prefix.size() < 2 || byteAt(0) != 0xFF || byteAt(1) != 0xD8) return {JpegKind::notJpeg, 0, 0};

  size_t at = 2;
  while (at + 3 < prefix.size()) {
    if (byteAt(at) != 0xFF) return {JpegKind::notJpeg, 0, 0};
    const unsigned char marker = byteAt(at + 1);
    if (marker == 0xFF) {
      at += 1;
      continue;
    }
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      at += 2;
      continue;
    }
    const size_t length = (static_cast<size_t>(byteAt(at + 2)) << 8) | byteAt(at + 3);
    if (length < 2) return {JpegKind::notJpeg, 0, 0};

    const bool isStartOfFrame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                                marker != 0xC8 && marker != 0xCC;
    if (isStartOfFrame) {
      if (at + 9 >= prefix.size()) return {JpegKind::undetermined, 0, 0};
      JpegInfo info;
      info.height = (byteAt(at + 5) << 8) | byteAt(at + 6);
      info.width = (byteAt(at + 7) << 8) | byteAt(at + 8);
      if (marker == 0xC0 || marker == 0xC1) info.kind = JpegKind::baseline;
      else if (marker == 0xC2) info.kind = JpegKind::progressive;
      else info.kind = JpegKind::unsupported;
      return info;
    }
    if (marker == 0xDA) return {JpegKind::notJpeg, 0, 0};
    at += 2 + length;
  }
  return {JpegKind::undetermined, 0, 0};
}

std::optional<std::string> jpegRejection(const JpegInfo& info) {
  switch (info.kind) {
    case JpegKind::notJpeg:
      return std::string("body is not a JPEG");
    case JpegKind::progressive:
      return std::string("progressive JPEG; the on-device decoder is baseline only");
    case JpegKind::unsupported:
      return std::string("unsupported JPEG encoding; use baseline");
    default:
      break;
  }
  if (info.width > kMaxImageWidth || info.height > kMaxImageHeight)
    return std::string("image exceeds 800x480; the on-device decoder cannot scale");
  return std::nullopt;
}

namespace {

const char kBootstrapHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bartender setup</title>
<style>
:root{color-scheme:dark}
body{margin:0;padding:2rem 1.25rem;background:#14161a;color:#e8eaed;
font:16px/1.5 system-ui,-apple-system,sans-serif}
main{max-width:34rem;margin:0 auto}
h1{font-size:1.4rem;margin:0 0 .5rem}
p{color:#a8b0bb}
code{background:#242833;border-radius:.25rem;padding:0 .25rem}
label{display:block;margin:1.5rem 0 .5rem;font-weight:600}
button{margin-top:1rem;padding:.6rem 1.2rem;border:0;border-radius:.4rem;
background:#4c8dff;color:#fff;font:inherit;cursor:pointer}
button[disabled]{opacity:.5;cursor:default}
#log{margin-top:1.25rem;font:13px/1.5 ui-monospace,monospace;word-break:break-all}
.bad{color:#ff8a80}
</style>
</head>
<body>
<main>
<h1>No web bundle installed</h1>
<p>This bartender has no web app on its SD card, so it is serving the setup page
built into its firmware. Pick the folder your web build produced
(<code>web/build</code>) to install it over WiFi.</p>
<label for="pick">Bundle folder</label>
<input id="pick" type="file" webkitdirectory directory multiple>
<button id="go">Upload</button>
<div id="log"></div>
</main>
<script>
var pick=document.getElementById('pick'),go=document.getElementById('go'),
log=document.getElementById('log');
function say(text,bad){var d=document.createElement('div');
if(bad)d.className='bad';d.textContent=text;log.appendChild(d);return d;}
go.onclick=async function(){
var files=Array.prototype.slice.call(pick.files);
if(!files.length){log.textContent='';say('Pick a folder first.',1);return;}
go.disabled=pick.disabled=true;log.textContent='';
var status=say(''),failed=0,i=0;
for(var f of files){
var parts=(f.webkitRelativePath||f.name).split('/');
var rel=parts.length>1?parts.slice(1).join('/'):parts[0];
status.textContent='Uploading '+(++i)+' of '+files.length+': '+rel;
try{
var r=await fetch('/api/www/'+encodeURI(rel),{method:'PUT',body:f});
if(!r.ok)throw new Error('HTTP '+r.status);
}catch(e){failed++;say(rel+' failed: '+e.message,1);}
}
status.textContent=(files.length-failed)+' of '+files.length+
' uploaded, '+failed+' failed.';
if(!failed)say('Done. Reload this page to open the app.');
go.disabled=pick.disabled=false;
};
</script>
</body>
</html>
)HTML";

}  // namespace

Response bootstrap() {
  Response response;
  response.status = 200;
  response.contentType = "text/html";
  response.body = kBootstrapHtml;
  return response;
}


Lockout::Lockout(Predicate pouring) : pouring_(std::move(pouring)) {}

bool Lockout::blocks(RouteKind kind) const {
  const bool mutating = kind == RouteKind::putRecipes || kind == RouteKind::putConfig ||
                        kind == RouteKind::uploadAsset || kind == RouteKind::deleteAsset ||
                        kind == RouteKind::uploadWww ||
                        kind == RouteKind::clearWww;
  return mutating && pouring_ && pouring_();
}

}
