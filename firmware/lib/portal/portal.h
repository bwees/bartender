#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace portal {

extern const char* const kRedirectLocation;
extern const char* const kWebRoot;
extern const char* const kAssetRoot;
extern const char* const kRecipesPath;
extern const char* const kConfigPath;

enum class Method { get, head, post, put, del, other };

struct Request {
  Method method = Method::get;
  std::string uri;
};

enum class RouteKind {
  captiveRedirect,
  staticFile,
  health,
  getRecipes,
  putRecipes,
  getConfig,
  putConfig,
  uploadAsset,
  deleteAsset,
  uploadWww,
  clearWww,
  unsafeWwwPath,
  badRequest,
  notFound,
  methodNotAllowed,
};

struct Route {
  RouteKind kind = RouteKind::captiveRedirect;
  std::string path;
};

struct Response {
  int status = 200;
  std::string contentType = "text/plain";
  std::string location;
  std::string body;
};

struct StaticFile {
  std::string path;
  std::string contentType;
  std::string cacheControl;
  bool gzip = false;
};

using PathExists = std::function<bool(const std::string&)>;

const std::vector<std::string>& probePaths();
bool isProbePath(const std::string& path);

std::string requestPath(const std::string& uri);
bool isSafePath(const std::string& path);

Route route(const Request& request);

Response captiveRedirect();
Response bootstrap();
Response health();
Response conflict();
Response json(int status, const std::string& body);

std::string contentTypeFor(const std::string& path);
std::string cacheControlFor(const std::string& path);
std::optional<StaticFile> resolveStatic(const std::string& uri, const PathExists& exists);
bool bundleInstalled(const PathExists& exists);

enum class StaticOutcome { file, bootstrap, redirect };

struct StaticDecision {
  StaticOutcome outcome = StaticOutcome::redirect;
  StaticFile file;
};

StaticDecision decideStatic(const std::string& uri, const PathExists& exists);

std::optional<std::string> assetPath(const std::string& uri);
std::optional<std::string> wwwUploadPath(const std::string& uri);
bool isJpegContentType(const std::string& header);

// LVGL 9.5 decodes drink art with tjpgd, which is baseline-only and runs at
// scale 0. A progressive or oversized JPEG renders as a blank tile with no error
// anywhere, so the upload handler rejects it instead of storing it.
enum class JpegKind { baseline, progressive, unsupported, notJpeg, undetermined };

struct JpegInfo {
  JpegKind kind = JpegKind::undetermined;
  int width = 0;
  int height = 0;
};

extern const int kMaxImageWidth;
extern const int kMaxImageHeight;

JpegInfo inspectJpeg(const std::string& prefix);
std::optional<std::string> jpegRejection(const JpegInfo& info);

class Lockout {
 public:
  using Predicate = std::function<bool()>;

  explicit Lockout(Predicate pouring);
  bool blocks(RouteKind kind) const;

 private:
  Predicate pouring_;
};

}
