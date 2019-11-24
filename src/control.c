// -----------------------------------------------------------------------------
// controller - the glue between the engine and the hardware
//
// reacts to events (grid press, clock etc) and translates them into appropriate
// engine actions. reacts to engine updates and translates them into user 
// interface and hardware updates (grid LEDs, CV outputs etc)
//
// should talk to hardware via what's defined in interface.h only
// should talk to the engine via what's defined in engine.h only
// ----------------------------------------------------------------------------

#include "compiler.h"
#include "string.h"

#include "control.h"
#include "interface.h"
#include "engine.h"

#define SPEEDCYCLE 4
#define CLOCKOUTWIDTH 25
#define MAX_DIVISIONS 128

#define B_FULL 11
#define B_HALF 6
#define B_DIM 2

#define SPEEDTIMER 0
#define CLOCKTIMER 1
#define CLOCKOUTTIMER 2
#define GATETIMER 3

preset_data_t p;
preset_meta_t m;
shared_data_t s;

u8 selected_row;
u8 current_tick;
u8 is_preset_saved;
u32 speed, speed_mod, gate_length_mod;

u8 clock_divs[12] = {128,64,32,16,8,7,6,5,4,3,2,1};

static void save_preset(void);
static void save_preset_with_confirmation(void);

static void update_speed_from_knob(void);
static void update_speed(void);

void fire_gate(u8 row);

static void process_grid_press(u8 x, u8 y, u8 on);

static void step(void);
static void output_clock(void);
static void clock(void);

// ----------------------------------------------------------------------------
// firmware dependent stuff starts here


// ----------------------------------------------------------------------------
// functions for main.c

void init_presets(void) {
    // called by main.c if there are no presets saved to flash yet
    // initialize meta - some meta data to be associated with a preset, like a glyph
    // initialize shared (any data that should be shared by all presets) with default values
    // initialize preset with default values 
    // store them to flash
    
    for (u8 i = 0; i < 8; i++) {
        p.row[i].position = 15 - i;
        p.row[i].logic.type = NONE;
    }

    store_preset_to_flash(0, &m, &p);
    store_shared_data_to_flash(&s);
    store_preset_index(0);
}

void init_control(void) {
    // load shared data
    // load current preset and its meta data
    current_tick = 0;
    selected_row = 0;

    // load_shared_data_from_flash(&s);
    load_preset_from_flash(0, &p);
    load_preset_meta_from_flash(0, &m);

    // set up any other initial values and timers
    add_timed_event(CLOCKTIMER, 100, 1);

    speed_mod = gate_length_mod = 0;
    update_speed_from_knob();

    add_timed_event(SPEEDTIMER, SPEEDCYCLE, 1);
}

void process_event(u8 event, u8 *data, u8 length) {
    switch (event) {
        case MAIN_CLOCK_RECEIVED:
            step();
            break;
        
        case MAIN_CLOCK_SWITCHED:
            break;
    
        case GATE_RECEIVED:
            break;
        
        case GRID_CONNECTED:
            break;
        
        case GRID_KEY_PRESSED:
            process_grid_press(data[0], data[1], data[2]);
            break;
    
        case GRID_KEY_HELD:
            break;
            
        case ARC_ENCODER_COARSE:
            break;
    
        case FRONT_BUTTON_PRESSED:
            current_tick = 1;
            break;
    
        case FRONT_BUTTON_HELD:
            save_preset_with_confirmation();
            break;
    
        case BUTTON_PRESSED:
            break;
    
        case I2C_RECEIVED:
            break;
            
        case TIMED_EVENT:
            if (data[0] == SPEEDTIMER) {
                update_speed_from_knob();
            } else if (data[0] == CLOCKTIMER) {
                if (!is_external_clock_connected()) step();
            } else if (data[0] == CLOCKOUTTIMER) {
                set_clock_output(0);
            } else if (data[0] == GATETIMER) {
                for (u8 i = 0; i < 8; i++) set_gate(i, 0);
            }
            break;
        
        case MIDI_CONNECTED:
            break;
        
        case MIDI_NOTE:
            break;
        
        case MIDI_CC:
            break;
            
        case MIDI_AFTERTOUCH:
            break;
            
        case SHNTH_BAR:
            break;
            
        case SHNTH_ANTENNA:
            break;
            
        case SHNTH_BUTTON:
            break;
            
        default:
            break;
    }
}

