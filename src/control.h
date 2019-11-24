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

typedef struct {
    enum logical_type type;
    u8 value;
    u8 compared_row;
} logic_t;

typedef struct {
    u8 position;
    u8 blink;
    logic_t logic;
} row_params_t;

typedef struct {
} shared_data_t;

typedef struct {
} preset_meta_t;

typedef struct {
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
