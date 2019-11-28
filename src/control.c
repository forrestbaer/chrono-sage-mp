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

#define GATE_OUTS 8

#define SPEEDCYCLE 4
#define CLOCKOUTWIDTH 10

#define B_FULL 9 
#define B_HALF 6
#define B_DIM 3

#define SPEEDTIMER 0
#define CLOCKTIMER 1
#define CLOCKOUTTIMER 2
#define GATETIMER 3

preset_data_t p;
preset_meta_t m;
shared_data_t s;

u8 selected_row;
u8 ec, do_error, do_blink_error, error_ref_row;
u8 current_tick;
u8 is_preset_saved;
u32 speed, speed_mod, gate_length_mod;

u8 clock_divs[12] = {128,64,32,16,8,7,6,5,4,3,2,1};

static void set_trigger_bits(u8 r, u8 d);
static u8 get_division(u8 p);

static void save_preset(void);
static void save_preset_with_confirmation(void);

static void update_speed_from_knob(void);
static void update_speed(void);

static u8 get_total_logical_refs(void);
static u8 get_row_compared_from(u8 r);
static u8 is_logically_referenced(u8 r, u8 include_selected);
static u8 is_circularly_referenced(u8 r);

static void rotate_clocks(void);
static void fire_gate(u8 r);
static void error_alerts(void);

static void process_grid_press(u8 x, u8 y, u8 on);
static u8 set_logic_led(u8 r, u8 t); 
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
    
    for (u8 i = 0; i < GATE_OUTS; i++) {
        p.row[i].position = 15 - i;
        p.row[i].division = get_division(p.row[i].position);
        set_trigger_bits(i, p.row[i].division);
        p.row[i].logic.type = 0;
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
            if (data[1] == 1) rotate_clocks();
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
            current_tick = 0;
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
                step();
            } else if (data[0] == CLOCKOUTTIMER) {
                set_clock_output(0);
            } else if (data[0] >= GATETIMER) {
                set_gate(data[0] - GATETIMER, 0);
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

void set_trigger_bits(u8 r, u8 d) {
    for (int i = 0; i < MAX_STEPS; i++) {
        p.row[r].triggers[i] = (i + 1) % d == 0 ? 1 : 0;
    }
    if (p.row[r].logic.type > 0) {
        switch(p.row[r].logic.type) {
            case 0:
            break;
            case 1: // AND
                for (int i = 0; i < MAX_STEPS; i++) {
                    p.row[r].triggers[i] = p.row[r].triggers[i] && p.row[p.row[r].logic.compared_row - 1].triggers[i] ? 1 : 0;
                }
            break;
            case 2: // OR
                for (int i = 0; i < MAX_STEPS; i++) {
                    p.row[r].triggers[i] = p.row[r].triggers[i] || p.row[p.row[r].logic.compared_row - 1].triggers[i] ? 1 : 0;
                }
            break;
            case 3: // XOR
                for (int i = 0; i < MAX_STEPS; i++) {
                    p.row[r].triggers[i] = p.row[r].triggers[i] != p.row[p.row[r].logic.compared_row - 1].triggers[i] ? 1 : 0;
                }
            break;
        }
    }
}

u8 get_division(u8 p) {
    return clock_divs[p - 4];
}

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
    output_clock();
    clock();
    error_alerts();
    refresh_grid();
}

void clock() {
    current_tick = (current_tick + 1) % MAX_STEPS;
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[i].triggers[current_tick]) fire_gate(i);
    }
}

void rotate_clocks() {
    u8 next_position = 0;
    u8 first_pos = p.row[0].position;
    u8 first_div = get_division(first_pos);

    for (u8 i = 0; i < GATE_OUTS; i++) {
        next_position = (next_position + 1) % GATE_OUTS;
        if (next_position == 0) {
            p.row[7].position = first_pos;
            p.row[7].division = first_div;
            set_trigger_bits(7, first_div);
        } else {
            p.row[i].position = p.row[next_position].position;
            p.row[i].division = get_division(p.row[i].position);
            set_trigger_bits(i, p.row[i].division);
        }
    }
}

void fire_gate(u8 r) {
    // can set dynamic gate length here
    add_timed_event(GATETIMER + r, 10, 0);
    set_gate(r, 1);
    p.row[r].blink = 1;
}

void error_alerts() {
    if(do_error) {
        ec++;
        if (ec == 5) {
            ec = 0;
            do_error = 0;
            do_blink_error = 0;
            error_ref_row = 0;
        } else {
            do_blink_error = ec % 2 == 0 ? 0 : 1;
        }
    }
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
}

