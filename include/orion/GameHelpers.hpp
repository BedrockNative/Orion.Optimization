#pragma once
namespace orion {
struct GameHelpers { void *parse{}, *copy{}, *grow{}, *destroy{}, *free{}, *fullPath{}; };
const GameHelpers& gameHelpers();
bool gameHelpersReady();
void initializeGameHelpers();
}
