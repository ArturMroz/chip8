#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

typedef struct {
    uint16_t window_width;
    uint16_t window_height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint16_t scale_factor;
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

typedef struct {
    emulator_state_t state;
} vm_t;

bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Failed to init SDL: %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow(
        "Chip8 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.window_width * config.scale_factor,
        config.window_height * config.scale_factor,
        SDL_WINDOW_OPENGL);

    if (!sdl->window) {
        SDL_Log("Failed to create a window: %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Failed to create a renderer: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

void deinit_sdl(sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

bool init_vm(vm_t *vm) {
    vm->state = RUNNING;
    return true;
}

bool set_config_from_args(config_t *config, const int argc, char **argv) {
    // defaults
    *config = (config_t){
        .window_width  = 64,
        .window_height = 32,
        .fg_color      = 0x00AA00FF,
        .bg_color      = 0xFF22EEFF,
        .scale_factor  = 10,
    };

    // overrides
    for (int i = 1; i < argc; i++) {
        (void)argv[i]; // shush unused vars warnings
    }

    return true;
}

void clear_screen(const sdl_t sdl, const config_t config) {
    const uint8_t r = config.bg_color >> 24;
    const uint8_t g = config.bg_color >> 16;
    const uint8_t b = config.bg_color >> 8;
    const uint8_t a = config.bg_color >> 0;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}

void handle_input(vm_t *vm) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            vm->state = QUIT;
            return;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                vm->state = QUIT;
                return;

            default:
                break;
            }
            break;

        case SDL_KEYUP:
            break;

        default:
            break;
        }
    }
}

int main(int argc, char **argv) {
    // init config
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    // init SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    // init chip8 vm
    vm_t vm = {0};
    if (!init_vm(&vm)) exit(EXIT_FAILURE);

    clear_screen(sdl, config);

    // main emulator loop
    while (vm.state != QUIT) {
        handle_input(&vm);

        // delay for ~60hz/60fps
        SDL_Delay(16);

        update_screen(sdl);
    }

    // final cleanup
    deinit_sdl(sdl);

    exit(EXIT_SUCCESS);
}
