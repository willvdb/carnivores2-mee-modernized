#include "System.h"
#include <SDL3/SDL_main.h>

int RunGame();
int main(int argc, char** argv)
{
    Platform::SetArguments(argc, argv);
    return RunGame();
}
