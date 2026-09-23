#ifndef PIPBOY_H
#define PIPBOY_H

#include "db.h"

namespace fallout {

typedef enum PipboyOpenIntent {
    PIPBOY_OPEN_INTENT_UNSPECIFIED = 0,
    PIPBOY_OPEN_INTENT_REST = 1,
} PipboyOpenIntent;

int pipboyOpen(int intent);
void pipboyInit();
void pipboyReset();
int pipboySave(File* stream);
int pipboyLoad(File* stream);

// Pip-Boy Link: read access to quest metadata (see data/quests.txt) and the
// localized quest texts (map.msg for the location, quests.msg for the title).
// The quest table stays loaded for the lifetime of the process (it is no
// longer freed when the in-game pip-boy window closes), so these are safe to
// call from the link server's sampler thread at any time during gameplay.
int pipboyQuestsEnsureLoaded();
int pipboyQuestsGetCount();
bool pipboyQuestsGetEntry(int index, int* location, int* description, int* gvar, int* displayThreshold, int* completedThreshold);
const char* pipboyQuestGetLocationText(int location);
const char* pipboyQuestGetDescriptionText(int description);

} // namespace fallout

#endif /* PIPBOY_H */
