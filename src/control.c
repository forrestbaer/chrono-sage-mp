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
#define MAX_PRESETS 10

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

enum page_type page;

u8 selected_row;
u8 ec, do_error, do_blink_error, error_ref_row;
u8 current_tick;
u8 selected_preset;
u8 is_preset_saved;
u8 is_config_changed;
u32 speed, speed_mod, gate_length_mod;

u8 default_divisions[12] = {128,64,32,16,8,7,6,5,4,3,2,1};

static void step(void);
static void output_clock(void);
static void clock(void);
static void update_speed_from_knob(void);
static void update_speed(void);

static void toggle_config_page(void);

static void set_trigger_bits(u8 r, u8 d);
static u8 get_division(u8 pos);

static void save_preset(void);
static void save_preset_with_confirmation(void);
static void load_preset(u8 preset);

static void reset_all_logic(void);
static u8 get_total_logical_refs(void);
static u8 get_row_compared_from(u8 r);
static u8 is_logically_referenced(u8 r, u8 include_selected);
static u8 is_circularly_referenced(u8 r);

static u8 process_nested_change(u8 r, u8 just_checking);
static void set_bits_for_logic_update(u8 x, u8 y, u8 was_toggled_off);

static void rotate_clocks(void);
static void fire_gate(u8 r);
static void fire_error_alerts(void);
static void set_preset_leds(void);

static void process_grid_press(u8 x, u8 y, u8 on);
static void process_grid_held(u8 x, u8 y);
static u8 set_logic_led(u8 r, u8 t); 
static void set_glyph_leds(enum logic_depth l);

static u8 push(Node **stack, u8 val);
static u8 pop(Node **stack, u8 *val);



// ----------------------------------------------------------------------------
// functions for main.c

void init_presets(void) {
    // called by main.c if there are no presets saved to flash yet
    // initialize meta - some meta data to be associated with a preset, like a glyph
    // initialize shared (any data that should be shared by all presets) with default values
    // initialize preset with default values 
    // store them to flash

    store_shared_data_to_flash(&s);

    p.config.logic_depth = SINGLE;
    p.config.input_config = CLOCK;

    for (u8 i = 0; i < 12; i++) {
        p.config.clock_divs[i] = default_divisions[i];
    }
    
    for (u8 i = 0; i < GATE_OUTS; i++) {
        p.row[i].position = 15 - i;
        p.row[i].division = get_division(p.row[i].position);
        p.row[i].logic.type = NONE;
        p.row[i].logic.compared_row = 0;
        set_trigger_bits(i, p.row[i].division);
    }

    for (u8 i = 0; i < MAX_PRESETS; i++) {
        store_preset_to_flash(i, &m, &p);
    }

    store_preset_index(0);
}

void init_control(void) {
    // load shared data
    // load current preset and its meta data
    current_tick = MAX_STEPS - 1;
    selected_row = 0;
    page = MAIN;

    // load_shared_data_from_flash(&s);
    // load_preset_from_flash(0, &p);
    load_preset(get_preset_index());

    // set up any other initial values and timers
    add_timed_event(CLOCKTIMER, 100, 1);

    speed_mod = gate_length_mod = 0;
    update_speed_from_knob();

    add_timed_event(SPEEDTIMER, SPEEDCYCLE, 1);
}

