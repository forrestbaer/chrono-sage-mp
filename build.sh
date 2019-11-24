#!/bin/sh
cd multipass/monome_euro/meadowphysics
make
cp multipass_mp.hex ../../../dist/chrono-sage.hex
make clean
cd ../../../
zip -r -X chrono-sage.zip dist
