// -----------------------------------------------------------------------------
// controller - the glue between the engine and the hardware
//
// reacts to events (grid press, clock etc) and translates them u8o appropriate
// engine actions. reacts to engine updates and translates them u8o user 
// u8erface and hardware updates (grid LEDs, CV outputs etc)
//
// should talk to hardware via what's defined in u8erface.h only
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

#define MAX_SPEED 1000
#define MIN_SPEED 30
#define SINGLE_DIVISION_SPEED 62

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
u8 tickers[8];
u8 step_ticker;
u16 knob_position, delta;
u8 selected_preset;
u32 speed;

u8 logical_divisions[12] = {128, 64, 32, 16, 8, 7, 6, 5, 4, 3, 2, 1};

static void step(void);
static void output_clock(void);
static void clock(void);
static void update_speed_from_knob(void);
static void update_speed(void);

static void toggle_config_page(void);
static void initialize_defaults(enum mode m);

static u8 get_division(u8 pos);

static void save_preset(void);
static void save_preset_with_confirmation(void);
static void load_preset(u8 preset);

static void rotate_clocks(void);
static void update_ticker(int r);
static void fire_gate(u8 r, enum gate_lengths gl);
static void fire_error_alerts(void);
static void set_preset_leds(void);

static void process_grid_press(u8 x, u8 y, u8 on);
static void process_grid_held(u8 x, u8 y);
static u8 set_logic_led(u8 r, u8 t); 
static void set_glyph_leds(enum mode l);

static u8 t_logic(u8 r, u8 index);
static u8 t_step(u8 r, u8 index);
static u8 is_circularly_referenced(u8 r);


// ----------------------------------------------------------------------------
// functions for main.c

void initialize_defaults(enum mode m) {
    if (m == LOGICAL) {
        for (u8 i = 0; i < 12; i++) {
            p.config.clock_divs[i] = logical_divisions[i];
        }
    
        for (u8 i = 0; i < GATE_OUTS; i++) {
            p.row[i].position = 15 - i;
            p.row[i].division = get_division(p.row[i].position);
            p.row[i].logic.type = NONE;
            p.row[i].logic.compared_to_row = 0;
            p.row[i].pattern_length = p.row[i].division;
            tickers[i] = p.row[i].pattern_length;
        }
    }
    if (m == STEP) {
        for (u8 y = 0; y < GATE_OUTS; y++) {
            for (u8 x = 0; x < 16; x++) {
                p.row[y].step.pulse[x] = 0;
                p.row[y].step.gl[x] = OFF;
            }
        }
    
        for (u8 i = 0; i < GATE_OUTS; i++) {
            p.row[i].pattern_length = 16;
            tickers[i] = 15;
        }
    }
}

void init_presets(void) {
    // called by main.c if there are no presets saved to flash yet
    // initialize meta - some meta data to be associated with a preset, like a glyph
    // initialize shared (any data that should be shared by all presets) with default values
    // initialize preset with default values 
    // store them to flash

    store_shared_data_to_flash(&s);

    p.config.mode = LOGICAL;
    p.config.input_config = CLOCK;

    initialize_defaults(p.config.mode);

    for (u8 i = 0; i < MAX_PRESETS; i++) {
        store_preset_to_flash(i, &m, &p);
    }

    store_preset_index(0);
}

void init_control(void) {
    // load shared data
    // load current preset and its meta data
    for (u8 i = 0; i << 8; i++) {
        tickers[i] = 0;
    }
    step_ticker = 0;
    selected_row = 0;
    page = MAIN;

    // load_shared_data_from_flash(&s);
    // load_preset_from_flash(0, &p);
    load_preset(get_preset_index());

    // set up any other initial values and timers
    add_timed_event(CLOCKTIMER, 100, 1);

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
    for (u8 i = 0; i < GATE_OUTS; i++) {
        tickers[i] = (tickers[i] + 1) % p.row[i].pattern_length;

        if (p.config.mode == LOGICAL && t_logic(i, tickers[i])) {
            fire_gate(i, SHORT);
        } else if (p.config.mode == STEP && t_step(i, tickers[i])) {
            fire_gate(i, p.row[i].step.gl[tickers[i]]);
        }
    }

    step_ticker = (step_ticker + 1) % 16;
}

