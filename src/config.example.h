#pragma once

// 4-digit code entered in the Aero XP app. It is the only auth the cover checks.
#define COVER_KEY "0000"

// How long each move keeps the connection (and, in hold-to-run directions, the
// button). Set it a bit longer than a full open or close; the controller stops
// the motor at its end stops.
#define MOVE_TIME_MS 125000

// How long the other button is held to stop a tap-started move (opening in the
// default mode). Too short and the controller ignores it; too long and the cover
// starts moving the other way.
#define STOP_PRESS_MS 1500
