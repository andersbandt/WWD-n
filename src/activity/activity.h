//*****************************************************************************
//! @file activity.h
//! @brief Activity session tracking — start/stop markers written into the NVS
//!        log so a dump can be sliced into "what was I doing" spans.
//*****************************************************************************

#ifndef SRC_ACTIVITY_ACTIVITY_H_
#define SRC_ACTIVITY_ACTIVITY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The session primitive.
 *
 * A "session" is: the user declares what they are doing, the device drops a
 * marker in the log, and something later reads between the markers. That is
 * the same shape whether the activity is a run (where the interesting output
 * is cadence and form analysis over the bounded IMU span) or eating (where
 * the interesting output is just how many minutes it took). So there is ONE
 * mechanism here, not one per activity type — adding "Driving" must not mean
 * writing a second state machine, a second record type, and a second decoder
 * branch.
 *
 * TO ADD AN ACTIVITY: append an id to activity_id_t, add one row to
 * ACTIVITY_DEFS in activity.c, and add the name to dump_decoder.py's
 * ACTIVITY_NAMES. Nothing else — the menu builds itself from the table, and
 * the record format does not change.
 *
 * SESSIONS ARE MUTUALLY EXCLUSIVE, on purpose. Starting one stops whatever
 * was running. The headline use is a time-allocation breakdown, and you are
 * doing one thing at a time — allowing overlap would make "how many hours did
 * I work" ambiguous and force the analysis to reason about intersections. If
 * genuinely concurrent tracking is ever needed (a run *during* a commute),
 * that is a deliberate format change, not something to bolt on here.
 */

typedef enum {
    ACTIVITY_NONE = 0,   /* not an activity — means "nothing running" */
    ACTIVITY_RUN  = 1,

    /* APPEND ONLY — these values are written to flash and are hand-mirrored
     * by dump_decoder.py. Inserting or renumbering silently rewrites the
     * meaning of every dump already taken. Planned: eating, driving, phone,
     * TV, working, exercise. */
} activity_id_t;

/** @brief Marker kind, as stored in struct record_activity.event. */
#define ACTIVITY_EVENT_STOP   0
#define ACTIVITY_EVENT_START  1

/** @brief Initialise session state. Call once, after nvs_init(). */
void activity_init(void);

/* ---- Catalogue (drives the menu; no UI code hardcodes an activity) ---- */

/** @brief How many activities are defined. */
size_t activity_count(void);

/** @brief Display name for catalogue slot @p idx, or "" if out of range.
 *  Names are kept short — the menu clips silently past ~14 chars. */
const char *activity_name_at(size_t idx);

/** @brief Activity id for catalogue slot @p idx, or ACTIVITY_NONE. */
activity_id_t activity_id_at(size_t idx);

/** @brief Display name for an id, or "None". */
const char *activity_name(activity_id_t id);

/* ---- Session control ---- */

/**
 * @brief Starts a session, stopping any already-running one first.
 *
 * Writes a RECORD_ACTIVITY START marker. Starting the activity that is
 * already running is a no-op (it does NOT restart it) — otherwise a
 * double-press would silently split one run into two.
 *
 * @return 0 on success, negative errno if the marker could not be logged
 *         (session state is still updated — see activity.c).
 */
int activity_start(activity_id_t id);

/**
 * @brief Stops the running session, writing a STOP marker.
 * @return 0 on success, -EALREADY if nothing was running.
 */
int activity_stop(void);

/** @brief Convenience: stop @p id if running, else start it. */
int activity_toggle(activity_id_t id);

/** @brief True while a session is running. */
bool activity_is_active(void);

/** @brief The running activity, or ACTIVITY_NONE. */
activity_id_t activity_current(void);

/**
 * @brief Session index of the running session — the "run index".
 *
 * Increments on every start. This is what analysis software keys on to tie a
 * span of IMU samples to a particular run. NOTE it is RAM-only and restarts
 * at 0 after a reset, so it is unique only *within a boot segment*; decoders
 * must scope it by the surrounding RESET_MARKER/TIME_ANCHOR, which they
 * already track for the time axis.
 */
uint16_t activity_current_seq(void);

/** @brief Seconds since the running session started, 0 if none. */
uint32_t activity_elapsed_sec(void);

#endif /* SRC_ACTIVITY_ACTIVITY_H_ */
