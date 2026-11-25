#include "menu/session.h"

#include <string.h>

#include "main/settings.h"
#include "menu/bests_file.h"
#include "shared/drill_session.h"

namespace tm {
namespace menu {
    static Session* s_session = NULL;
    static Session s_fallback;

    Session* attachSession(u8* block)
    {
        if (block == NULL)
        {
            s_session = NULL;
            return NULL;
        }
        s_session = drills::sessionIn(block);
        if (s_session == NULL)
        {
            s_session = reinterpret_cast<Session*>(block + drills::kSessionOffset);
            memset(s_session, 0, sizeof(Session));
            s_session->magic = drills::kSessionMagic;
            s_session->version = drills::kSessionVersion;
            s_session->page = 0;
            bests::load(*s_session);
        }
        else
        {
            // Back from a match: what it raised goes to the card.
            bests::save(*s_session);
        }
        return s_session;
    }

    Session& session()
    {
        return s_session != NULL ? *s_session : s_fallback;
    }
}
}