void rotate_clocks() {
    // TODO fix this to work with STEP
    u8 next_position = 0;
    u8 first_pos = p.row[0].position;
    u8 first_div = get_division(first_pos);
    u8 first_length = first_div;

    for (u8 i = 0; i < GATE_OUTS; i++) {
        next_position = (next_position + 1) % GATE_OUTS;
        if (next_position == 0) {
            p.row[7].position = first_pos;
            p.row[7].division = first_div;
            p.row[7].pattern_length = first_length;
        } else {
            p.row[i].position = p.row[next_position].position;
            p.row[i].division = get_division(p.row[i].position);
            p.row[i].pattern_length = p.row[i].division;
        }
    }
}

void fire_gate(u8 r, enum gate_lengths gl) {
    add_timed_event(GATETIMER + r, gl, 0);
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

    // this stuff doesn't work :(
    if (get_knob_value(0) != knob_position) {
        delta = knob_position > get_knob_value(0) ? knob_position - get_knob_value(0) : get_knob_value(0) - knob_position;
    }

    // speed = ((get_knob_value(0) * 1980) >> 16) + 40;
    // slightly more sane value for meadowphysics
    if (delta > 0) {
        speed = (get_knob_value(0) >> 6);
        delta = 0;
        knob_position = get_knob_value(0);
        update_speed();
    }
}

void update_speed() {
    u32 sp = speed;
    if (sp > MAX_SPEED) sp = MAX_SPEED; else if (sp < MIN_SPEED) sp = MIN_SPEED;
    
    update_timer_interval(CLOCKTIMER, 60000 / sp);
}

void output_clock() {
    add_timed_event(CLOCKOUTTIMER, CLOCKOUTWIDTH, 0);
    set_clock_output(1);
}

u8 is_circularly_referenced(u8 r) {
    // don't allow selection of logic on selected row (self referencing)
    return p.row[r].logic.compared_to_row > 0 && p.row[r].logic.compared_to_row - 1 == selected_row;
}

void toggle_config_page() {
    page = page == CONFIG ? MAIN : CONFIG;
}

u8 t_step(u8 r, u8 index) {
    return p.row[r].step.pulse[index] == 1 ? 1 : 0;
}

u8 t_logic(u8 r, u8 index) {
    u8 row_div = p.row[r].division;
    u8 target_div = p.row[r].logic.compared_to_row > 0 ? p.row[p.row[r].logic.compared_to_row - 1].division : 0;

    u8 len = p.row[r].logic.type > 0 ? row_div * target_div : row_div;
    u8* row_array = malloc(len * sizeof(u8));
    u8* target_array = malloc(len * sizeof(u8));

    u8 match;

    for (u8 i = 0; i <= len; i++) {
        row_array[i] = (i + 1) % row_div == 0 ? 1 : 0;
        target_array[i] = (i + 1) % target_div == 0 ? 1 : 0;
    }

    switch (p.row[r].logic.type) {
        case 0:
            match = row_array[index] == 1 ? 1 : 0;
            break;
        case 1: // AND
            match = row_array[index] == 1 && target_array[index] == 1 ? 1 : 0;
            break;
        case 2: // OR
            match = row_array[index] == 1 || target_array[index] == 1 ? 1 : 0;
            break;
        case 3: // XOR
            match = row_array[index] != target_array[index] ? 1 : 0;
            break;
    }

    free(row_array);
    free(target_array);

    return match;
}

