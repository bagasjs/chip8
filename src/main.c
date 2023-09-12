#include <SDL2/SDL.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_render.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHIP8_DEFAULT_WINDOW_WIDTH 64
#define CHIP8_DEFAULT_WINDOW_HEIGHT 32

// SDL configurations
typedef struct {
#define CHIP8_DEFAULT_SCALE_FACTOR 10
    uint32_t window_width, window_height;
    SDL_Color fg_color, bg_color;
    uint32_t scale_factor;
    const char* rom_name;
    bool with_pixel_outlines;
} Config;

void set_config_from_args(Config* cfg, int argc, const char** argv)
{
    cfg->rom_name = argv[1];
    cfg->scale_factor = CHIP8_DEFAULT_SCALE_FACTOR;
    cfg->with_pixel_outlines = true;
    cfg->fg_color = (SDL_Color){ .r=0xff, .g=0xff, .b=0xff, .a=0xff };
    cfg->bg_color = (SDL_Color){ .r=0x00, .g=0x00, .b=0x00, .a=0xff };
    for(int i = 0; i < argc; ++i) {
        (void)argv[i];
    }
}

// SDL objects
typedef struct { 
    SDL_Window* w; 
    SDL_Renderer* r; 
    SDL_Color fg_color, bg_color;
} Sdl;

