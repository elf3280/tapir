#ifndef OPENRES_H
#define OPENRES_H

#include <ruby.h>
#include <SDL.h>

SDL_RWops *openres(const char *path);

#endif /* OPENRES_H */