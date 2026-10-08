#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "portal.h"

std::set<std::string> present;

bool onCard(const std::string& path) { return present.count(path) > 0; }

portal::Request get(const std::string& uri) { return {portal::Method::get, uri}; }

void setUp() { present.clear(); }
void tearDown() {}

void test_probe_paths_are_detected() {
  TEST_ASSERT_TRUE(portal::isProbePath("/generate_204"));
  TEST_ASSERT_TRUE(portal::isProbePath("/gen_204"));
  TEST_ASSERT_TRUE(portal::isProbePath("/hotspot-detect.html"));
  TEST_ASSERT_TRUE(portal::isProbePath("/library/test/success.html"));
  TEST_ASSERT_TRUE(portal::isProbePath("/canonical.html"));
  TEST_ASSERT_TRUE(portal::isProbePath("/success.txt"));
  TEST_ASSERT_TRUE(portal::isProbePath("/connecttest.txt"));
  TEST_ASSERT_TRUE(portal::isProbePath("/ncsi.txt"));
  TEST_ASSERT_TRUE(portal::isProbePath("/redirect"));
  TEST_ASSERT_TRUE(portal::isProbePath("/fwlink"));
  TEST_ASSERT_FALSE(portal::isProbePath("/"));
  TEST_ASSERT_FALSE(portal::isProbePath("/index.html"));
}

void test_probe_route_ignores_query_and_case() {
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::captiveRedirect,
                        portal::route(get("/generate_204?t=1")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::captiveRedirect,
                        portal::route(get("/Hotspot-Detect.html")).kind);
}