void process_event(u8 event, u8 *data, u8 length) {
    switch (event) {
        case MAIN_CLOCK_RECEIVED:
            if (p.config.input_config == CLOCK && data[1]) {
                step();
            } else if (p.config.input_config == ROTATE && data[1]) {
                rotate_clocks();
            }
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
            process_grid_held(data[0], data[1]);
            break;
            
        case ARC_ENCODER_COARSE:
            break;
    
        case FRONT_BUTTON_PRESSED:
            if (!data[0]) toggle_config_page();
            break;
    
        case FRONT_BUTTON_HELD:
            break;
    
        case BUTTON_PRESSED:
            break;
    
        case I2C_RECEIVED:
            break;
            
        case TIMED_EVENT:
            if (data[0] == SPEEDTIMER) {
                update_speed_from_knob();
            } else if (data[0] == CLOCKTIMER) {
                if (!is_external_clock_connected() || p.config.input_config == ROTATE) step();
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

u8 get_division(u8 pos) {
    return p.config.clock_divs[pos - 4];
}

void save_preset_with_confirmation() {
    save_preset();
    refresh_grid();
}

void save_preset() {
    store_preset_to_flash(selected_preset, &m, &p);
    store_shared_data_to_flash(&s);
    store_preset_index(selected_preset);
    is_preset_saved = 1;
}

void load_preset(u8 preset) {
    selected_preset = preset;
    load_preset_from_flash(selected_preset, &p);

    refresh_grid();
}

void step() {
    output_clock();
    clock();
    fire_error_alerts();
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

void fire_error_alerts() {
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
    
    //speed = ((get_knob_value(0) * 1980) >> 16) + 40;
    speed = (get_knob_value(0) >> 6);
    update_speed();
}

void update_speed() {
    u32 sp = speed + speed_mod;
    if (sp > 1000) sp = 1000; else if (sp < 30) sp = 30;
    
    update_timer_interval(CLOCKTIMER, 60000 / sp);
}

void output_clock() {
    add_timed_event(CLOCKOUTTIMER, CLOCKOUTWIDTH, 0);
    set_clock_output(1);
}


void reset_all_logic() {
    for (u8 i = 0; i < GATE_OUTS; i++) {
        p.row[i].logic.compared_row = 0;
        p.row[i].logic.type = 0;
        p.row[i].position = 15 - i;
        p.row[i].division = get_division(p.row[i].position);
        set_trigger_bits(i, p.row[i].division);
    }
}

u8 get_total_logical_refs() {
    u8 total_logical_references = 0;
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[i].logic.compared_row > 0) total_logical_references++;
    }
    return total_logical_references;
}

u8 get_row_compared_from(u8 r) {
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[i].logic.compared_row - 1 == r) return i + 1;
    }
    return 0;
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

void set_bits_for_logic_update(u8 x, u8 y, u8 toggled_off) {
    p.row[selected_row].logic.type = toggled_off ? 0 : x;
    p.row[selected_row].logic.compared_row = toggled_off ? 0 : y + 1;
    p.row[selected_row].division = get_division(p.row[selected_row].position);

    // reverse linked list for setting bits ?
    set_trigger_bits(selected_row, p.row[selected_row].division);

    refresh_grid();
}

u8 process_nested_change(u8 r, u8 just_checking) {
    Node *stack = NULL;
    u8 s;
    u8 is_linked = 0;

    push(&stack, r);
    u8 r_comp1 = p.row[r].logic.compared_row;
    while (r_comp1 > 0) {
        if (just_checking) {
            if (r_comp1 - 1 == r) {
                is_linked = 1;
                break;
            }
        }
        push(&stack, r_comp1 - 1);
        r_comp1 = p.row[r_comp1 - 1].logic.compared_row;
    }

    // set triggers on stack items LIFO
    while (pop(&stack, &s)) {
        if (just_checking) {
            // unload memory
        } else {
            p.row[s].division = get_division(p.row[s].position);
            set_trigger_bits(s, p.row[s].division);
        }
    }

    u8 r_comp2 = get_row_compared_from(r);
    while (r_comp2 > 0) {
        if (just_checking) {
            if (r_comp2 - 1 == r) {
                is_linked = 1;
                break;
            }
        } else {
            p.row[r_comp2 - 1].division = get_division(p.row[r_comp2 - 1].position);
            set_trigger_bits(r_comp2 - 1, p.row[r_comp2 - 1].division);
        }
        r_comp2 = get_row_compared_from(r_comp2 - 1);
    }

    if (just_checking) {
        return is_linked;
    } else {
        return 0;
        refresh_grid();
    }
}

void toggle_config_page() {
    page = page == CONFIG ? MAIN : CONFIG;
}

void process_grid_press(u8 x, u8 y, u8 on) {

    if (page == CONFIG) {
        // config page

        // param load happening
        if (page == CONFIG && x > 2 && x < 13 && y == 0 && !on) {
            selected_preset = x - 3;
            load_preset(selected_preset);
        }

        // setting of logic_depth
        if ((x > 2 && x < 7) && (y > 1 && y < 6) && p.config.logic_depth == NESTED) {
            if (!on) return;
            p.config.logic_depth = SINGLE; 
            reset_all_logic();
        } else if ((x > 8 && x < 13) && (y > 1 && y < 6) && p.config.logic_depth == SINGLE) {
            if (!on) return;
            p.config.logic_depth = NESTED;
            reset_all_logic();
        }

        if ((x == 14 || x == 15) && y == 0 && !on) {
            p.config.input_config = x == 14 ? CLOCK : ROTATE;
        }

        refresh_grid();

    } else if (page == MAIN) {
 
        if (!on) return;

        // main page

        // select a row
        if (x == 0) {
            selected_row = y;
        }

        // logic buttons
        if (x > 0 && x < 4) {

            u8 was_toggled_off = p.row[selected_row].logic.type == x && p.row[selected_row].logic.compared_row - 1 == y;

            if (p.config.logic_depth == SINGLE) {
                // rules for SINGLE logic depth

                // don't allow for channel A->B & B->A (circular logic)
                // don't allow selection of logic on selected row (self referencing)
                if (is_circularly_referenced(y)) {
                    do_error = 1;
                    error_ref_row = y + 1;
                } else if (selected_row == y) {
                    do_error = 1;
                    error_ref_row = selected_row + 1;
                } else {
                    set_bits_for_logic_update(x, y, was_toggled_off);
                }
            } else if (p.config.logic_depth == NESTED) {
                // rules setup for NESTED logic depth

                // don't allow selection of logic on selected row (self referencing)
                // don't allow a channel to be selected for logic if it's already selected for logic (logic loops)
                // don't allow for channel A->B & B->A (circular logic)
                // don't allow all channels to have logic, otherwise a loop must exist
                if (is_logically_referenced(y, 0)) {
                    do_error = 1;
                    error_ref_row = get_row_compared_from(y);
                } else if (is_circularly_referenced(y)) {
                    do_error = 1;
                    error_ref_row = y + 1;
                } else if (process_nested_change(y, 1) || selected_row == y || (get_total_logical_refs() > GATE_OUTS - 2 && was_toggled_off == 0 && p.row[selected_row].logic.compared_row - 1 != y)) {
                    do_error = 1;
                    error_ref_row = selected_row + 1;
                } else {
                    // nested logic loop
                    u8 last_compared_row = p.row[selected_row].logic.compared_row;
                    u8 last_logic_type = p.row[selected_row].logic.type;

                    p.row[selected_row].logic.type = was_toggled_off ? 0 : x;
                    p.row[selected_row].logic.compared_row = was_toggled_off ? 0 : y + 1;
                    p.row[selected_row].division = get_division(p.row[selected_row].position);

                    if (process_nested_change(selected_row, 1) == 0) {
                        process_nested_change(selected_row, 0);
                    } else {
                        p.row[selected_row].logic.type = last_logic_type;
                        p.row[selected_row].logic.compared_row = last_compared_row;

                        do_error = 1;
                        error_ref_row = selected_row + 1;
                    }
                }
            }
        }

        // division buttons
        if (x > 3 && x < 16) {
            p.row[y].position = x;
            p.row[y].division = get_division(p.row[y].position);
            set_trigger_bits(y, p.row[y].division);

            // update each row that's compared against this row (SINGLE mode)
            if (p.config.logic_depth == SINGLE) {
                for (u8 i = 0; i < GATE_OUTS; i++) {
                    if (p.row[i].logic.compared_row - 1 == y) {
                        set_trigger_bits(i, p.row[i].division);
                    }
                }
            } else if (p.config.logic_depth == NESTED) {
                // nested logic loop
                process_nested_change(y, 0);
            }
        }
    }
}

void process_grid_held(u8 x, u8 y) {
    if (x == 0 && y == 0) {
        toggle_config_page();
    }

    // param save happening
    if (page == CONFIG && x > 2 && x < 13 && y == 0) {
        selected_preset = x - 3;
        save_preset_with_confirmation();
    }
}


//
// rendering functions 
//

void set_preset_leds() {
    for (u8 x = 3; x < 13; x++) {
        set_grid_led(x, 0, 6);
    }

    set_grid_led(14, 0, p.config.input_config == 0 ? B_FULL + 4 : B_HALF);
    set_grid_led(15, 0, p.config.input_config == 1 ? B_FULL + 4 : B_HALF);

    set_grid_led((selected_preset % 10) + 3, 0, 14);
}

u8 set_logic_led(u8 r, u8 t) {
    if (p.row[selected_row].logic.type == t && p.row[selected_row].logic.compared_row - 1 == r) {
        return B_FULL + 3;
    } else {
        if (p.config.logic_depth == SINGLE) {
            if (is_circularly_referenced(r) || selected_row == r) {
               return B_DIM; 
            } else {
                return B_DIM + 3;
            }
        } else if (p.config.logic_depth == NESTED) {
            if (is_logically_referenced(r, 0) || is_circularly_referenced(r) || selected_row == r || (get_total_logical_refs() > GATE_OUTS - 2 && p.row[selected_row].logic.compared_row - 1 != r)) {
               return B_DIM; 
            } else {
                return B_DIM + 3;
            }
        }
    }
    return 0;
}

void set_glyph_leds(enum logic_depth l) {
    u8 bs = 6;
    u8 bn = 6;

    if (l == SINGLE) {
        bs = 12;
    } else if (l == NESTED) {
        bn = 12;
    }

    // SINGLE
    // col 1
    set_grid_led(3, 2, bs);
    set_grid_led(3, 3, 2);
    set_grid_led(3, 4, 2);
    set_grid_led(3, 5, bs);
    // col 2
    set_grid_led(4, 2, 2);
    set_grid_led(4, 3, bs);
    set_grid_led(4, 4, bs);
    set_grid_led(4, 5, 2);
    // col 3
    set_grid_led(5, 2, 2);
    set_grid_led(5, 3, bs);
    set_grid_led(5, 4, bs);
    set_grid_led(5, 5, 2);
    // col 4
    set_grid_led(6, 2, bs);
    set_grid_led(6, 3, 2);
    set_grid_led(6, 4, 2);
    set_grid_led(6, 5, bs);

    // NESTED
    // row 1
    set_grid_led(9, 2, bn);
    set_grid_led(10, 2, 2);
    set_grid_led(11, 2, 2);
    set_grid_led(12, 2, bn);
    // row 2
    set_grid_led(9, 3, bn);
    set_grid_led(10, 3, bn);
    set_grid_led(11, 3, 2);
    set_grid_led(12, 3, bn);
    // row 3
    set_grid_led(9, 4, bn);
    set_grid_led(10, 4, 2);
    set_grid_led(11, 4, bn);
    set_grid_led(12, 4, bn);
    // row 4
    set_grid_led(9, 5, bn);
    set_grid_led(10, 5, 2);
    set_grid_led(11, 5, 2);
    set_grid_led(12, 5, bn);
}

void render_grid(void) {
    if (!is_grid_connected()) return;

    if (page == CONFIG) {
        clear_all_grid_leds();
        set_preset_leds();
        set_grid_led(0, 0, 6);
        set_glyph_leds(p.config.logic_depth);
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
            set_grid_led(0, error_ref_row > 0 ? error_ref_row - 1 : selected_row, B_DIM);
        } else {
            set_grid_led(0, error_ref_row > 0 ? error_ref_row - 1 : selected_row, 14);
        }
    }
}

void render_arc(void) {
    // no arc support for now, could be added!
}


//
// helper functions
//

u8 push(Node **stack, u8 val) {
    Node *p = malloc(sizeof(Node));
    u8 success = p != NULL;

    if (success) {
        p->v = val;
        p->next = *stack;
        *stack = p;
    }

    return success;
}

u8 pop(Node **stack, u8 *val) {
    u8 success = *stack != NULL;

    if (success) {
        Node *p = *stack;
        *stack = (*stack)->next;
        *val = p->v;
        free(p);
    }       

    return success;
}