void update_ticker(int r) {
    u8 closest_row = 0;
    u8 closest_amount = p.row[r].pattern_length;

    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[r].pattern_length == p.row[i].pattern_length && r != i) {
            tickers[r] = tickers[i];
            return;
        }
    }
    for (u8 i = 0; i < GATE_OUTS; i++) {
        if (p.row[r].pattern_length < p.row[i].pattern_length && r != i) {
            u8 new_offset = p.row[i].pattern_length - p.row[r].pattern_length;
            if (new_offset < closest_amount) {
                closest_amount = new_offset;
                closest_row = i;
            }
        }
    }
    tickers[r] = tickers[closest_row];
}

void process_grid_press(u8 x, u8 y, u8 on) {

    if (page == CONFIG) {
        // param load happening
        if (page == CONFIG && x > 2 && x < 13 && y == 0 && !on) {
            selected_preset = x - 3;
            load_preset(selected_preset);
        }

        // setting of mode
        if ((x > 2 && x < 7) && (y > 1 && y < 6) && p.config.mode == STEP) {
            if (!on) return;
            p.config.mode = LOGICAL;
            initialize_defaults(LOGICAL);
        } else if ((x > 8 && x < 13) && (y > 1 && y < 6) && p.config.mode == LOGICAL) {
            if (!on) return;
            p.config.mode = STEP;
            initialize_defaults(STEP);
        }

        // input config clocked or clock rotation
        if ((x == 14 || x == 15) && y == 0 && !on) p.config.input_config = ROTATE;
        if ((x == 0 || x == 1) && y == 0 && !on) p.config.input_config = CLOCK;

        // speed config via grid
        if (x >= 0 && x <= 15 && y == 7 && !on) {
            // TODO: experiment with speed divisions from grid
            // 30-1000 is current limit
            speed = (x + 1) * SINGLE_DIVISION_SPEED;
            update_speed();
            refresh_grid();
        }

        refresh_grid();

    } else if (page == MAIN) {
        if (!on) return;

        // select a row
        if (x == 0 && p.config.mode == LOGICAL) {
            selected_row = y;
        }

        // logic type
        if (x > 0 && x < 4 && p.config.mode == LOGICAL) {
            u8 was_toggled_off = p.row[selected_row].logic.type == x && p.row[selected_row].logic.compared_to_row - 1 == y;
            // LOGICAL button press rules for x 1-3 (NONE/AND/OR/XOR)

            // don't allow for channel A->B & B->A (circular logic)
            // don't allow selection of logic on selected row (self referencing)
            if (is_circularly_referenced(y)) {
                do_error = 1;
                error_ref_row = y + 1;
            } else if (selected_row == y) {
                do_error = 1;
                error_ref_row = selected_row + 1;
            } else {
                p.row[selected_row].logic.type = was_toggled_off ? 0 : x;
                p.row[selected_row].logic.compared_to_row = was_toggled_off ? 0 : y + 1;
                p.row[selected_row].division = get_division(p.row[selected_row].position);
                p.row[selected_row].pattern_length = was_toggled_off ? p.row[selected_row].division : p.row[selected_row].division * p.row[p.row[selected_row].logic.compared_to_row - 1].division;
                update_ticker(selected_row);
            }
        }

        // logical division press
        if (x > 3 && x < 16 && p.config.mode == LOGICAL && p.row[y].position != x) {
            // LOGICAL button press rules for x 4-15 (divisions)
            p.row[y].position = x;
            p.row[y].division = get_division(p.row[y].position);

            if (p.row[y].logic.compared_to_row > 0) {
                p.row[y].pattern_length = p.row[y].division * p.row[p.row[y].logic.compared_to_row - 1].division;
            } else {
                p.row[y].pattern_length = p.row[y].division;
            }

            update_ticker(y);

            // update rows that logically reference this one
            for (u8 i = 0; i < GATE_OUTS; i++) {
                if (i != y && p.row[i].logic.compared_to_row - 1 == y) {
                    p.row[i].pattern_length = p.row[y].division * p.row[i].division;
                    update_ticker(i);
                }
            }
        }

        // step press
        if (p.config.mode == STEP) {
            switch(p.row[y].step.gl[x]) {
                case OFF: p.row[y].step.gl[x] = SHORT; p.row[y].step.pulse[x] = 1; break;
                case SHORT: p.row[y].step.gl[x] = LONG; break;
                case LONG: p.row[y].step.gl[x] = OFF; p.row[y].step.pulse[x] = 0; break;
            }
        }
    }
}

