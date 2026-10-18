// Patch manifests: byte patches to the loaded eboot as data
// files, so a patch is a file next to the host rather than code in it, and a
// mod can ship its own. docs/modding.md is the format; the short of it:
//
//   [patch]
//   name = "skip-intro"
//   eboot = "941f887a..."            # the SHA-256 the addresses belong to
//   description = "..."
//   option = "startup.skip_intro"    # optional: a config switch that enables it
//   [site.fromsoft]
//   address = "0x04d99138"           # guest VA at the preferred 0x400000 base
//   expect = "4c 00 6f 00"           # what must be there
//   bytes = "00 00 00 00"            # what is written
//   kind = "code"                    # or "data": how the page is protected after
//
// A manifest applies only when its eboot hash is the loaded image's, and
// only whole: every site's expected bytes are checked before any is
// written. Files come from <exe dir>/patches, <exe dir>/../patches (a build
// tree) and <mods>/patches, in that order; a later file with the same name
// replaces an earlier one.
//
// A player leaves one out with `<name> = false` under [patches] in
// bbhost.toml, which the setup window's Patches tab writes.
#pragma once

#include <string>
#include <vector>

struct ElfImage;

// Reads the manifests and applies the ones that match; one log line each.
void patch_manifests_apply(ElfImage* image);

// One manifest as the setup window's Patches tab lists it (host/launcher.cpp):
// read the way patch_manifests_apply reads it, nothing applied.
struct PatchListing {
    std::string path;  // the file it was read from
    std::string name, description, option, eboot;
    bool enabled = true;  // its own [patch] enabled
    std::string error;    // why it does not parse ("" when it does)
};
// Every manifest patch_manifests_apply would read, by file name, a later
// directory's replacing an earlier one's. `mods` is the mods directory ("" for none).
std::vector<PatchListing> patch_manifests_list(const std::string& mods);
// Whether the player left this manifest out: [patches] <name> = false.
bool patch_manifest_off(const std::string& name);
// Whether `option` is one a manifest may name (startup.skip_intro, ...).
bool patch_manifest_option_known(const std::string& option);
