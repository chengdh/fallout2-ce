#ifndef PIPBOY_SERVER_H_
#define PIPBOY_SERVER_H_

#include <string>

namespace fallout {

// Pip-Boy Link server: exposes a live snapshot of the game state to a companion
// app running on another display. See PIPBOY.md for the wire protocol.
//
// Threading model (important):
//   * The game is single threaded. Everything that touches game state must run
//     on the main thread, therefore `pipboyServerTick()` is called from the main
//     loop and is the *only* place that reads `gDude` and friends.
//   * The network thread never touches game state. It only consumes the
//     snapshot map produced by the main thread.
//
// Default state is DISABLED: no socket is bound and no thread is created until
// the user turns it on, so the feature costs nothing when unused.

// Reads settings and, if enabled, starts the listener thread.
// Safe to call when disabled - it will do nothing and return true.
bool pipboyServerInit();

// Stops the thread and closes all sockets. Safe to call more than once.
void pipboyServerExit();

// Called once per frame from the main loop. Internally rate limited to
// `sample_interval_ms`, and returns immediately when disabled.
void pipboyServerTick();

bool pipboyServerIsEnabled();

// Turns the server on/off at runtime. Returns the resulting state.
bool pipboyServerSetEnabled(bool enabled);

// Hotkey helper: flips the current state, returns the new state.
bool pipboyServerToggle();

// Short human readable line for the on-screen toast, e.g. "Pip-Boy: ON 27000".
// Returned buffer is owned by the server and valid until the next call.
const char* pipboyServerStatusMessage();

} // namespace fallout

#endif /* PIPBOY_SERVER_H_ */
