## Chrono Sage

A logical clock divider alternative firmware for the Monome Meadowphysics module.

![Logical Mode](/doc/logical_mode.png) 
![Step Mode](/doc/step_mode.png) 
![Config](/doc/cs_config.png) 

One of my favorite clock dividers for euro is the Shakmat Time Wizard, primarily because of the built in logic functions that can give you all kinds of fun results just by twiddling some knobs. Problem is, it requires a clock, and only has two logic functions (AND/OR) which can only be used on one channel.  With the release of the [Multipass library](https://llllllll.co/t/multipass-a-framework-for-developing-firmwares-for-monome-eurorack-modules/26354) for monome modules by the awesome @scanner_darkly, modifying my fairly-unused Meadowphysics module to do what I wanted came into reach! 

### Requirements

* **Monome Grid** (if you want to do anything more than have 1-8 divisors on the clock from 1-8 outs)
* **Monome Meadowphysics**

### Documentation

Flash the module using monome [firmware update instructions here](https://monome.org/docs/modular/update/). Using the hex file below, there are flash scripts included in the zip.

**LOGICAL MODE:**
* The first column selects the output to set a logical condition for
* Columns 2-4 set the logical condition from the selected column to the output row selected, **column 1 is logical AND** (both gates are high), **column 2 is logical OR** (either gate is high), **column 3 is logical XOR** (gate when both outputs are not equal)
* Divisions are as listed in the image above for each output
- Logic is only checked one level deep, multiple channels can reference the same target, updates to target logic update all referencing channels.  **rules** : no self selection, no circular logic selection

**STEP MODE**
* Each row corresponds to an output 1-8, a row/output has 4 bars of 4 steps, each step can be a 15ms trigger (dim, single press) or a 50% clock PW gate (bright, double press), a third press on a gate/trigger will turn it off.

**CONFIGURATION PAGE**:
- Tap on the front button to go into configuration mode, the top 10 buttons are preset slots, hold to save current configuration into a slot, tap to load a slot, the two big glyphs are the two modes now functional in Chrono Sage (LOGICAL/STEP). 
- The bottom row is a speed control, slowest left, fastest right, will become useful when I port this over to the other trilogy modules.  
- The top left two buttons set the input jack to clock from an external source, the top right set the input jack to rotate rows top to bottom on pulse.
