//
// Copyright(C) 2024 RP2040 Doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//     Pico Display Pack A/B/X/Y button input
//
#ifndef I_BUTTONS_H
#define I_BUTTONS_H

void I_ButtonsInit(void);
// Sample the buttons and post any resulting Doom key events; cheap to call often
void I_ButtonsPoll(void);
// Sample the buttons (called every LCD line from core 1, RAM resident); rate limited internally
void I_ButtonsSample(void);

#endif
