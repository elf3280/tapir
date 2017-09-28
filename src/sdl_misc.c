#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include "gl_misc.h"
#include "sdl_misc.h"
#include "Sprite.h"
#include "Window.h"
#include "Input.h"
#include "RGSSReset.h"
#include "Tilemap.h"

#if RGSS >= 2
int window_width = 544;
int window_height = 416;
#else
int window_width = 640;
int window_height = 480;
#endif
int window_brightness = 255;
SDL_Window *window = NULL;
SDL_GLContext glcontext = NULL;
static size_t registry_size, registry_capacity;
static struct Renderable **registry;

static GLuint transition_shader;

static void initTransition(void);
static void deinitTransition(void);

void initSDL() {
  registry_capacity = 100;
  registry = malloc(sizeof(*registry) * registry_capacity);

  if(SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    exit(1);
  }

  int img_flags = IMG_INIT_JPG|IMG_INIT_PNG|IMG_INIT_TIF;
  if(IMG_Init(img_flags) != img_flags) {
    fprintf(stderr, "IMG_Init Error: %s\n", IMG_GetError());
    exit(1);
  }

  if(TTF_Init()) {
    fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError());
    exit(1);
  }

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

  /* TODO: fetch window title from Game.ini */
  window = SDL_CreateWindow("tapir",
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      window_width, window_height,
      SDL_WINDOW_OPENGL);
  if(!window) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    exit(1);
  }

  // int major, minor;
  // SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  // SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
  // fprintf(stderr, "GL version: %d.%d\n", major, minor);

  glcontext = SDL_GL_CreateContext(window);
  if(!glcontext) {
    fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
    exit(1);
  }

  initTransition();
  initSpriteSDL();
  initWindowSDL();
  initTilemapSDL();
}

void cleanupSDL() {
  deinitTilemapSDL();
  deinitWindowSDL();
  deinitSpriteSDL();
  deinitTransition();
  if(glcontext) SDL_GL_DeleteContext(glcontext);
  if(window) SDL_DestroyWindow(window);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  if(registry) free(registry);
}

static int compare_renderables(const void *o1, const void *o2) {
  // TODO: use viewport, y and generated time.
  struct Renderable *r1 = *((struct Renderable * const *)o1);
  struct Renderable *r2 = *((struct Renderable * const *)o2);
  if(r1->z < r2->z) return -1;
  if(r1->z > r2->z) return 1;
  return 0;
}

void event_loop() {
  SDL_Event e;
  int quit = 0;

  while(SDL_PollEvent(&e)) {
    switch(e.type) {
      case SDL_KEYDOWN:
        if(e.key.keysym.sym == SDLK_F12) {
          rb_raise(rb_eRGSSReset, "RGSS Reset");
        }
        if(!e.key.repeat) {
          keyPressed(e.key.keysym.sym);
        }
        break;
      case SDL_KEYUP:
        keyReleased(e.key.keysym.sym);
        break;
      case SDL_QUIT:
        quit = 1;
        break;
    }
  }

  if(quit) {
    exit(0);
  }
}

void renderSDL() {
  qsort(registry, registry_size, sizeof(*registry), compare_renderables);

  SDL_GL_MakeCurrent(window, glcontext);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glScissor(0, 0, window_width, window_height);
  glViewport(0, 0, window_width, window_height);
  int last_z = INT_MIN;
  for(size_t i = 0; i < registry_size; ++i) {
    renderTilemaps(last_z, registry[i]->z);
    last_z = registry[i]->z;
    registry[i]->render(registry[i]);
  }
  renderTilemaps(last_z, INT_MAX);

  if(window_brightness != 255) {
    // transition

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(transition_shader);
    glUniform1f(glGetUniformLocation(transition_shader, "brightness"),
        window_brightness / 255.0);

    gl_draw_rect(-1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0);

    glUseProgram(0);
  }

  SDL_GL_SwapWindow(window);
}

void registerRenderable(struct Renderable *renderable) {
  if(registry_size >= registry_capacity) {
    registry_capacity = registry_capacity + registry_capacity / 2;
    registry = realloc(registry, sizeof(*registry) * registry_capacity);
  }
  registry[registry_size++] = renderable;
}

void unregisterRenderable(struct Renderable *renderable) {
  size_t i;
  for(i = 0; i < registry_size; ++i) {
    if(registry[i] == renderable) break;
  }
  if(i == registry_size) return;
  registry[i] = registry[registry_size - 1];
  --registry_size;
}

static void initTransition(void) {
  static const char *vsh_source =
    "#version 120\n"
    "\n"
    "void main(void) {\n"
    "    gl_Position = gl_Vertex;\n"
    "}\n";

  static const char *fsh_source =
    "#version 120\n"
    "#if __VERSION__ >= 130\n"
    "#define texture2D texture\n"
    "#define texture2DProj textureProj\n"
    "#endif\n"
    "\n"
    "uniform float brightness;\n"
    "\n"
    "void main(void) {\n"
    "    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0 - brightness);\n"
    "}\n";

  transition_shader = compileShaders(vsh_source, fsh_source);
}
static void deinitTransition(void) {
  if(transition_shader) glDeleteProgram(transition_shader);
}
