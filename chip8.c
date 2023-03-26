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
    bool pixel_border;
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

    // vm->ram[0x1FF] = 2; // set magic number to run test #2 directly

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
        .pixel_border  = false,
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

    if (config.pixel_border) {
        rect.w -= 2;
        rect.h -= 2;
    }

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
    printf("addr: 0x%04X, op: 0x%04X, desc: ", vm->pc - 2, vm->ins.opcode);

    switch ((vm->ins.opcode >> 12) & 0x0F) {
    case 0x0:
        if (vm->ins.nn == 0xE0) {
            // 00E0: Clears the screen.
            printf("Clear screen\n");
        } else if (vm->ins.nn == 0xEE) {
            // 00EE: Return from subroutine
            printf("Return from subroutine to address 0x%04X\n", *(vm->stack_ptr - 1));
        } else {
            // 0NNN: Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN.
            printf("Call machine code routine at address 0x%03X\n", vm->ins.nnn);
        }
        break;

    case 0x1:
        // 1NNN: Jump to address NNN
        printf("Jump to address NNN (0x%03X)\n", vm->ins.nnn);
        break;

    case 0x2:
        // 2NNN: Call subroutine at NNN
        printf("Call subroutine at NNN (0x%03X)\n", vm->ins.nnn);
        break;

    case 0x3:
        // 3XNN: Skips the next instruction if VX equals NN.
        printf("Skip the next instruction if V%u (0x%02X) == NN (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.nn);
        break;

    case 0x4:
        // 4XNN: Skips the next instruction if VX does not equal NN.
        printf("Skip the next instruction if V%u (0x%02X) != NN (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.nn);
        break;

    case 0x5:
        // 5XY0: Skips the next instruction if VX equals YY.
        printf("Skip the next instruction if V%u (0x%02X) == V%u (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
        break;

    case 0x6:
        // 6XNN: Sets VX to NN.
        printf("Set V%d to NN (0x%02X)\n", vm->ins.x, vm->ins.nn);
        break;

    case 0x7:
        printf("Add NN (0x%02X) to V%d\n", vm->ins.nn, vm->ins.x);
        break;
        // 7XNN: Adds NN to VX (carry flag is not changed).

    case 0x8:
        switch (vm->ins.n) {
        case 0x0:
            // 8XY0: Sets VX to the value of VY.
            printf("Set V%d (0x%02X) = V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x1:
            // 8XY1: Sets VX to VX or VY. (bitwise OR operation)
            printf("Set V%d (0x%02X) |= V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x2:
            // 8XY2: Sets VX to VX and VY. (bitwise AND operation)
            printf("Set V%d (0x%02X) &= V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x3:
            // 8XY3: Sets VX to VX xor VY.
            printf("Set V%d (0x%02X) ^= V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x4:
            // 8XY4: Adds VY to VX. VF is set to 1 when there's a carry, and to 0 when there is not.
            printf("Set V%d (0x%02X) += V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x5:
            // 8XY5: VY is subtracted from VX. VF is set to 0 when there's a borrow, and 1 when there is not.
            printf("Set V%d (0x%02X) -= V%d (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0x6:
            // 8XY6 Stores the least significant bit of VX in VF and then shifts VX to the right by 1.
            printf("Set V%d (0x%02X) >>= 1 and VF to (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->v[vm->ins.x] & 1);
            break;

        case 0x7:
            // 8XY7: Sets VX to VY minus VX. VF is set to 0 when there's a borrow, and 1 when there is not.
            printf("Set V%d (0x%02X) = V%d (0x%02X) - V%d\n", vm->ins.x, vm->v[vm->ins.x], vm->ins.x, vm->ins.y, vm->v[vm->ins.y]);
            break;

        case 0xE:
            // 8XYE: Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
            printf("Set V%d (0x%02X) <<= 1 and VF to (0x%02X)\n", vm->ins.x, vm->v[vm->ins.x], vm->v[vm->ins.x] & 0x80);
            break;

        default:
            break; // unimplemented
        }
        break;

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
        } else {
            // 0NNN: Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN.
            vm->pc = vm->ins.nnn;
        }
        break;

    case 0x1:
        // 1NNN: Jumps to address NNN.
        vm->pc = vm->ins.nnn;
        break;

    case 0x2:
        // 2NNN: Calls machine code routine at address NNN.
        *vm->stack_ptr++ = vm->pc;      // store current address to return to on stacc (push)
        vm->pc           = vm->ins.nnn; // set program counter to subroutine address, so next opcode is gotten from there
        break;

    case 0x3:
        // 3XNN: Skips the next instruction if VX equals NN.
        if (vm->v[vm->ins.x] == vm->ins.nn) {
            vm->pc += 2;
        }
        break;

    case 0x4:
        // 4XNN: Skips the next instruction if VX does not equal NN.
        if (vm->v[vm->ins.x] != vm->ins.nn) {
            vm->pc += 2;
        }
        break;

    case 0x5:
        // 5XY0: Skips the next instruction if VX equals VY.
        if (vm->v[vm->ins.x] == vm->v[vm->ins.y]) {
            vm->pc += 2;
        }
        break;

    case 0x6:
        // 6XNN: Sets VX to NN.
        vm->v[vm->ins.x] = vm->ins.nn;
        break;

    case 0x7:
        // 7XNN: Adds NN to VX (carry flag is not changed).
        vm->v[vm->ins.x] += vm->ins.nn;
        break;

    case 0x8:
        switch (vm->ins.n) {
        case 0x0:
            // 8XY0: Sets VX to the value of VY.
            vm->v[vm->ins.x] = vm->v[vm->ins.y];
            break;

        case 0x1:
            // 8XY1: Sets VX to VX or VY. (bitwise OR operation)
            vm->v[vm->ins.x] |= vm->v[vm->ins.y];
            break;

        case 0x2:
            // 8XY2: Sets VX to VX and VY. (bitwise AND operation)
            vm->v[vm->ins.x] &= vm->v[vm->ins.y];
            break;

        case 0x3:
            // 8XY3: Sets VX to VX xor VY.
            vm->v[vm->ins.x] ^= vm->v[vm->ins.y];
            break;

        case 0x4:
            // 8XY4: Adds VY to VX. VF is set to 1 when there's a carry, and to 0 when there is not.
            const uint8_t ogx = vm->v[vm->ins.x];
            vm->v[vm->ins.x] += vm->v[vm->ins.y];
            vm->v[0xF] = ogx < vm->v[vm->ins.x];
            break;

        case 0x5:
            // 8XY5: VY is subtracted from VX. VF is set to 0 when there's a borrow, and 1 when there is not.
            vm->v[0xF] = vm->v[vm->ins.x] > vm->v[vm->ins.y];
            vm->v[vm->ins.x] -= vm->v[vm->ins.y];
            break;

        case 0x6:
            // 8XY6 Stores the least significant bit of VX in VF and then shifts VX to the right by 1.
            vm->v[0xF] = vm->v[vm->ins.x] & 0x1;
            vm->v[vm->ins.x] >>= 1;
            break;

        case 0x7:
            // 8XY7: Sets VX to VY minus VX. VF is set to 0 when there's a borrow, and 1 when there is not.
            vm->v[0xF]       = vm->v[vm->ins.y] > vm->v[vm->ins.x];
            vm->v[vm->ins.x] = vm->v[vm->ins.y] - vm->v[vm->ins.x];
            break;

        case 0xE:
            // 8XYE: Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
            vm->v[0xF] = vm->v[vm->ins.x] & 0x80;
            vm->v[vm->ins.x] <<= 1;
            break;

        default:
            break; // unimplented
        }
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

                // stop drawing if we hit right screen edge
                if (++x >= WIDTH) break;
            }

            // stop drawing the entire sprite if we hit bottom screen edge
            if (++y >= HEIGHT) break;
        }
        break;

    case 0xF:
        switch (vm->ins.nn) {
        case 0x07: // FX07:	Timer	Vx = get_delay()	Sets VX to the value of the delay timer.
            break;
        case 0x0A: // FX0A:	KeyOp	Vx = get_key()	A key press is awaited, and then stored in VX (blocking operation, all instruction halted until next key event).
            break;
        case 0x15: // FX15:	Timer	delay_timer(Vx)	Sets the delay timer to VX.
            break;
        case 0x18: // FX18:	Sound	sound_timer(Vx)	Sets the sound timer to VX.
            break;

        case 0x1E:
            // FX1E: Adds VX to I. VF is not affected.
            vm->i += vm->v[vm->ins.x];
            break;

        case 0x29:
            // FX29: Sets I to the location of the sprite for the character in VX. Characters 0-F (in hexadecimal) are represented by a 4x5 font.
            const uint8_t ch = vm->v[vm->ins.x] & 0x0F;
            vm->i            = vm->ram[ch * 5];
            break;

        case 0x33:
            // FX33: Stores the binary-coded decimal representation of VX, with the hundreds digit
            // in memory at location in I, tens digit at I+1, and ones digit at I+2.
            const uint8_t x    = vm->v[vm->ins.x];
            vm->ram[vm->i]     = x / 100;      // hundreds
            vm->ram[vm->i + 1] = x % 100 / 10; // tens
            vm->ram[vm->i + 2] = x % 10;       // digit
            break;

        case 0x55:
            // FX55: Stores from V0 to VX (including VX) in memory, starting at address I. The
            // offset from I is increased by 1 for each value written, but I is left unmodified.
            for (uint8_t i = 0; i <= vm->ins.x; i++) {
                vm->ram[vm->i + i] = vm->v[i];
            }
            break;

        case 0x65:
            // FX65: Fills from V0 to VX (including VX) with values from memory starting at addr I.
            // The offset from I is increased by 1 for each value read, but I is left unmodified.
            for (uint8_t i = 0; i <= vm->ins.x; i++) {
                vm->v[i] = vm->ram[vm->i + i];
            }
            break;

        default:
            break; // unimplemented
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
