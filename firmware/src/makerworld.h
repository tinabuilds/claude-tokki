#pragma once
#include <stdint.h>

// MakerWorld creator stats, read by the board itself from api.bambulab.com
// (makerworld.com sits behind a bot wall; the app API host does not).
// Sign-in is Bambu's email-code login, so Google/Apple accounts work and no
// password ever reaches the board. Two modes, picked at sign-in:
//   public — keep only the user id; show followers / likes / downloads
//   points — also keep the login token (90 days); add points, boost tokens
//            and unread comments
// Hardware: makerworld.cpp. Sim: boards/sim/makerworld_sim.cpp.

enum mw_link_t {
    MW_OFF = 0,        // never signed in
    MW_WAITING,        // signed in, waiting for Wi-Fi / first fetch
    MW_ONLINE,         // numbers are fresh
    MW_EXPIRED,        // token rejected: sign in again from the setup page
};

struct MakerWorldState {
    mw_link_t link;
    bool     has_data;       // at least one fetch landed
    bool     has_points;     // points mode (token kept)
    int      fans, likes, downloads;
    int      points, boost_tokens;
    int      new_comments;   // unread comments + replies only, not other notifications
    uint32_t seq;            // bumps on every successful fetch
};

void makerworld_init(void);
// Copy the latest state. Returns true when it changed since the previous call.
bool makerworld_get(MakerWorldState* out);
// Serial "mw {json}":
//   {"email":"a@b.c"}                 → Bambu emails a sign-in code
//   {"code":"123456","points":true}   → sign in with it; points=false keeps only the user id
//   {"off":true}                      → forget everything
// Network work happens on the MakerWorld task; results print as "mw: ..." lines.
bool makerworld_cmd(const char* json);