void output_clock() {
    add_timed_event(CLOCKOUTTIMER, CLOCKOUTWIDTH, 0);
    set_clock_output(1);
}

u8 get_total_logical_refs() {
    u8 total_logical_references = 0;
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[i].logic.compared_row > 0) total_logical_references++;
    }
    return total_logical_references;
};

u8 get_row_compared_from(u8 r) {
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[i].logic.compared_row - 1 == r) return i + 1;
    }
}

u8 is_logically_referenced(u8 r, u8 include_selected) {
    // don't allow a channel to be selected for logic if it's already selected for logic (logic loops)
    u8 is_referenced = 0;
    
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (include_selected) {
            if (p.row[i].logic.compared_row - 1 == r) is_referenced = 1;
        } else {
            if (i != selected_row) {
                if (p.row[i].logic.compared_row - 1 == r) is_referenced = 1;
            }
        }
    }
    
    return is_referenced;
}

u8 is_circularly_referenced(u8 r) {
    // don't allow selection of logic on selected row (self referencing)
    return p.row[r].logic.compared_row > 0 && p.row[r].logic.compared_row - 1 == selected_row;
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
        p.row[y].division = get_division(p.row[y].position);

        // reverse linked list for setting bits ?
        set_trigger_bits(y, p.row[y].division);

        u8 cr = get_row_compared_from(y);
        if (cr > 0) {
            cr--;
            p.row[cr].division = get_division(p.row[cr].position);
            set_trigger_bits(cr, p.row[cr].division);
        }
    }

    //
    // logic buttons
    // sets logic type and comparison row
    //
    if (x > 0 && x < 4) {

        u8 toggled_logic = p.row[selected_row].logic.type == x && p.row[selected_row].logic.compared_row - 1 == y;
        
        // don't allow selection of logic on selected row (self referencing)
        // don't allow a channel to be selected for logic if it's already selected for logic (logic loops)
        // don't allow all channels to have logic, otherwise a loop must exist
        if (is_logically_referenced(y, 0)) {
            do_error = 1;
            error_ref_row = get_row_compared_from(y);
        } else if (is_circularly_referenced(y)) {
            do_error = 1;
            error_ref_row = y + 1;
        } else if (selected_row == y || (get_total_logical_refs() > GATE_OUTS - 2 && toggled_logic == 0 && p.row[selected_row].logic.compared_row - 1 != y)) {
            do_error = 1;
            error_ref_row = selected_row + 1;
        } else {
            p.row[selected_row].logic.type = toggled_logic ? 0 : x;
            p.row[selected_row].logic.compared_row = toggled_logic ? 0 : y + 1;
            p.row[selected_row].division = get_division(p.row[selected_row].position);

            // reverse linked list for setting bits ?
            set_trigger_bits(selected_row, p.row[selected_row].division);

            refresh_grid();
        }
    }
}

u8 set_logic_led(u8 r, u8 t) {
    if (p.row[selected_row].logic.type == t && p.row[selected_row].logic.compared_row - 1 == r) {
        return B_FULL + 3;
    } else {
        if (is_logically_referenced(r, 0) || is_circularly_referenced(r) || selected_row == r || (get_total_logical_refs() > GATE_OUTS - 2 && p.row[selected_row].logic.compared_row - 1 != r)) {
           return B_DIM; 
        } else {
            return B_DIM + 3;
        }
    }
}


void render_grid(void) {
    if (!is_grid_connected()) return;

     // show that preset was saved with some flashing
    if (is_preset_saved) {
        for (u8 i = 0; i < GATE_OUTS; i++) {
            set_grid_led(p.row[i].position, i, B_DIM);
        }
        is_preset_saved = 0;
    } else {
        clear_all_grid_leds();

        for (u8 i = 0; i < GATE_OUTS; i++) {
            set_grid_led(0, i, p.row[i].logic.compared_row > 0 ? B_DIM : 0);
            set_grid_led(1, i, set_logic_led(i, 1));
            set_grid_led(2, i, set_logic_led(i, 2));
            set_grid_led(3, i, set_logic_led(i, 3));
            set_grid_led(p.row[i].position, i, p.row[i].blink ? B_FULL + 3 : B_HALF);
            p.row[i].blink = 0;
        }

        if (do_blink_error == 1) {
            set_grid_led(0, error_ref_row > 0 ? error_ref_row - 1 : selected_row, B_FULL + 3);
        } else {
            set_grid_led(0, error_ref_row > 0 ? error_ref_row - 1 : selected_row, B_HALF);
        }
    }
}

void render_arc(void) {
    // no arc support for now, could be added!
}