void test_probe_route_answers_any_method() {
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::captiveRedirect,
                        portal::route({portal::Method::post, "/generate_204"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::captiveRedirect,
                        portal::route({portal::Method::other, "/ncsi.txt"}).kind);
}

void test_redirect_shape_wins_every_os() {
  const portal::Response response = portal::captiveRedirect();
  TEST_ASSERT_EQUAL_INT(302, response.status);
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", response.location.c_str());
  TEST_ASSERT_GREATER_OR_EQUAL_size_t(5, response.body.size());
  TEST_ASSERT_EQUAL_STRING("text/html", response.contentType.c_str());
}

void test_static_prefers_precompressed() {
  present = {"/www/index.html", "/www/index.html.gz"};
  auto file = portal::resolveStatic("/", onCard);
  TEST_ASSERT_TRUE(file.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/index.html.gz", file->path.c_str());
  TEST_ASSERT_TRUE(file->gzip);
  TEST_ASSERT_EQUAL_STRING("text/html", file->contentType.c_str());
}

void test_static_falls_back_to_plain_file() {
  present = {"/www/_app/immutable/entry/app.js"};
  auto file = portal::resolveStatic("/_app/immutable/entry/app.js", onCard);
  TEST_ASSERT_TRUE(file.has_value());
  TEST_ASSERT_FALSE(file->gzip);
  TEST_ASSERT_EQUAL_STRING("text/javascript", file->contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("max-age=31536000, immutable", file->cacheControl.c_str());
}

void test_static_resolves_html_and_directory_index() {
  present = {"/www/about.html"};
  auto suffixed = portal::resolveStatic("/about", onCard);
  TEST_ASSERT_TRUE(suffixed.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/about.html", suffixed->path.c_str());

  present = {"/www/recipes/index.html"};
  auto directory = portal::resolveStatic("/recipes", onCard);
  TEST_ASSERT_TRUE(directory.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/recipes/index.html", directory->path.c_str());
}

void test_static_falls_back_to_spa_shell() {
  present = {"/www/200.html", "/www/index.html"};
  auto file = portal::resolveStatic("/recipes/deep/link", onCard);
  TEST_ASSERT_TRUE(file.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/200.html", file->path.c_str());
  TEST_ASSERT_EQUAL_STRING("no-cache", file->cacheControl.c_str());
}

void test_static_without_any_bundle_resolves_nothing() {
  auto file = portal::resolveStatic("/", onCard);
  TEST_ASSERT_FALSE(file.has_value());
}

void test_static_rejects_directory_traversal() {
  present = {"/config.json", "/www/../config.json"};
  TEST_ASSERT_FALSE(portal::resolveStatic("/../config.json", onCard).has_value());
  TEST_ASSERT_FALSE(portal::resolveStatic("/a/../../config.json", onCard).has_value());
  TEST_ASSERT_FALSE(portal::isSafePath("/..%2fconfig.json/.."));
  TEST_ASSERT_FALSE(portal::isSafePath("/www\\config.json"));
  TEST_ASSERT_TRUE(portal::isSafePath("/_app/immutable/chunks/BBwMp-ts.js"));
}

void test_asset_upload_maps_uuid_to_jpeg_path() {
  auto path = portal::assetPath("/api/assets/9f1c2d34-56ab-47de-8f01-2233445566aa");
  TEST_ASSERT_TRUE(path.has_value());
  TEST_ASSERT_EQUAL_STRING("/assets/9f1c2d34-56ab-47de-8f01-2233445566aa.jpeg",
                           path->c_str());

  const portal::Route resolved =
      portal::route({portal::Method::post, "/api/assets/rec-1"});
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::uploadAsset, resolved.kind);
  TEST_ASSERT_EQUAL_STRING("/assets/rec-1.jpeg", resolved.path.c_str());
}

void test_asset_upload_rejects_unsafe_identifiers() {
  TEST_ASSERT_FALSE(portal::assetPath("/api/assets/../../config.json").has_value());
  TEST_ASSERT_FALSE(portal::assetPath("/api/assets/a/b").has_value());
  TEST_ASSERT_FALSE(portal::assetPath("/api/assets/").has_value());
  TEST_ASSERT_FALSE(portal::assetPath("/api/assets/name.jpeg").has_value());
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::badRequest,
                        portal::route({portal::Method::post, "/api/assets/a b"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::methodNotAllowed,
                        portal::route(get("/api/assets/rec-1")).kind);
}

void test_asset_upload_requires_jpeg_content_type() {
  TEST_ASSERT_TRUE(portal::isJpegContentType("image/jpeg"));
  TEST_ASSERT_TRUE(portal::isJpegContentType("Image/JPEG; charset=binary"));
  TEST_ASSERT_FALSE(portal::isJpegContentType("multipart/form-data; boundary=x"));
  TEST_ASSERT_FALSE(portal::isJpegContentType(""));
}

std::string jpeg(unsigned char sofMarker, int width, int height) {
  const unsigned char bytes[] = {
      0xFF, 0xD8,
      0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
      0xFF, sofMarker, 0x00, 0x11, 0x08,
      static_cast<unsigned char>(height >> 8), static_cast<unsigned char>(height & 0xFF),
      static_cast<unsigned char>(width >> 8), static_cast<unsigned char>(width & 0xFF),
      0x03};
  return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void test_upload_accepts_baseline_jpeg() {
  const portal::JpegInfo info = portal::inspectJpeg(jpeg(0xC0, 320, 240));
  TEST_ASSERT_EQUAL_INT(portal::JpegKind::baseline, info.kind);
  TEST_ASSERT_EQUAL_INT(320, info.width);
  TEST_ASSERT_EQUAL_INT(240, info.height);
  TEST_ASSERT_FALSE(portal::jpegRejection(info).has_value());
}

void test_upload_rejects_progressive_jpeg() {
  const portal::JpegInfo info = portal::inspectJpeg(jpeg(0xC2, 320, 240));
  TEST_ASSERT_EQUAL_INT(portal::JpegKind::progressive, info.kind);
  const auto rejection = portal::jpegRejection(info);
  TEST_ASSERT_TRUE(rejection.has_value());
  TEST_ASSERT_NOT_NULL(strstr(rejection->c_str(), "progressive"));
}

void test_upload_rejects_non_jpeg_and_oversized() {
  TEST_ASSERT_EQUAL_INT(portal::JpegKind::notJpeg, portal::inspectJpeg("PNG\r\n").kind);
  TEST_ASSERT_EQUAL_INT(portal::JpegKind::notJpeg, portal::inspectJpeg("").kind);
  TEST_ASSERT_TRUE(portal::jpegRejection(portal::inspectJpeg("not an image")).has_value());
  TEST_ASSERT_TRUE(
      portal::jpegRejection(portal::inspectJpeg(jpeg(0xC0, 1920, 1080))).has_value());
  TEST_ASSERT_FALSE(
      portal::jpegRejection(portal::inspectJpeg(jpeg(0xC0, 800, 480))).has_value());
}

void test_upload_accepts_a_jpeg_whose_frame_header_is_beyond_the_first_chunk() {
  std::string truncated = jpeg(0xC0, 320, 240).substr(0, 8);
  const portal::JpegInfo info = portal::inspectJpeg(truncated);
  TEST_ASSERT_EQUAL_INT(portal::JpegKind::undetermined, info.kind);
  TEST_ASSERT_FALSE(portal::jpegRejection(info).has_value());
}

void test_www_upload_maps_nested_bundle_paths() {
  auto nested = portal::wwwUploadPath("/api/www/_app/immutable/chunks/BBwMp-ts.js");
  TEST_ASSERT_TRUE(nested.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/_app/immutable/chunks/BBwMp-ts.js", nested->c_str());

  auto root = portal::wwwUploadPath("/api/www/index.html?v=1");
  TEST_ASSERT_TRUE(root.has_value());
  TEST_ASSERT_EQUAL_STRING("/www/index.html", root->c_str());

  const portal::Route resolved =
      portal::route({portal::Method::put, "/api/www/_app/version.json"});
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::uploadWww, resolved.kind);
  TEST_ASSERT_EQUAL_STRING("/www/_app/version.json", resolved.path.c_str());
}

void test_www_upload_rejects_traversal_and_absolute_paths() {
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/../config.json").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/a/../../config.json").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www//config.json").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/a\\b.js").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/a%2e%2e/b.js").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/nested/").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/assets/rec-1").has_value());
  TEST_ASSERT_FALSE(portal::wwwUploadPath("/api/www/" + std::string(200, 'a')).has_value());

  TEST_ASSERT_EQUAL_INT(portal::RouteKind::unsafeWwwPath,
                        portal::route({portal::Method::put, "/api/www/../config.json"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::methodNotAllowed,
                        portal::route(get("/api/www/index.html")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::methodNotAllowed,
                        portal::route({portal::Method::post, "/api/www/index.html"}).kind);
}

void test_bootstrap_serves_when_the_bundle_is_missing() {
  TEST_ASSERT_FALSE(portal::bundleInstalled(onCard));
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::bootstrap,
                        portal::decideStatic("/", onCard).outcome);
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::bootstrap,
                        portal::decideStatic("/recipes/deep/link", onCard).outcome);

  present = {"/assets/rec-1.jpeg"};
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::bootstrap,
                        portal::decideStatic("/", onCard).outcome);

  const portal::Response page = portal::bootstrap();
  TEST_ASSERT_EQUAL_INT(200, page.status);
  TEST_ASSERT_EQUAL_STRING("text/html", page.contentType.c_str());
  TEST_ASSERT_TRUE(page.location.empty());
}

void test_bootstrap_yields_to_an_installed_bundle() {
  present = {"/www/index.html"};
  TEST_ASSERT_TRUE(portal::bundleInstalled(onCard));
  const portal::StaticDecision decision = portal::decideStatic("/", onCard);
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::file, decision.outcome);
  TEST_ASSERT_EQUAL_STRING("/www/index.html", decision.file.path.c_str());

  present = {"/www/index.html.gz"};
  TEST_ASSERT_TRUE(portal::bundleInstalled(onCard));
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::file,
                        portal::decideStatic("/recipes/deep/link", onCard).outcome);

  present = {"/www/200.html", "/www/index.html"};
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::file,
                        portal::decideStatic("/recipes/deep/link", onCard).outcome);
  TEST_ASSERT_EQUAL_STRING(
      "/www/200.html", portal::decideStatic("/recipes/deep/link", onCard).file.path.c_str());
}

void test_bootstrap_never_pre_empts_a_captive_probe() {
  for (const auto& probe : portal::probePaths()) {
    TEST_ASSERT_EQUAL_INT(portal::RouteKind::captiveRedirect, portal::route(get(probe)).kind);
    TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::redirect,
                          portal::decideStatic(probe, onCard).outcome);
  }
  TEST_ASSERT_EQUAL_INT(portal::StaticOutcome::redirect,
                        portal::decideStatic("/generate_204?t=1", onCard).outcome);
}

void test_bootstrap_page_is_small_and_self_contained() {
  const std::string body = portal::bootstrap().body;
  TEST_ASSERT_GREATER_THAN_size_t(0, body.size());
  TEST_ASSERT_LESS_THAN_size_t(8192, body.size());
  TEST_ASSERT_NOT_NULL(strstr(body.c_str(), "webkitdirectory"));
  TEST_ASSERT_NOT_NULL(strstr(body.c_str(), "/api/www/"));
  TEST_ASSERT_NULL(strstr(body.c_str(), "http://"));
  TEST_ASSERT_NULL(strstr(body.c_str(), "https://"));
  TEST_ASSERT_NULL(strstr(body.c_str(), "src="));
  TEST_ASSERT_NULL(strstr(body.c_str(), "href="));
}

void test_unsafe_bundle_path_is_a_client_error_not_a_redirect() {
  const portal::Route resolved =
      portal::route({portal::Method::put, "/api/www/../../config.json"});
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::unsafeWwwPath, resolved.kind);
  TEST_ASSERT_TRUE(resolved.path.empty());
  TEST_ASSERT_FALSE(portal::Lockout([] { return true; }).blocks(resolved.kind));
}

void test_api_routes() {
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::health, portal::route(get("/api/health")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::getRecipes,
                        portal::route(get("/api/recipes")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::putRecipes,
                        portal::route({portal::Method::put, "/api/recipes"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::getConfig, portal::route(get("/api/config")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::putConfig,
                        portal::route({portal::Method::put, "/api/config"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::notFound, portal::route(get("/api/nope")).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::methodNotAllowed,
                        portal::route({portal::Method::post, "/api/health"}).kind);
  TEST_ASSERT_EQUAL_INT(portal::RouteKind::staticFile,
                        portal::route(get("/_app/immutable/entry/app.js")).kind);
}

void test_health_matches_the_openapi_shell() {
  const portal::Response response = portal::health();
  TEST_ASSERT_EQUAL_INT(200, response.status);
  TEST_ASSERT_EQUAL_STRING("application/json", response.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", response.body.c_str());
}

void test_lockout_blocks_writes_while_pouring() {
  bool pouring = true;
  const portal::Lockout locked([&pouring] { return pouring; });

  TEST_ASSERT_TRUE(locked.blocks(portal::RouteKind::putRecipes));
  TEST_ASSERT_TRUE(locked.blocks(portal::RouteKind::putConfig));
  TEST_ASSERT_TRUE(locked.blocks(portal::RouteKind::uploadAsset));
  TEST_ASSERT_TRUE(locked.blocks(portal::RouteKind::uploadWww));
  TEST_ASSERT_FALSE(locked.blocks(portal::RouteKind::getRecipes));
  TEST_ASSERT_FALSE(locked.blocks(portal::RouteKind::staticFile));
  TEST_ASSERT_FALSE(locked.blocks(portal::RouteKind::captiveRedirect));
  TEST_ASSERT_FALSE(locked.blocks(portal::RouteKind::health));

  pouring = false;
  TEST_ASSERT_FALSE(locked.blocks(portal::RouteKind::putRecipes));

  TEST_ASSERT_EQUAL_INT(409, portal::conflict().status);
  TEST_ASSERT_EQUAL_STRING("application/json", portal::conflict().contentType.c_str());
}

void test_content_type_mapping() {
  TEST_ASSERT_EQUAL_STRING("text/css", portal::contentTypeFor("/www/app.css").c_str());
  TEST_ASSERT_EQUAL_STRING("application/json",
                           portal::contentTypeFor("/www/_app/version.json").c_str());
  TEST_ASSERT_EQUAL_STRING("image/jpeg", portal::contentTypeFor("/assets/x.jpeg").c_str());
  TEST_ASSERT_EQUAL_STRING("image/svg+xml", portal::contentTypeFor("/www/logo.svg").c_str());
  TEST_ASSERT_EQUAL_STRING("application/octet-stream",
                           portal::contentTypeFor("/www/noextension").c_str());
  TEST_ASSERT_EQUAL_STRING("application/octet-stream",
                           portal::contentTypeFor("/www/dir.with.dot/file").c_str());
}


void test_delete_www_clears_the_bundle_root() {
  const auto r = portal::route({portal::Method::del, "/api/www"});
  TEST_ASSERT_EQUAL((int)portal::RouteKind::clearWww, (int)r.kind);
  TEST_ASSERT_EQUAL_STRING(portal::kWebRoot, r.path.c_str());
}

void test_clearing_the_bundle_is_blocked_while_pouring() {
  portal::Lockout pouring([] { return true; });
  portal::Lockout idle([] { return false; });
  TEST_ASSERT_TRUE(pouring.blocks(portal::RouteKind::clearWww));
  TEST_ASSERT_FALSE(idle.blocks(portal::RouteKind::clearWww));
}

void test_www_root_rejects_methods_other_than_delete() {
  for (auto method : {portal::Method::get, portal::Method::put, portal::Method::post}) {
    const auto r = portal::route({method, "/api/www"});
    TEST_ASSERT_EQUAL((int)portal::RouteKind::methodNotAllowed, (int)r.kind);
  }
}

void test_delete_does_not_reach_individual_bundle_files() {
  const auto r = portal::route({portal::Method::del, "/api/www/app.js"});
  TEST_ASSERT_EQUAL((int)portal::RouteKind::methodNotAllowed, (int)r.kind);
}


void test_drink_art_is_served_from_the_card_root_not_the_bundle() {
  auto exists = [](const std::string& p) { return p == "/assets/abc.jpeg"; };
  const auto file = portal::resolveStatic("/assets/abc.jpeg", exists);
  TEST_ASSERT_TRUE(file.has_value());
  TEST_ASSERT_EQUAL_STRING("/assets/abc.jpeg", file->path.c_str());
  TEST_ASSERT_EQUAL_STRING("image/jpeg", file->contentType.c_str());
  TEST_ASSERT_FALSE(file->gzip);
}

void test_missing_drink_art_does_not_fall_back_to_the_spa_shell() {
  auto exists = [](const std::string& p) {
    return p == "/www/200.html" || p == "/www/index.html";
  };
  TEST_ASSERT_FALSE(portal::resolveStatic("/assets/gone.jpeg", exists).has_value());
}

void test_missing_drink_art_does_not_reach_the_bootstrap_page() {
  auto exists = [](const std::string&) { return false; };
  const auto decision = portal::decideStatic("/assets/gone.jpeg", exists);
  TEST_ASSERT_NOT_EQUAL((int)portal::StaticOutcome::file, (int)decision.outcome);
}


void test_delete_asset_maps_uuid_to_the_stored_jpeg() {
  const auto r = portal::route({portal::Method::del, "/api/assets/rec-1"});
  TEST_ASSERT_EQUAL((int)portal::RouteKind::deleteAsset, (int)r.kind);
  TEST_ASSERT_EQUAL_STRING("/assets/rec-1.jpeg", r.path.c_str());
}

void test_delete_asset_rejects_unsafe_identifiers() {
  const auto r = portal::route({portal::Method::del, "/api/assets/../config"});
  TEST_ASSERT_EQUAL((int)portal::RouteKind::badRequest, (int)r.kind);
}

void test_deleting_artwork_is_blocked_while_pouring() {
  portal::Lockout pouring([] { return true; });
  portal::Lockout idle([] { return false; });
  TEST_ASSERT_TRUE(pouring.blocks(portal::RouteKind::deleteAsset));
  TEST_ASSERT_FALSE(idle.blocks(portal::RouteKind::deleteAsset));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_probe_paths_are_detected);
  RUN_TEST(test_probe_route_ignores_query_and_case);
  RUN_TEST(test_probe_route_answers_any_method);
  RUN_TEST(test_redirect_shape_wins_every_os);
  RUN_TEST(test_static_prefers_precompressed);
  RUN_TEST(test_static_falls_back_to_plain_file);
  RUN_TEST(test_static_resolves_html_and_directory_index);
  RUN_TEST(test_static_falls_back_to_spa_shell);
  RUN_TEST(test_static_without_any_bundle_resolves_nothing);
  RUN_TEST(test_static_rejects_directory_traversal);
  RUN_TEST(test_asset_upload_maps_uuid_to_jpeg_path);
  RUN_TEST(test_asset_upload_rejects_unsafe_identifiers);
  RUN_TEST(test_asset_upload_requires_jpeg_content_type);
  RUN_TEST(test_upload_accepts_baseline_jpeg);
  RUN_TEST(test_upload_rejects_progressive_jpeg);
  RUN_TEST(test_upload_rejects_non_jpeg_and_oversized);
  RUN_TEST(test_upload_accepts_a_jpeg_whose_frame_header_is_beyond_the_first_chunk);
  RUN_TEST(test_www_upload_maps_nested_bundle_paths);
  RUN_TEST(test_www_upload_rejects_traversal_and_absolute_paths);
  RUN_TEST(test_bootstrap_serves_when_the_bundle_is_missing);
  RUN_TEST(test_bootstrap_yields_to_an_installed_bundle);
  RUN_TEST(test_bootstrap_never_pre_empts_a_captive_probe);
  RUN_TEST(test_bootstrap_page_is_small_and_self_contained);
  RUN_TEST(test_unsafe_bundle_path_is_a_client_error_not_a_redirect);
  RUN_TEST(test_api_routes);
  RUN_TEST(test_health_matches_the_openapi_shell);
  RUN_TEST(test_lockout_blocks_writes_while_pouring);
  RUN_TEST(test_content_type_mapping);
  RUN_TEST(test_delete_www_clears_the_bundle_root);
  RUN_TEST(test_clearing_the_bundle_is_blocked_while_pouring);
  RUN_TEST(test_www_root_rejects_methods_other_than_delete);
  RUN_TEST(test_delete_does_not_reach_individual_bundle_files);
  RUN_TEST(test_drink_art_is_served_from_the_card_root_not_the_bundle);
  RUN_TEST(test_missing_drink_art_does_not_fall_back_to_the_spa_shell);
  RUN_TEST(test_missing_drink_art_does_not_reach_the_bootstrap_page);
  RUN_TEST(test_delete_asset_maps_uuid_to_the_stored_jpeg);
  RUN_TEST(test_delete_asset_rejects_unsafe_identifiers);
  RUN_TEST(test_deleting_artwork_is_blocked_while_pouring);
  return UNITY_END();
}