bool sdl_init(Sdl* sdl, Config conf)
{
    if(SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        fprintf(stderr, "Failed to initialize SDL\n %s\n", SDL_GetError());
        return false;
    }
    sdl->bg_color = conf.bg_color;
    sdl->fg_color = conf.fg_color;

    sdl->w = SDL_CreateWindow("CHIP-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
            CHIP8_DEFAULT_WINDOW_WIDTH*conf.scale_factor, 
            CHIP8_DEFAULT_WINDOW_HEIGHT*conf.scale_factor, SDL_WINDOW_SHOWN);

    if(sdl->w == NULL) {
        fprintf(stderr, "Failed to create window\n %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    sdl->r = SDL_CreateRenderer(sdl->w, -1, SDL_RENDERER_ACCELERATED);
    if(sdl->r == NULL) {
        fprintf(stderr, "Failed to create renderer\n %s\n", SDL_GetError());
        SDL_DestroyWindow(sdl->w);
        SDL_Quit();
        return false;
    }
    return true;
}

void sdl_deinit(Sdl sdl)
{
    SDL_DestroyRenderer(sdl.r);
    SDL_DestroyWindow(sdl.w);
    SDL_Quit();
}

void clear_screen(Sdl sdl)
{
    SDL_SetRenderDrawColor(sdl.r, 
            sdl.bg_color.r, 
            sdl.bg_color.g, 
            sdl.bg_color.b, 
            sdl.bg_color.a);
    SDL_RenderClear(sdl.r);
}

typedef enum {
    EMULATOR_QUIT = 0,
    EMULATOR_RUNNING,
    EMULATOR_PAUSED,
} Emulator_State;

#define CHIP8_RAM_CAPACITY 0x1000
#define CHIP8_STACK_B 0xEA0
#define CHIP8_STACK_E 0xEFF
#define CHIP8_STACK_SIZE (CHIP8_STACK_E - CHIP8_STACK_B)
#define CHIP8_ROM_B 0x200

typedef struct {
    Emulator_State state;
    uint8_t ram[CHIP8_RAM_CAPACITY];
    bool display[CHIP8_DEFAULT_WINDOW_WIDTH*CHIP8_DEFAULT_WINDOW_HEIGHT];
    uint16_t* stack;
    uint8_t V[16]; // registers
    uint16_t I; // index registers
    uint16_t PC; // Program Counter
    uint8_t delay_timer; // Decrements at 60hz when > 0
    uint8_t sound_timer; // Decrements at 60hz and plays tone when > 0
    bool keypad[16]; // 0x0 0xF
    const char* rom_name;
} Chip8;

bool chip8_init(Chip8* c, Config conf)
{
    memset(c->ram, 0, CHIP8_RAM_CAPACITY);

    // Load FONT
    const uint8_t fonts[] = {
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
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    memcpy(&c->ram[0], fonts, sizeof(fonts));

    // Load ROM
    FILE* f = fopen(conf.rom_name, "rb");
    if(f == NULL) {
        fprintf(stderr, "ROM file %s is invalid or not exist\n", conf.rom_name);
        return false;
    }
    fseek(f, 0L, SEEK_END);
    long rom_size = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if(rom_size > CHIP8_RAM_CAPACITY - CHIP8_ROM_B) {
        fprintf(stderr, "ROM file %s is too big %ld > %d\n", conf.rom_name, rom_size, CHIP8_RAM_CAPACITY);
        fclose(f);
        return false;
    }

    if(fread(&c->ram[CHIP8_ROM_B], rom_size, 1, f) != 1) {
        fprintf(stderr, "Could not read ROM file into memory\n");
        fclose(f);
        return false;
    }

    fclose(f);

    c->state = EMULATOR_RUNNING;
    c->PC = CHIP8_ROM_B;
    c->stack = (uint16_t*)&c->ram[CHIP8_STACK_B];
    return true;
}

void chip8_deinit(Chip8 c)
{
    (void)c;
}

void handle_input(Chip8* c)
{
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                c->state = EMULATOR_QUIT;
                return;
            case SDL_KEYDOWN:
                switch(ev.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        c->state = EMULATOR_QUIT;
                        break;
                    case SDLK_SPACE:
                        if(c->state == EMULATOR_RUNNING) {
                            c->state = EMULATOR_PAUSED;
                            printf("===== PAUSED =====\n");
                        } else {
                            c->state = EMULATOR_RUNNING;
                            printf("===== RUNNING =====\n");
                        }
                        break;
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

typedef struct {
    uint16_t opcode;
    uint8_t X; // the 2nd nibble of the opcode
    uint8_t Y; // the 3rd nibble of the opcode
    uint8_t N; // the 4th nibble of the opcode
    uint8_t NN; // Y and N combined or the second byte (8 bit)
    uint16_t NNN; // X, Y and N combined (12bit)
} Inst;

Inst chip8_fetch_next_instruction(Chip8* c)
{
    Inst inst = {0};
    inst.opcode = (c->ram[c->PC] << 8) | c->ram[c->PC+1];
    c->PC += 2;
    inst.NNN = inst.opcode & 0x0FFF;
    inst.NN = inst.opcode & 0x0FF;
    inst.N = inst.opcode & 0x0F;
    inst.X = (inst.opcode >> 8) & 0x0F;
    inst.Y = (inst.opcode >> 4) & 0x0F;
    return inst;
}

void update_screen(Sdl sdl, const Chip8* c, Config cfg)
{
    SDL_Rect r = (SDL_Rect){ .x = 0, .y = 0, .w = cfg.scale_factor, .h = cfg.scale_factor };

    for(uint32_t i = 0; i < sizeof(c->display); i++) {
        r.x = (i % CHIP8_DEFAULT_WINDOW_WIDTH) * cfg.scale_factor;
        r.y = (i / CHIP8_DEFAULT_WINDOW_WIDTH) * cfg.scale_factor;

        if(c->display[i]) {
            SDL_SetRenderDrawColor(sdl.r, 
                    sdl.fg_color.r,
                    sdl.fg_color.g,
                    sdl.fg_color.b,
                    sdl.fg_color.a);
            SDL_RenderFillRect(sdl.r, &r);

            if(cfg.with_pixel_outlines) {
                SDL_SetRenderDrawColor(sdl.r, 
                        sdl.bg_color.r,
                        sdl.bg_color.g,
                        sdl.bg_color.b,
                        sdl.bg_color.a);
                SDL_RenderDrawRect(sdl.r, &r);
            }
        } else {
            SDL_SetRenderDrawColor(sdl.r, 
                    sdl.bg_color.r,
                    sdl.bg_color.g,
                    sdl.bg_color.b,
                    sdl.bg_color.a);
            SDL_RenderFillRect(sdl.r, &r);
        }
    }
    SDL_RenderPresent(sdl.r);
}

#ifndef NDEBUG
void print_debug_info(Chip8* c, Inst inst)
{
    printf("[ADDR]: 0x%04X [OPCODE]: 0x%04X [EXEC]: ", c->PC - 2, inst.opcode);
    switch((inst.opcode >> 12) & 0x0F) {
        case 0x0:
            {
                if(inst.NN == 0xE0) {
                    printf("clear_screen;\n");
                } else if(inst.NN == 0xEE) {
                    printf("return %u; \n", *(c->stack - 1));
                } else {
                    printf("unimplemented instruction\n");
                }
            } break;
        case 0x1:
            {
                printf("jump to NNN(0x%2X)\n", inst.NNN);
            } break;
        case 0x2:
            {
                printf("jump to NNN(0x%2X) & push PC(0x%2X)\n", inst.NNN, c->PC);
            } break;
        case 0x6:
            {
                printf("set V%X(0x%02X), NN(0x%02X)\n",
                        inst.X, c->V[inst.X], inst.NN);
            } break;
        case 0x7:
            {
                printf("set V%X(0x%02X), += NN(0x%02X)\n",
                        inst.X, c->V[inst.X], inst.NN);
            } break;
        case 0xA:
            {
                printf("set I, NNN(0x%04X)\n", inst.NNN);
            } break;
        case 0xD:
            {
                printf("draw N(%u)-height at V%X(0x%02X), V%X(0x%02X) " 
                        "from I (0x%04X)\n", inst.N, inst.X, c->V[inst.X],
                        inst.Y, c->V[inst.Y], c->I);
            } break;
        default:
            {
                printf("unimplemented instruction\n");
            } break;
    }
}
#endif

void chip8_emulate_instruction(Chip8* c)
{
    Inst inst = chip8_fetch_next_instruction(c);

#ifndef NDEBUG
    print_debug_info(c, inst);
#endif

    switch((inst.opcode >> 12) & 0x0F) {
        case 0x0:
            {
                if(inst.NN == 0xE0) {
                    memset(c->display, false, sizeof(c->display));
                } else if(inst.NN == 0xEE) {
                    c->PC = *((uint16_t*)c->stack); // set pc to the top value of the stack
                    c->stack -= 1;
                }
            } break;
        case 0x1:
            {
                c->PC = inst.NNN;
            } break;
        case 0x2:
            {
                // 0x2NNN Call subroutine at NNN
                uint16_t* stack_ptr = (uint16_t*)c->stack;
                *stack_ptr = c->PC; // save current address to to return to on subroutine stack
                c->PC = inst.NNN; // set program counter to NNN
                c->stack += 1;
            } break;
        case 0x6:
            {
                c->V[inst.X] = inst.NN;
            } break;
        case 0x7:
            {
                c->V[inst.X] += inst.NN;
            } break;
        case 0xA:
            {
                c->I = inst.NNN;
            } break;
        case 0xD:
            {
                // 0xDXYN Draw N height sprite at coords X,Y; Read from memory
                // Screen pixels is XOR'd with sprite bits
                // location I. VF (Carry flag) is set if any screen pixels
                // are set off 
                uint8_t x_coord = c->V[inst.X] % CHIP8_DEFAULT_WINDOW_WIDTH;
                uint8_t y_coord = c->V[inst.Y] % CHIP8_DEFAULT_WINDOW_HEIGHT;
                const uint8_t x_orig = x_coord;

                c->V[0xF] = 0;

                for(uint8_t i = 0; i < inst.N; i++) {
                    const uint8_t sprite_data = c->ram[c->I + i];
                    x_coord = x_orig;

                    for(int8_t j = 7; j >= 0; j--) {
                        // If sprite pixel/bit is on and display pixel is on, set carry flag
                        uint16_t display_index = y_coord * CHIP8_DEFAULT_WINDOW_WIDTH + x_coord;
                        const bool sprite_bit = (sprite_data & (1 << j));

                        if(sprite_bit && c->display[display_index]) {
                            c->V[0xF] = 1;
                        }

                        c->display[display_index] ^= sprite_bit;

                        // stop drawing if it hit the edge of the screen;
                        if(++x_coord >= CHIP8_DEFAULT_WINDOW_WIDTH) 
                            break;
                    }

                    // stop drawing if it hit the bottom of the screen;
                    if(++y_coord >= CHIP8_DEFAULT_WINDOW_HEIGHT)
                        break;
                }
            } break;
        default:
            break;
    }
}

int main(int argc, const char** argv)
{
    if(argc < 2) {
        fprintf(stderr, "USAGE: %s <path to rom>\n", argv[0]);
        return 69;
    }
    Config conf;
    Sdl sdl;
    Chip8 chip8;

    set_config_from_args(&conf, argc, argv);

    if(!sdl_init(&sdl, conf)) {
        sdl_deinit(sdl);
        return 69;
    }

    if(!chip8_init(&chip8, conf)) {
        fprintf(stderr, "Failed to create CHIP-8 instance\n");
        sdl_deinit(sdl);
        return 69;
    }

    clear_screen(sdl);

    while(chip8.state != EMULATOR_QUIT) {
        // Get Time
        // Emulate CHIP-8 instructions;
        // Get Time elapsed since last get time
        handle_input(&chip8);
        if(chip8.state == EMULATOR_PAUSED)
            continue;

        chip8_emulate_instruction(&chip8);
        SDL_Delay(16);

        update_screen(sdl, &chip8, conf);
    }

    sdl_deinit(sdl);
    return 0;
}
