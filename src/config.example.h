#pragma once

// 4-digit code entered in the Aero XP app. It is the only auth the cover checks.
#define COVER_KEY "0000"

// Full travel times. Stopwatch a full open and a full close and put them here:
// they drive the position estimate and how long the button is held.
#define OPEN_TIME_MS 90000
#define CLOSE_TIME_MS 90000

// Keep holding past the estimated travel time so the cover always reaches its
// own end stop. The controller stops the motor at its limits.
#define EXTRA_HOLD_MS 5000
