// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Nightwatch bbport_settings.cpp; see vendor/nightwatch/NOTICE.
const PartySwitch PartySwitches[] = {
    {"party_host_revive", "BB_PARTY_B2_HOST_REVIVE", &Values::party_host_revive},
    {"party_host_limbo_revive", "BB_PARTY_HOST_LIMBO_REVIVE", &Values::party_host_limbo_revive},
    {"party_guest_revive_inplace", "BB_PARTY_GUEST_REVIVE_INPLACE", &Values::party_guest_revive_inplace},
    {"party_guest_respawn", "BB_PARTY_GUEST_RESPAWN", &Values::party_guest_respawn},
    {"party_guest_refill", "BB_PARTY_GUEST_REFILL", &Values::party_guest_refill},
    {"party_chalice_rejoin", "BB_PARTY_CHALICE_REJOIN", &Values::party_chalice_rejoin},
    {"party_guest_mark", "BB_PARTY_GUEST_MARK", &Values::party_guest_mark},
    {"party_full_rewards", "BB_PARTY_FULL_REWARDS", &Values::party_full_rewards},
    {"party_items", "BB_PARTY_ITEMS", &Values::party_items},
    {"party_grant_bells", "BB_PARTY_GRANT_BELLS", &Values::party_grant_bells},
    {"party_open_world", "BB_PARTY_OPEN_WORLD", &Values::party_open_world},
    {"party_progress", "BB_PARTY_PROGRESS", &Values::party_progress},
    {"party_progress_npc", "BB_PARTY_PROGRESS_NPC", &Values::party_progress_npc},
    {"party_story", "BB_PARTY_STORY", &Values::party_story},
    {"party_story_mirror", "BB_PARTY_STORY_MIRROR", &Values::party_story_mirror},
    // 0.7.1: read live from Values (BB_PARTY_GATHER is also the director's pre-ring lamp gather,
    // BB_PARTY_KEY_ITEMS_SUMMONED=1 means more without the key view: never exported).
    {"party_kill_record", "BB_PARTY_KILL_RECORD", &Values::party_kill_record, false},
    {"party_npc_mirror", "BB_PARTY_NPC_MIRROR", &Values::party_npc_mirror, false},
    {"party_gather", "BB_PARTY_GATHER", &Values::party_gather, false},
    {"party_covenant_per_player", "BB_PARTY_COVENANT_PER_PLAYER", &Values::party_covenant_per_player, false},
    // 0.7.3: lamp travel voting (party_vote.h reads it live; the host's rule, off by default).
    {"party_travel_vote", "BB_PARTY_TRAVEL_VOTE", &Values::party_travel_vote, false},
    {"party_npc_fights", "BB_PARTY_NPC_FIGHTS", &Values::party_npc_fights, false},  // 0.7.3, D11
    {"party_any_kidnap", "BB_PARTY_ANY_KIDNAP", &Values::party_any_kidnap, false},  // 0.7.3, kidnapall
    {"party_key_view", "BB_PARTY_KEY_VIEW", &Values::party_key_view, false},
    {"party_key_items_summoned", "BB_PARTY_KEY_ITEMS_SUMMONED", &Values::party_key_items_summoned, false},
    // 0.7.2: the rot cleanses (party_balance reads them live; host's rules).
    {"party_rot_cleanse_boss", "BB_PARTY_ROT_CLEANSE_BOSS", &Values::party_rot_cleanse_boss, false},
    {"party_rot_cleanse_killer", "BB_PARTY_ROT_CLEANSE_KILLER", &Values::party_rot_cleanse_killer, false},
    {"party_rot_cleanse_echoes", "BB_PARTY_ROT_CLEANSE_ECHOES", &Values::party_rot_cleanse_echoes, false},
};
const int PartySwitchCount = int(std::size(PartySwitches));

