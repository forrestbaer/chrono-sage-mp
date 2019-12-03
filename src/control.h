// ----------------------------------------------------------------------------
// defines functions for multipass to send events to the controller (grid 
// presses etc)
//
// defines functions for engine to send updates (note on etc)
//
// defines data structures for multipass preset management
// ----------------------------------------------------------------------------

// 128, 64, 32, 16, 8, 7, 6, 5, 4, 3, 2, 1
#define MAX_STEPS 128

#pragma once
#include "types.h"

// ----------------------------------------------------------------------------
// firmware dependent stuff starts here


// ----------------------------------------------------------------------------
// shared types

enum logical_type { NONE, AND, OR, NOR };
enum logic_depth { SINGLE, NESTED };
enum input_config { CLOCK, ROTATE };
enum page_type { MAIN, CONFIG };

typedef struct node {
    u8 v;
    struct node *next;
} Node;

typedef struct {
    enum logical_type type;
    u8 compared_row;
} logic_t;

typedef struct {
    enum logic_depth logic_depth;
    enum input_config input_config;
    u8 clock_divs[12]; 
} config_t;

typedef struct {
    u8 position;
    u8 division;
    u8 offset;
    u8 triggers[MAX_STEPS];
    u8 blink;
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
