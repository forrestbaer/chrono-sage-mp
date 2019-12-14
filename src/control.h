// ----------------------------------------------------------------------------
// defines functions for multipass to send events to the controller (grid 
// presses etc)
//
// defines functions for engine to send updates (note on etc)
//
// defines data structures for multipass preset management
// ----------------------------------------------------------------------------

#pragma once
#include "types.h"

// ----------------------------------------------------------------------------
// firmware dependent stuff starts here


// ----------------------------------------------------------------------------
// shared types

enum logical_type { NONE, AND, OR, NOR };
enum mode { LOGICAL, EUCLIDIAN };
enum input_config { CLOCK, ROTATE };
enum rotate_direction { LEFT, RIGHT };
enum page_type { MAIN, CONFIG };

typedef struct {
    enum logical_type type;
    u8 compared_to_row;
} logic_t;

typedef struct {
    enum mode mode;
    enum input_config input_config;
    u8 clock_divs[12]; 
} config_t;

typedef struct {
    u8 position;
    u8 division;
    u8 blink;
    u8 pattern_length;
    logic_t logic;
} row_params_t;

typedef struct {
} shared_data_t;

typedef struct {
} preset_meta_t;

typedef struct {
    config_t config;
    row_params_t row[8];
} preset_data_t;


// ----------------------------------------------------------------------------
// firmware settings/variables main.c needs to know


// ----------------------------------------------------------------------------
// functions control.c needs to implement (will be called from main.c)

void init_presets(void);
void init_control(void);
void process_event(u8 event, u8 *data, u8 length);
void render_grid(void);
void render_arc(void);


// ----------------------------------------------------------------------------
// functions engine needs to call
