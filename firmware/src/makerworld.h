#pragma once
#include <stdint.h>
#include <stddef.h>

// MakerWorld creator stats, read by the board itself from api.bambulab.com
// (makerworld.com sits behind a bot wall; the app API host does not).
// Sign-in is Bambu's email-code login, so Google/Apple accounts work and no
// password ever reaches the board. Two modes, picked at sign-in:
//   public — keep only the user id; show followers / likes / downloads
//   points — also keep the login token (90 days); add points, boosts
//            received and unread comments + replies
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
    int      points, boosts;  // boosts = total boosts ever received on your models
    int      new_comments;   // unread comments + replies only, not other notifications
    char     name[32];       // MakerWorld display name
    uint32_t avatar_seq;     // bumps when a new avatar image is ready
    uint32_t seq;            // bumps on every successful fetch
};

void makerworld_init(void);
// Avatar as ARGB8888 (w*h*4 bytes), or nullptr before the first download.
// The buffer stays valid until avatar_seq changes again.
const uint8_t* makerworld_avatar(int* w, int* h);
// Decode a PNG into an ARGB8888 buffer for makerworld_avatar(). Shared with the sim.
bool makerworld_set_avatar_png(const uint8_t* png, size_t len);
// Copy the latest state. Returns true when it changed since the previous call.
bool makerworld_get(MakerWorldState* out);
// Serial "mw {json}":
//   {"email":"a@b.c"}                 → Bambu emails a sign-in code
//   {"code":"123456","points":true}   → sign in with it; points=false keeps only the user id
//   {"off":true}                      → forget everything
// Network work happens on the MakerWorld task; results print as "mw: ..." lines.
bool makerworld_cmd(const char* json);
