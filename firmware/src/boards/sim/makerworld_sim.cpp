// Sim stand-in for makerworld.cpp: no network. SIM_MW="fans,likes,downloads[,points,boost,comments]"
// fills the page (three numbers = public mode, six = points mode); unset = never signed in.
// SIM_MW_LINK=<n> forces a mw_link_t (e.g. 3 = token expired).
#include "makerworld.h"
#include <stdio.h>
#include <stdlib.h>

static MakerWorldState st = {};
static bool fresh = true;

void makerworld_init(void) {
    const char* v = getenv("SIM_MW");
    if (v) {
        int n = sscanf(v, "%d,%d,%d,%d,%d,%d", &st.fans, &st.likes, &st.downloads,
                       &st.points, &st.boost_tokens, &st.new_comments);
        st.has_points = n == 6;
        st.has_data = n >= 3;
        st.link = MW_ONLINE;
        st.seq = 1;
    }
    const char* link = getenv("SIM_MW_LINK");
    if (link) st.link = (mw_link_t)atoi(link);
}

bool makerworld_get(MakerWorldState* out) {
    *out = st;
    bool changed = fresh;
    fresh = false;
    return changed;
}

bool makerworld_cmd(const char* json) { (void)json; return false; }