void process_grid_held(u8 x, u8 y) {
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
    // save slots
    for (u8 x = 3; x < 13; x++) {
        set_grid_led(x, 0, 6);
    }

    // grid speed config leds
    float asl = ((float)speed / MAX_SPEED) * 15;
    u8 active_speed_led = (int)asl;
    
    for (u8 x = 0; x < 16; x++) {
        set_grid_led(x, 7, x == active_speed_led ? B_FULL + 4 : 6);
    }

    // input config, 0+1 = CLOCK, 14+15 = ROTATE
    set_grid_led(0, 0, p.config.input_config == 0 ? B_FULL + 4 : B_HALF);
    set_grid_led(1, 0, p.config.input_config == 0 ? B_FULL + 4 : B_HALF);
    set_grid_led(14, 0, p.config.input_config == 1 ? B_FULL + 4 : B_HALF);
    set_grid_led(15, 0, p.config.input_config == 1 ? B_FULL + 4 : B_HALF);

    set_grid_led((selected_preset % 10) + 3, 0, 14);
}

u8 set_logic_led(u8 r, u8 t) {
    if (p.row[selected_row].logic.type == t && p.row[selected_row].logic.compared_to_row - 1 == r) {
        return B_FULL + 3;
    } else {
        if (p.config.mode == LOGICAL) {
            if (is_circularly_referenced(r) || selected_row == r) {
               return B_DIM; 
            } else {
                return B_DIM + 3;
            }
        }
    }
    return 0;
}

void set_glyph_leds(enum mode l) {
    u8 bs = 8;
    u8 be = 8;

    if (l == LOGICAL) {
        bs = 13;
    } else if (l == STEP) {
        be = 13;
    }

    // LOGICAL
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

    // STEP
    // row 1
    set_grid_led(9, 2, be);
    set_grid_led(10, 2, 2);
    set_grid_led(11, 2, be);
    set_grid_led(12, 2, 4);
    // row 2
    set_grid_led(9, 3, 2);
    set_grid_led(10, 3, 2);
    set_grid_led(11, 3, 4);
    set_grid_led(12, 3, be);
    // row 3
    set_grid_led(9, 4, be);
    set_grid_led(10, 4, 4);
    set_grid_led(11, 4, 2);
    set_grid_led(12, 4, 2);
    // row 4
    set_grid_led(9, 5, 4);
    set_grid_led(10, 5, be);
    set_grid_led(11, 5, 2);
    set_grid_led(12, 5, be);
}

void render_grid(void) {
    if (!is_grid_connected()) return;

    if (page == CONFIG) {
        clear_all_grid_leds();
        set_preset_leds();
        set_glyph_leds(p.config.mode);
    } else {
        clear_all_grid_leds();

        if (p.config.mode == LOGICAL) {
            for (u8 i = 0; i < GATE_OUTS; i++) {
                set_grid_led(0, i, p.row[i].logic.compared_to_row > 0 ? B_DIM : 0);
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
        } else if (p.config.mode == STEP) {
            u8 step_br;

            for (u8 y = 0; y < GATE_OUTS; y++) {
                for (u8 x = 0; x < 16; x++) {
                    switch (p.row[y].step.gl[x]) {
                        case OFF: step_br = 0; break;
                        case SHORT: step_br = B_DIM; break;
                        case LONG: step_br = B_HALF; break;
                    }
                    if (x == step_ticker) {
                        set_grid_led(x, y, p.row[y].step.pulse[x] ? step_br + 6 : 0);
                    } else {
                        set_grid_led(x, y, p.row[y].step.pulse[x] ? step_br : 0);
                    }
                }
            }
        }
    }
}

void render_arc(void) {
    // TODO: add arc support!
}