// -----------------
// actions

void save_preset_with_confirmation() {
    is_preset_saved = 1;
    save_preset();
}

void save_preset() {
    store_preset_to_flash(0, &m, &p);
    store_shared_data_to_flash(&s);
    store_preset_index(0);

    refresh_grid();
}

void step() {
    clock();
    output_clock();
    refresh_grid();
}

void clock() {
        current_tick = (current_tick + 1) % MAX_DIVISIONS;
        for (u8 i = 0; i < 8; i++) {
            switch (p.row[i].logic.type) {
                case 0: // NONE
                    if (current_tick % clock_divs[p.row[i].position - 4] == 0) fire_gate(i);
                    break;
                case 1: // AND
                    if (current_tick % clock_divs[p.row[i].position - 4] == 0 && current_tick % p.row[i].logic.value == 0) fire_gate(i);
                    break;
                case 2: // OR
                    if (current_tick % clock_divs[p.row[i].position - 4] == 0 || current_tick % p.row[i].logic.value == 0) fire_gate(i);
                    break;
                case 3: // NOR
                    if (current_tick % clock_divs[p.row[i].position - 4] != 0 && current_tick % p.row[i].logic.value != 0) fire_gate(i);
                    break;
            }
        }

    add_timed_event(GATETIMER, 25, 0);
}

void fire_gate(u8 row) {
    set_gate(row, 1);
    p.row[row].blink = 1;
}

void update_speed_from_knob() {
    if (get_knob_count() == 0) return;
    
    speed = ((get_knob_value(0) * 1980) >> 16) + 20;
    update_speed();
}

void update_speed() {
    u32 sp = speed + speed_mod;
    if (sp > 2000) sp = 2000; else if (sp < 20) sp = 20;
    
    update_timer_interval(CLOCKTIMER, 60000 / sp);
    // to be added, grid stuff
    // update_display();
}

void output_clock() {
    add_timed_event(CLOCKOUTTIMER, CLOCKOUTWIDTH, 0);
    set_clock_output(1);
}

void process_grid_press(u8 x, u8 y, u8 on) {

    if (!on) return;
    //
    // select a row
    //
    if (x == 0) {
        selected_row = y;
    }

    //
    // division buttons
    //
    if (x > 3 && x < 16) {
        p.row[y].position = x;
    }

    //
    // logic buttons
    //
    if ((x > 0 && x < 4) && selected_row != y) {
        p.row[selected_row].logic.type = p.row[selected_row].logic.type == x && p.row[selected_row].logic.compared_row == y ? 0 : x;
        p.row[selected_row].logic.compared_row = y;
        p.row[selected_row].logic.value = clock_divs[p.row[y].position - 4];
    }
}

void render_grid(void) {
    if (!is_grid_connected()) return;

    if (is_preset_saved) {
        // show that preset was saved with some flashing
        for (u8 i = 0; i < 8; i++) {
            set_grid_led(p.row[i].position, i, B_DIM);
        }
        is_preset_saved = 0;
    } else {
        clear_all_grid_leds();

        for (u8 i = 0; i < 8; i++) {
            set_grid_led(0, selected_row, B_HALF);
            set_grid_led(1, i, p.row[selected_row].logic.type == 1 && p.row[selected_row].logic.compared_row == i ? B_HALF : B_DIM);
            set_grid_led(2, i, p.row[selected_row].logic.type == 2 && p.row[selected_row].logic.compared_row == i ? B_HALF : B_DIM);
            set_grid_led(3, i, p.row[selected_row].logic.type == 3 && p.row[selected_row].logic.compared_row == i ? B_HALF : B_DIM);
            set_grid_led(p.row[i].position, i, p.row[i].blink ? 10 : B_HALF);
            p.row[i].blink = 0;
        }
    }
}

void render_arc(void) {
    // render arc LEDs here or leave blank if not used
}