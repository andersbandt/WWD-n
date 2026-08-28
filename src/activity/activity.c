//*****************************************************************************
//! @file activity.c
//! @brief Activity session tracking. See activity.h for the design contract.
//*****************************************************************************

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <activity/activity.h>
#include <nvs.h>
#include <peripheral/clock.h>   /* get_dt_ticks() */

LOG_MODULE_REGISTER(activity, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * The catalogue. This table is the single source of truth: the menu builds
 * itself from it, so adding an activity is one row here plus one enum value.
 *
 * Keep names short. The activity screen draws them at FONT_MEDIUM and the
 * menu clips silently past ~14 characters with no wrap and no warning (see
 * the text-clipping budget in ui_display.c).
 */
static const struct {
    activity_id_t id;
    const char   *name;
} ACTIVITY_DEFS[] = {
    { ACTIVITY_RUN,   "Running"     },
    { ACTIVITY_EAT,   "Eating"      },
    { ACTIVITY_WORK,  "Working"     },
    { ACTIVITY_TV,    "Watching TV" },
    { ACTIVITY_PHONE, "Phone"       },
    { ACTIVITY_CLEAN, "Cleaning"    },
    { ACTIVITY_DRIVE, "Driving"     },
};

/* Row order is menu order, and it deliberately matches the enum: the ids are
 * on flash and cannot be reordered, so keeping the table in the same order
 * means one list to reason about instead of two.
 *
 * "Watching TV" is the longest name that fits. The activity screen draws
 * "> " + name + " *" at FONT_MEDIUM (8 px/char) into WIDTH - ACT_X = 124 px,
 * i.e. 15 characters — so a name has 11 to work with, and the 12th would be
 * silently clipped with no wrap and no warning. */

#define ACTIVITY_DEF_COUNT  (sizeof(ACTIVITY_DEFS) / sizeof(ACTIVITY_DEFS[0]))

static activity_id_t current_id;
static uint16_t      current_seq;
static uint16_t      next_seq;
static int64_t       started_uptime_ms;


void activity_init(void)
{
    current_id        = ACTIVITY_NONE;
    current_seq       = 0;
    next_seq          = 0;
    started_uptime_ms = 0;
}


size_t activity_count(void)
{
    return ACTIVITY_DEF_COUNT;
}


const char *activity_name_at(size_t idx)
{
    return (idx < ACTIVITY_DEF_COUNT) ? ACTIVITY_DEFS[idx].name : "";
}


activity_id_t activity_id_at(size_t idx)
{
    return (idx < ACTIVITY_DEF_COUNT) ? ACTIVITY_DEFS[idx].id : ACTIVITY_NONE;
}


const char *activity_name(activity_id_t id)
{
    for (size_t i = 0; i < ACTIVITY_DEF_COUNT; i++) {
        if (ACTIVITY_DEFS[i].id == id) {
            return ACTIVITY_DEFS[i].name;
        }
    }
    return "None";
}


/*
 * Writes one marker. nand_offset is captured BEFORE the record is appended,
 * so it points at where this marker itself lands — analysis can seek straight
 * there instead of walking pages from zero to find a session boundary.
 *
 * Treated as advisory, not authoritative: nvs_log_record() buffers into a page
 * that is flushed later, so the offset is exact only to the page the marker
 * lands in. The marker's own position in the decoded stream remains the
 * ground truth for span bounds; this is a seek hint.
 */
static int log_marker(uint8_t event, activity_id_t id, uint16_t seq)
{
    struct record_activity rec = {
        .event       = event,
        .activity_id = (uint8_t)id,
        .session_seq = seq,
        .nand_offset = (uint32_t)nvs_get_addr_offset(),
    };

    int rc = nvs_log_record(RECORD_ACTIVITY, &rec, sizeof(rec), get_dt_ticks());

    if (rc != 0) {
        /* Most likely NVS never came up (wrong/absent NAND). Warn once per
         * marker rather than failing the UI action — the on-screen session
         * still works, it just is not recorded. */
        LOG_WRN("activity: marker not logged (%s %s, seq=%u): %d",
                event == ACTIVITY_EVENT_START ? "START" : "STOP",
                activity_name(id), seq, rc);
    }
    return rc;
}


int activity_stop(void)
{
    if (current_id == ACTIVITY_NONE) {
        return -EALREADY;
    }

    activity_id_t stopping     = current_id;
    uint16_t      stopping_seq = current_seq;
    uint32_t      elapsed      = activity_elapsed_sec();

    /* Clear state BEFORE logging: if the log write fails we still want the
     * session ended on screen, and activity_elapsed_sec() must not keep
     * counting a session the user has already stopped. */
    current_id        = ACTIVITY_NONE;
    current_seq       = 0;
    started_uptime_ms = 0;

    LOG_INF("activity: STOP %s seq=%u after %u s",
            activity_name(stopping), stopping_seq, elapsed);

    return log_marker(ACTIVITY_EVENT_STOP, stopping, stopping_seq);
}


int activity_start(activity_id_t id)
{
    if (id == ACTIVITY_NONE) {
        return -EINVAL;
    }

    /* Restarting the running activity would split one session into two in the
     * log for no reason — a fumbled double-press should not corrupt a run. */
    if (current_id == id) {
        return 0;
    }

    if (current_id != ACTIVITY_NONE) {
        activity_stop();   /* mutually exclusive, see activity.h */
    }

    current_id        = id;
    current_seq       = next_seq++;
    started_uptime_ms = k_uptime_get();

    LOG_INF("activity: START %s seq=%u", activity_name(id), current_seq);

    return log_marker(ACTIVITY_EVENT_START, id, current_seq);
}


int activity_toggle(activity_id_t id)
{
    return (current_id == id) ? activity_stop() : activity_start(id);
}


bool activity_is_active(void)
{
    return current_id != ACTIVITY_NONE;
}


activity_id_t activity_current(void)
{
    return current_id;
}


uint16_t activity_current_seq(void)
{
    return current_seq;
}


uint32_t activity_elapsed_sec(void)
{
    if (current_id == ACTIVITY_NONE) {
        return 0;
    }
    return (uint32_t)((k_uptime_get() - started_uptime_ms) / 1000);
}
