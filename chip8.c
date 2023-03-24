#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"

#define DEBUG

#define WIDTH  64
#define HEIGHT 32

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
    uint16_t opcode;
    // TOOD could use union here?
    uint16_t nnn; // 12 bit address/constant
    uint8_t nn;   // 8 bit constant
    uint8_t n;    // 4 bit constant
    uint8_t x;    // 4 bit register identifier
    uint8_t y;    // 4 bit register identifier

} instruction_t;

typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];     // memory
    bool display[64 * 32]; // og chip8 resolution
    uint16_t stack[12];    // subroutine stack
    uint16_t *stack_ptr;   // pointer to stack
    uint8_t v[16];         // data registers V0-VF
    uint16_t i;            // index register I
    uint16_t pc;           // program counter
    uint8_t delay_timer;   // decrements at 60hz when >0
    uint8_t sound_timer;   // decrements at 60hz and plays tone when >0
    bool keypad[16];       // hexadecimal keyboard
    const char *rom_name;  // currently running rom
    instruction_t ins;     // currently executing instruction

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

bool init_vm(vm_t *vm, const char *rom_name) {
    const uint32_t entry_point = 0x200; // chip8 rom entry point

    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80, // F
    };

    // load font
    memcpy(&vm->ram[0], font, sizeof(font));

    // load rom
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("Failed to load rom file %s\n", rom_name);
        return false;
    }

    // get rom size
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof vm->ram - entry_point;
    rewind(rom);

    if (rom_size > max_size) {
        SDL_Log("Rom file %s is too big! Rom size: %zu, max allowed: %zu.\n", rom_name, rom_size, max_size);
        return false;
    }

    if (fread(&vm->ram[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Failed to load rom file into vm's ram\n");
        return false;
    };

    fclose(rom);

    // defaults
    vm->state     = RUNNING;
    vm->pc        = entry_point;
    vm->rom_name  = rom_name;
    vm->stack_ptr = &vm->stack[0];

    return true;
}

bool set_config_from_args(config_t *config, const int argc, char **argv) {
    // defaults
    *config = (config_t){
        .window_width  = 64,
        .window_height = 32,
        .fg_color      = 0x0FEEEEFF,
        .bg_color      = 0x020022FF,
        .scale_factor  = 20,
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

void update_screen(const sdl_t sdl, const config_t config, const vm_t *vm) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    const uint8_t fg_r = config.fg_color >> 24;
    const uint8_t fg_g = config.fg_color >> 16;
    const uint8_t fg_b = config.fg_color >> 8;
    const uint8_t fg_a = config.fg_color >> 0;

    const uint8_t bg_r = config.bg_color >> 24;
    const uint8_t bg_g = config.bg_color >> 16;
    const uint8_t bg_b = config.bg_color >> 8;
    const uint8_t bg_a = config.bg_color >> 0;

    // draw rectangle per set pixels in display
    for (uint32_t i = 0; i < sizeof vm->display; i++) {
        rect.x = (i % WIDTH) * config.scale_factor;
        rect.y = (i / WIDTH) * config.scale_factor;

        if (vm->display[i]) {
            // if pixel is on draw fg colour
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        } else {
            // if pixel is on draw bg colour
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }

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

            case SDLK_SPACE:
                if (vm->state == RUNNING) {
                    vm->state = PAUSED;
                    puts("= PAUSED =");
                } else {
                    vm->state = RUNNING;
                    puts("= RESUMED =");
                }
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

#ifdef DEBUG
void print_debug_info(vm_t *vm) {
    printf("Address: 0x%04X, op: 0x%04X, desc: ", vm->pc - 2, vm->ins.opcode);

    switch ((vm->ins.opcode >> 12) & 0x0F) {
    case 0x0:
        if (vm->ins.nn == 0xE0) {
            // 00E0: Clears the screen.
            printf("Clear screen\n");

        } else if (vm->ins.nn == 0xEE) {
            // 0x00EE: Return from subroutine
            printf("Return from subroutine to address 0x%04X\n", *(vm->stack_ptr - 1));
        } else {
            printf("Unimplemented opcode.\n");
        }
        break;

    case 0x1:
        // 1NNN: Jump to address NNN
        printf("Jump to address NNN (0x%04X)\n", vm->ins.nnn);
        break;

    case 0x2:
        // 2NNN: Call subroutine at NNN
        printf("Call subroutine at NNN (0x%04X)\n", vm->ins.nnn);
        break;

    case 0x6:
        // 6XNN: Sets VX to NN.
        printf("Set V%d to NN (0x%02X)\n", vm->ins.x, vm->ins.nn);
        break;

    case 0x7:
        printf("Add NN (0x%02X) to V%d\n", vm->ins.nn, vm->ins.x);
        break;
        // 7XNN: Adds NN to VX (carry flag is not changed).

    case 0xA:
        // ANNN: Sets I to the address NNN.
        printf("Set I to NNN (0x%04X)\n", vm->ins.nnn);
        break;

    case 0xD:
        // DXYN: Draws a sprite at coordinate (VX, VY)
        printf("Draw N (%u) height sprite at coordinate V%X: %u V%X: %u from memory location I (0x%X)\n",
               vm->ins.n, vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y], vm->i);
        break;

    default:
        printf("Unimplemented or invalid opcode\n");
    }
}
#endif

void run_instruction(vm_t *vm) {
    // little endian to big endian
    vm->ins.opcode = (vm->ram[vm->pc] << 8) | vm->ram[vm->pc + 1];
    vm->pc += 2;

    // fill out current instruction format
    // TODO using bitfields could make this easier?
    // TODO can just use local ins var instead of vm->ins?
    vm->ins.nnn = vm->ins.opcode & 0x0FFF;
    vm->ins.nn  = vm->ins.opcode & 0x0FF;
    vm->ins.n   = vm->ins.opcode & 0x0F;
    vm->ins.x   = (vm->ins.opcode >> 8) & 0x0F;
    vm->ins.y   = (vm->ins.opcode >> 4) & 0x0F;

#ifdef DEBUG
    print_debug_info(vm);
#endif

    switch ((vm->ins.opcode >> 12) & 0x0F) {
    case 0x0:
        if (vm->ins.nn == 0xE0) {
            // 00E0: Clears the screen.
            memset(&vm->display[0], false, sizeof vm->display);
        } else if (vm->ins.nn == 0xEE) {
            // 00EE: Returns from a subroutine.
            vm->pc = *(--vm->stack_ptr); // grab last address from subroutine stack (pop)
        }
        break;

    case 0x2:
        // 2NNN: Calls machine code routine at address NNN.
        *vm->stack_ptr++ = vm->pc;      // store current address to return to on stac (push)
        vm->pc           = vm->ins.nnn; // set program counter to subroutine address, so next opcode is gotten from there
        break;

    case 0x6:
        // 6XNN: Sets VX to NN.
        vm->v[vm->ins.x] = vm->ins.nn;
        break;

    case 0x7:
        // 7XNN: Adds NN to VX (carry flag is not changed).
        vm->v[vm->ins.x] += vm->ins.nn;
        break;

    case 0xA:
        // ANNN: Sets I to the address NNN.
        vm->i = vm->ins.nnn;
        break;

    case 0xD:
        // DXYN: Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels and a height of N pixels.
        // Each row of 8 pixels is read as bit-coded starting from memory location I; I value does
        // not change after the execution of this instruction.
        // VF is set to 1 if any screen pixels are flipped from set to unset when the sprite is
        // drawn, and to 0 if that does not happen (this is used for collision detection).

        // wrap around the edges of the screen
        // using '&' instead of 'modulo' as x % y == x & (y - 1) when y is a power of 2
        uint8_t x = vm->v[vm->ins.x] & (WIDTH - 1);
        uint8_t y = vm->v[vm->ins.y] & (HEIGHT - 1);

        const uint8_t og_x = x;

        vm->v[0x0F] = 0; // init carry flag to 0

        // loop over sprite rows (N in total)
        for (uint8_t i = 0; i < vm->ins.n; i++) {
            const uint8_t sprite_data = vm->ram[vm->i + i]; // next byte/row of sprite data

            x = og_x; // reset x for next row to draw

            for (int8_t j = 7; j >= 0; j--) {
                const bool sprite_bit = (sprite_data & (1 << j));
                bool *pixel           = &vm->display[y * WIDTH + x];

                // if sprite pixel is on and display pixel is on, set carry flag
                if (sprite_bit && pixel) vm->v[0x0F] = 1;

                // xor display pixel with sprite pixel to set it on/off
                *pixel ^= sprite_bit;

                // stop drawing if hit right screen edge
                if (++x >= WIDTH) break;
            }

            // stop drawing the entire sprite if hit bottom screen edge
            if (++y >= HEIGHT) break;
        }

        break;

    default:
        break; // unimplemented
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // init config
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    // init SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    // init chip8 vm
    vm_t vm              = {0};
    const char *rom_name = argv[1];
    if (!init_vm(&vm, rom_name)) exit(EXIT_FAILURE);

    clear_screen(sdl, config);

    // main emulator loop
    while (vm.state != QUIT) {
        handle_input(&vm);

        if (vm.state == PAUSED) continue;

        run_instruction(&vm);

        SDL_Delay(16); // delay for ~60hz/60fps

        update_screen(sdl, config, &vm);
    }

    // final cleanup
    deinit_sdl(sdl);

    exit(EXIT_SUCCESS);
}
