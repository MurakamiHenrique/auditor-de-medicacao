#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace auditor {
constexpr int W = 160, H = 120, SIDE = 32, N = SIDE * SIDE, GUARD = 192;
constexpr int DAY_S = 86400;
using Feature = std::array<uint8_t, N>;
using Context = std::array<uint8_t, GUARD>;
enum class Observation { Unknown, Present, Empty };
// Armed: waiting for the daily window. Alert keeps sampling during late_s to detect a late removal.
enum class State { Idle, Armed, Monitoring, Removed, NotLoaded, Alert, LateRemoved, Fault };
const char *name(Observation v);
const char *name(State v);
struct Roi { int x = 40, y = 24, w = 80, h = 72; };
struct Settings {
    Roi roi{};
    int hour = 8, minute = 0, window_s = 60, interval_s = 5, confirmations = 2;
    // A cell "changed" when it differs from the empty reference by more than
    // max(change, calibrated noise + margin) grey levels, after removing the global brightness offset.
    float change = 20, margin = 8, context_distance = 20, stability = 8;
    int min_cells = 6; // smallest changed blob (in 32x32 cells) accepted as a dose
    int profile = 0; // 0 unconfirmed, 1 AI Thinker, 2 WROVER KIT
    int late_s = 1800; // extra monitoring after an alert; 0 disables
    bool enabled = true; // daily schedule active
};
struct Sample { Feature roi{}; Context context{}; uint16_t context_count = 0; bool quality = false; };
// Only the empty compartment is a reference: anything that appears in it, of any shape and in any
// position, is a dose. `noise` is the 99th-percentile cell difference measured while calibrating.
struct Calibration {
    Sample empty{};
    float noise = 0;
    bool has_empty = false;
};
// Comparison with the empty reference. `top` is the mean of the 10% largest cell differences
// (for logs), `blob` the largest 4-connected group of changed cells and `cells` all changed cells.
struct Change { float offset = 0, top = 255; int blob = 0, cells = 0; };
struct Result {
    Observation label = Observation::Unknown;
    Change change{};
    float threshold = 255, context = 255;
};
bool valid(const Settings &s);
bool same_roi(const Roi &a, const Roi &b);
bool extract(const uint8_t *gray, size_t len, const Roi &roi, Sample &out);
float distance(const uint8_t *a, const uint8_t *b, size_t n); // mean absolute difference
float context_distance(const Sample &a, const Sample &b);
// Cell differences after subtracting the median signed difference (global brightness shift).
Change compare(const Feature &v, const Feature &ref, float threshold);
// 99th percentile of the offset-corrected cell differences: the noise of one frame.
float noise_level(const Feature &v, const Feature &ref);
float cell_threshold(const Calibration &c, const Settings &s);
bool calibrated(const Calibration &c, const Settings &s);
// Present: a changed blob of at least min_cells. Empty: no blob above min_cells/2 and few changed
// cells. Anything in between, a scene/light change or a bad image is unknown, never empty.
Result classify(const Sample &sample, const Calibration &c, const Settings &s);

// Seconds from local time-of-day `now_s` until the daily window starts. Negative when now lies
// inside the window (the caller may resume it with less time left); never below -window_s.
int64_t next_window_offset(int64_t now_s, const Settings &s);
// Final results trigger a caregiver notification; Alert is notified on entry, not on expiry.
bool notifiable(State v);

// Times are monotonic milliseconds, never wall-clock values.
class Cycle {
public:
    State state = State::Idle;
    // Schedules one window [start, end]. Presence is verified inside the window, not at arming.
    bool arm(int64_t now, int64_t start, int64_t end, const Settings &s);
    void cancel();
    bool needs_sample(int64_t now) const;
    void observe(int64_t completed_at, Observation value);
    void tick(int64_t now);
    int64_t start() const { return start_; }
    int64_t deadline() const { return end_; }
    int64_t late_deadline() const { return late_end_; }
    bool seen_present() const { return seen_present_; }
    // Window in progress: configuration and camera commands must wait.
    bool sampling(int64_t now) const {
        return state == State::Monitoring || (state == State::Alert && now <= late_end_);
    }
    // No window pending or in progress: the scheduler may arm the next one.
    bool finished(int64_t now) const {
        return state != State::Armed && state != State::Monitoring && !sampling(now);
    }
private:
    void record(int64_t now, Observation value);
    Settings settings_{};
    int64_t start_ = 0, end_ = 0, late_end_ = 0, next_ = 0, last_ = -1;
    int streak_ = 0;
    bool seen_present_ = false;
    Observation latest_ = Observation::Unknown;
};
} // namespace auditor
