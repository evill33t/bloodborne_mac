// The update check, against GitHub releases (CONTRIBUTING.md, "Releasing"). A
// release build looks for a newer release at start; the F10 screen's UPDATES
// rows and the setup window say what it found, and a newer release's page
// opens in the browser, where the player downloads it. bbhost never downloads
// or replaces its own executable.
//
// update.source is the "releases/latest" API URL; a private repository's
// needs update.token_file. Nothing is sent but that token and a user agent:
// never the account's.
//
// Each release carries the bare executables under the same names every time
// (bbhost.exe, bbhost), SHA256SUMS and SHA256SUMS.sig, besides the packages.
// Older builds' updater looked for bbhost-<tag>-windows.exe; finding none, it
// says to get the release from its page. The release key below signs
// SHA256SUMS and the official plugins.
#pragma once

#include <cstdint>
#include <string>

namespace updater {

// This build's version (BBHOST_VERSION: "v0.1.0", "v0.1.0-3-gabc1234",
// "v0.0.0-<rev>", "+" for a changed tree).
const char* current_version();

// At start: the copy an older build's update set aside removed (every start),
// and the check begun on a thread of its own when update.check (or a release
// build) says so and there is a window to offer it in - unless the setup
// window has checked already.
// BBHOST_UPDATE_TEST=1 checks regardless, headless too, and logs what it finds.
void start();

// The setup window, while its box is checked: a check at once, unless one has
// run or is running.
void check_once();

// The F10 rows and the setup window.
std::string status();      // "bbhost v0.1.0 - up to date", "bbhost v0.2.0 is available (this is v0.1.0)", ...
std::string detail();      // why a check failed, or the newer release's page
bool busy();               // a check is running
void check_now();          // look again (the "Check for updates" row)
bool update_available();   // a newer release was found
std::string latest_tag();  // its tag ("v0.2.0"), "" until one is found
// Its page in the browser (the "Open the release page" row, the setup
// window's button): false when there is none or the browser did not open.
bool open_release_page();

// The release signing key's public half (32 bytes): what signs SHA256SUMS,
// and the official plugins (host/plugins.cpp).
const std::uint8_t* release_key();

// The official plugins of the latest release, each verified against the
// release key, into `dir` (the plugin manager's "Get official plugins"). On
// a thread of its own; plugins_status() says how it went.
void fetch_plugins(const std::string& dir);
std::string plugins_status();
bool plugins_busy();

// The pieces, for tests (tests/updater_test.cpp).
// "v1.2.3..." -> 1, 2, 3; false when it does not start that way.
bool parse_version(const std::string& v, int out[3]);
// The release tag is newer than this build: a higher major.minor.patch. A
// build past a tag ("v0.1.0-3-g...") is that tag's version, so the next
// release is newer and the same tag is not.
bool is_newer(const std::string& tag, const std::string& current);
// The manifest's Ed25519 signature (64 raw bytes) against `public_key` (32).
bool signature_ok(const std::string& manifest, const std::string& signature, const std::uint8_t public_key[32]);
// What a "releases/latest" answer says: the release's tag, and the page to
// open for it - its html_url, else bbhost's page for the tag, else bbhost's
// latest release. False when the answer is not a release (no JSON object, no
// tag).
bool parse_release(const std::string& body, std::string& tag, std::string& page);
// A page the browser may be sent to. It comes from the network, so only
// https://github.com/ and only the characters a release page needs.
bool release_page_ok(const std::string& url);

}  // namespace updater
