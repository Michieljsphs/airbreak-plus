#include "stubs.h"
#include "common_code.h"

#include "my_asv.h"
#include "feat_triggercycle.h"

const float INSTANT_PS = 0.45f;

typedef struct {
  float last_ips; // Last inspiratory support
  float insp_ps; // PS on the last inspiratory tick
  float peak_ps; // PS when final_ips was set
  float eps; // EPS (cmH2O) - used to prevent instant jumps in pressure in case of autotriggering
  float ips_fa; // Flow-Assist IPS (cmH2O) - currently used to augment pretrigger effort
  float final_ips; // Peak inspiratory support, used for the expiratory downslope
} features_t;

STATIC void init_features(features_t *feat) {
  feat->last_ips = 0.0f;
  feat->insp_ps = 0.0f;
  feat->peak_ps = 0.0f;
  feat->eps = 0.0f;
  feat->ips_fa = 0.0f;
  feat->final_ips = 0.0f;
}


extern void pressure_limit_max_difference();
extern int variable_get_by_id(int var_id);
extern const unsigned short wrapper_limit_max_pdiff_toggle_var_id;
extern const unsigned short wrapper_limit_max_pdiff_triggercycle_var_id;
extern const unsigned short wrapper_limit_max_pdiff_asv_max_var_id;
extern const unsigned short wrapper_limit_max_pdiff_asv_sens_var_id;
extern const unsigned short wrapper_limit_max_pdiff_eps_var_id;
extern const unsigned short wrapper_limit_max_pdiff_exhale_shape_var_id;
extern const unsigned short wrapper_limit_max_pdiff_ps_ramp_var_id;
// Patch-time Start PS in 1/50 cmH2O, used while the var_id slot is 0xFFFF.
extern const unsigned short wrapper_limit_max_pdiff_ps_ramp_default;
extern const unsigned short wrapper_limit_max_pdiff_fl_boost_var_id;
extern const unsigned short wrapper_limit_max_pdiff_ffl_var_id;

STATIC bool custom_g8_toggle(unsigned short var_id, bool fallback) {
  return (var_id == 0xFFFFu) ? fallback : (variable_get_by_id((int)var_id) != 0);
}

STATIC int custom_var_raw(unsigned short var_id, int fallback) {
  return (var_id == 0xFFFFu) ? fallback : variable_get_by_id((int)var_id);
}

STATIC float custom_asv_sens_multiplier(void) {
  switch (custom_var_raw(wrapper_limit_max_pdiff_asv_sens_var_id, 2)) {
    case 0: return 0.50f;
    case 1: return 0.75f;
    case 3: return 1.25f;
    case 4: return 1.50f;
    case 2:
    default: return 1.00f;
  }
}

typedef enum {
  VAUTO_OFF,
  VAUTO_WAIT_INSPIRATION,
  VAUTO_ACTIVE,
} vauto_state_t;

typedef struct {
  therapy_worker prepare, finish;
  breath_tracking_t *tracking;
  triggercycle_t trc;
  asv_data_t asv;
  features_t features;
  int mode;
  vauto_state_t vauto_state;
  float asv_max, asv_sens, eps, exhale_shape;
  float start_ps; // Start PS (cmH2O) at the beginning of the ramp; negative = off
  float fl_boost_max; // FL Boost setting: support added at full flow limitation (cmH2O)
  float fl_level; // Per-breath smoothed flow-limitation index (0.0-1.0)
  float fl_ps; // FL Boost support for the current breath (cmH2O)
  bool custom_enabled, custom_tc;
  volatile bool settings_dirty;
  bool reset;
} breath_controller_t;

static const therapy_worker_method prepare_methods[7];

STATIC breath_controller_t *find_breath_controller(void) {
  return (breath_controller_t *)therapy_worker_find(2, prepare_methods);
}

void custom_breath_settings_changed(void) {
  // Startup callbacks can precede worker creation. Do not allocate state here.
  breath_controller_t *s = find_breath_controller();
  if (s) { s->settings_dirty = true; }
}

STATIC void load_breath_settings(breath_controller_t *s) {
  // Clear before reading so a callback during the reload remains pending.
  s->settings_dirty = false;
  s->custom_enabled = custom_g8_toggle(wrapper_limit_max_pdiff_toggle_var_id, true);
  s->custom_tc = custom_g8_toggle(wrapper_limit_max_pdiff_triggercycle_var_id, true);
  s->asv_max = custom_var_raw(wrapper_limit_max_pdiff_asv_max_var_id, 0) / 50.0f;
  s->asv_sens = custom_asv_sens_multiplier();
  s->eps = custom_var_raw(wrapper_limit_max_pdiff_eps_var_id, 50) / 50.0f;
  s->exhale_shape = -0.75f + 0.25f * custom_var_raw(wrapper_limit_max_pdiff_exhale_shape_var_id, 0);
  // Stored 0 is off; n > 0 is (n - 10) / 50 cmH2O, so 0.0 is a real ramp start.
  const int ps_raw = custom_var_raw(wrapper_limit_max_pdiff_ps_ramp_var_id,
                                    (int)wrapper_limit_max_pdiff_ps_ramp_default);
  s->start_ps = ps_raw > 0 ? (float)(ps_raw - 10) / 50.0f : -1.0f;
  s->fl_boost_max = wrapper_limit_max_pdiff_ffl_var_id == 0xFFFFu ? 0.0f :
                    custom_var_raw(wrapper_limit_max_pdiff_fl_boost_var_id, 0) / 50.0f;
}

STATIC void init_breath_controller(breath_controller_t *s) {
  unsigned char *p = (unsigned char *)s;
  for (unsigned i = 0; i < sizeof(*s); ++i) { p[i] = 0; }
  init_triggercycle(&s->trc);
  init_asv_data(&s->asv);
  init_features(&s->features);
  s->settings_dirty = true;
  s->reset = true;
}

STATIC void breath_noop(therapy_worker *node) { }

STATIC void breath_stop(therapy_worker *node) {
  breath_controller_t *s = (breath_controller_t *)node;
  s->prepare.enabled = s->finish.enabled = 0;
  s->vauto_state = VAUTO_OFF;
  s->trc.trigger = s->trc.cycle = TC_STOCK;
  s->reset = true;
}

STATIC void breath_mode(therapy_worker *node) {
  breath_controller_t *s = (breath_controller_t *)node;
  // Lifecycle callbacks use the requested mode; I[6F] is published next tick.
  s->mode = ivars[0x12];
  s->prepare.enabled = s->finish.enabled = s->mode == MODE_VAUTO || s->mode == MODE_S;
  s->vauto_state = VAUTO_OFF;
  s->trc.trigger = s->trc.cycle = TC_STOCK;
  s->settings_dirty = true;
  s->reset = true;
}

STATIC void reset_vauto_state(breath_controller_t *s) {
  init_asv_data(&s->asv);
  init_features(&s->features);
  s->fl_level = 0.0f;
  s->fl_ps = 0.0f;
}

// FL Boost: the firmware's fuzzy flow-limitation index (FFL, 0.00-1.00) is
// sampled once per breath. A fast-attack, slow-release average ignores single
// flattened breaths, follows sustained flow limitation within a few breaths, and
// keeps support for a while after the airway opens so the boost does not
// oscillate on and off with its own effect.
#define FL_ATTACK 0.15f // per breath while FFL is above the average
#define FL_RELEASE 0.03f // per breath while FFL is below the average
#define FL_START 0.05f // average where FL Boost starts
#define FL_FULL 0.25f // average where FL Boost reaches the setting
#define FL_RISE_AT 0.5f // stock pressure phase at which the boost is fully applied

STATIC void update_fl_boost(breath_controller_t *s) {
  if (!(s->fl_boost_max > 0.0f)) {
    s->fl_level = 0.0f;
    s->fl_ps = 0.0f;
    return;
  }
  float ffl = clamp01(variable_get_by_id((int)wrapper_limit_max_pdiff_ffl_var_id) / 100.0f);
  float rate = ffl > s->fl_level ? FL_ATTACK : FL_RELEASE;
  s->fl_level += (ffl - s->fl_level) * rate;
  s->fl_ps = s->fl_boost_max * remap01c(s->fl_level, FL_START, FL_FULL);
}

STATIC void breath_prepare(therapy_worker *node) {
  breath_controller_t *s = (breath_controller_t *)node;
  if (s->settings_dirty) { load_breath_settings(s); }
  if (s->reset) {
    init_triggercycle(&s->trc);
    reset_vauto_state(s);
    s->reset = false;
  }

  bool requested = s->mode == MODE_VAUTO && vauto_ps > 0.0f && s->custom_enabled;
  if (!requested) {
    if (s->vauto_state != VAUTO_OFF) { reset_vauto_state(s); }
    s->vauto_state = VAUTO_OFF;
  } else if (s->vauto_state == VAUTO_OFF) {
    s->vauto_state = VAUTO_WAIT_INSPIRATION;
  }

  bool enabled = ivars[0x71] != 1 && s->custom_tc;
  if (s->mode == MODE_VAUTO) {
    enabled &= s->vauto_state == VAUTO_ACTIVE;
  }
  prepare_triggercycle(&s->trc, &s->tracking->tr, &s->tracking->hist, enabled);
}

STATIC void breath_finish(therapy_worker *node) {
  breath_controller_t *s = (breath_controller_t *)((unsigned char *)node -
                              __builtin_offsetof(breath_controller_t, finish));
  const tracking_t *tr = &s->tracking->tr;
  if (s->vauto_state == VAUTO_WAIT_INSPIRATION &&
      tr->st_inhaling && tr->st_just_started) {
    s->vauto_state = VAUTO_ACTIVE;
  }
}

static const therapy_worker_method prepare_methods[7] = {
  breath_noop, breath_noop, breath_stop, breath_mode, breath_mode, breath_stop, breath_prepare
};
static const therapy_worker_method finish_methods[7] = {
  breath_noop, breath_noop, breath_noop, breath_noop, breath_noop, breath_noop, breath_finish
};

// Called once by the tracker while constructing the native worker list.
void custom_breath_attach_prepare(breath_tracking_t *tracking) {
  breath_controller_t *s = find_breath_controller();
  if (!s) {
    s = malloc(sizeof(*s));
    init_breath_controller(s);
    s->tracking = tracking;
    therapy_worker_append(&s->prepare, 2);
    s->prepare.methods = prepare_methods;
    s->prepare.enabled = 0;
  }
}

void custom_breath_attach_finish(breath_tracking_t *tracking) {
  breath_controller_t *s = find_breath_controller();
  if (!s->finish.methods) {
    therapy_worker_append(&s->finish, 2);
    s->finish.methods = finish_methods;
    s->finish.enabled = 0;
  }
}

// Assembly comparison hooks only read decisions prepared for this tick.
int custom_tc_decision(int cycle) {
  breath_controller_t *s = find_breath_controller();
  if (!s || !s->prepare.enabled || s->reset) { return TC_STOCK; }
  return cycle ? s->trc.cycle : s->trc.trigger;
}

// Reshapes PS in 0.0-1.0 format to differently shaped slopes with `mult` times the AUC, first increasing slope before magnitude
// Only using ^4 shape, because going to ^8 and above is very jarring and results in bad premature cycling
STATIC float reshape_vauto_ps(float ps1, float mult) {
  // ^2 - 1.330, ^6 - 1.707, ^8 - 1.770
  float inverse = 1.0f - ps1;
  float inverse_squared = inverse * inverse;
  float ps4 = 1.0f - inverse_squared * inverse_squared;  // ~1.594x the AUC
  ps4 = ps4 * 0.25f + ps1 * 0.75f; // 25%=1.1485x, 50%=~1.297x the AUC
  const float auc = 1.1485;
  if (mult <= 1.0) { 
    return ps1; 
  } else if ((mult > 1.0) && (mult <= 2.0)) {
    return remap(mult, 1.0f, 2.0f, ps1, ps4 * (2.0f / auc));
  } else {
    return ps4 * (mult / auc);
  }

  return ps1;
}


// Start PS: while the ramp runs, support rises from Start PS to the set PS in
// step with the ramped pressure reference (F[2D] going from F[05] to F[28]).
// The stock ramp state decides whether a ramp is running: F[2E] is the remaining
// ramp time in minutes, and scaling stops the moment it is zero, so a reference
// that settles a little short of F[28] (VAuto moves it, Ramp=Auto) cannot leave
// the support reduced for the rest of the night.
// Only the support above EPAP is scaled, so EPAP follows the stock ramp and
// only IPAP is lower. Returns `ps_cmd` unchanged when the feature is off, no
// ramp is running, or the ramp reference span is too small to measure.
STATIC float ps_ramp_apply(const breath_controller_t *s, float ps_cmd) {
  if (!s || s->mode != MODE_VAUTO || !(vauto_ps > 0.0f) || !(s->start_ps >= 0.0f)) {
    return ps_cmd;
  }
  const float remaining = fvars[0x2e];
  if (!__builtin_isfinite(remaining) || !(remaining > 0.0f)) { return ps_cmd; }
  const float span = fvars[0x28] - fvars[0x05];
  const float abs_span = span < 0.0f ? -span : span;
  if (!__builtin_isfinite(span) || abs_span < 0.05f) { return ps_cmd; }
  const float progress = clamp01((*cmd_epap_ramp - fvars[0x05]) / span);
  if (!__builtin_isfinite(progress)) { return ps_cmd; }
  const float start = min(s->start_ps, vauto_ps);
  const float k = (start + (vauto_ps - start) * progress) / vauto_ps;
  const float support = ps_cmd + vauto_ps / 2.0f; // support above EPAP
  // Only reduce positive support; never exceed the set PS.
  if (!(support > 0.0f) || !(k < 1.0f)) { return ps_cmd; }
  return support * k - vauto_ps / 2.0f;
}

// Stock pressure limiter with the Start PS scaling applied to the PS command.
STATIC void limit_pressure_stock(const breath_controller_t *s) {
  const float orig_ps = *cmd_ps;
  *cmd_ps = ps_ramp_apply(s, orig_ps);
  pressure_limit_max_difference();
  *cmd_ps = orig_ps;
}

void MAIN start() {
  breath_controller_t *s = find_breath_controller();
  if (!s || s->vauto_state != VAUTO_ACTIVE || !(vauto_ps > 0.0f)) {
    limit_pressure_stock(s);
    return;
  }

  tracking_t *tr = &s->tracking->tr;
  asv_data_t *asv = &s->asv;
  float asv_max = wrapper_limit_max_pdiff_asv_max_var_id == 0xFFFFu ?
                  2.0f * vauto_ps : s->asv_max;
  asv->asv_sens = s->asv_sens;
  update_asv_data(asv, tr, asv_max > 0.0f);

  features_t *feat = &s->features;
  if (feat->eps < -s->eps) { feat->eps = -s->eps; }

  float new_ps = *cmd_ps;

  float current_eps = s->eps;

  const float ps = *cmd_ps + vauto_ps/2.0f;
  const float ps1 = (ps/vauto_ps);

  if (tr->st_inhaling) {
    if (tr->st_just_started) {
      feat->final_ips = 0.0f;
      feat->peak_ps = vauto_ps;
      update_fl_boost(s);
    }
    // FL Boost rises with the stock pressure phase, reaching full strength at
    // FL_RISE_AT of it so the boost arrives early in the inhale, and adds to the base shape.
    const float shape_ps = remap(ps1, 0.0f, 1.0f, feat->eps, vauto_ps-INSTANT_PS) + INSTANT_PS;
    float base_ps = shape_ps + s->fl_ps * clamp01(ps1 / FL_RISE_AT);
    new_ps = base_ps;
    if (asv_max > 0.0f) {
      float new_ps1 = reshape_vauto_ps(ps1, asv->asv_factor);
      float boosted_ps = remap(new_ps1, 0.0f, 1.0f, feat->eps, vauto_ps - INSTANT_PS) + INSTANT_PS*asv->asv_factor;
      // ASV Max stays relative to the unboosted shape: the larger of the two boosts wins.
      new_ps = max(base_ps, min(boosted_ps, shape_ps + asv_max));
    }

    feat->ips_fa = 0.0f;
    feat->eps = min(feat->eps + 0.01f * current_eps, 0.0f);

    if (new_ps > feat->final_ips) {
      feat->final_ips = new_ps;
      feat->peak_ps = vauto_ps;
    }
    feat->last_ips = new_ps;
    feat->insp_ps = vauto_ps;
  } else { // Exhaling
    if (tr->current.ti >= 0.7f) {
      current_eps = max(0.0f, current_eps - max(0.0f, feat->final_ips - feat->peak_ps) * 0.25f);
      if (tr->st_just_started) { feat->eps = -current_eps; }
      else {
        float fraction = tr->current.volume_max > 0.0f
            ? tr->current.volume / tr->current.volume_max
            : 0.0f;
        float eps1 = remap01c(fraction, 0.10f, 0.7f);
        eps1 = sqrtf(eps1);
        eps1 = min(eps1, remap01c(tr->current.te, max(1.2f, tr->recent.te * 0.8f), max(0.4f, tr->recent.te * 0.4f)));
        feat->eps = max(feat->eps, -current_eps * eps1);
      }
    }
    // Keep the completed inspiration's scale across positive PS edits.
    const float exp_scale = feat->insp_ps > 0.0f ? feat->insp_ps : vauto_ps;
    float xc = clamp01(ps / exp_scale);
    float new_ps1 = xc + s->exhale_shape * xc * (1.0f - xc);
    new_ps = remap(new_ps1, 0.0f, 1.0f, feat->eps, feat->final_ips);
    // min() would hide NaN/Inf from the final output check.
    if (__builtin_isfinite(new_ps)) { new_ps = min(new_ps, feat->last_ips); }

    if (tr->st_pre_trigger > 0) { feat->ips_fa = min(tr->st_pre_trigger, 2) * 0.2f; };
    if (*flow_compensated <= 0.0f) { feat->ips_fa = 0.0f; }
    new_ps += feat->ips_fa;
  }

  // Convert support above EPAP back to the signed midpoint-relative waveform.
  new_ps = *cmd_ps + (new_ps - ps);

  if (!__builtin_isfinite(new_ps)) {
    s->vauto_state = VAUTO_OFF;
    s->reset = true;
    limit_pressure_stock(s);
    return;
  }

  const float orig_ps = *cmd_ps;
  new_ps = ps_ramp_apply(s, new_ps);
  // Keep pre-FOT pressure at least 4 cmH2O, or the stock value if lower.
  const float pressure_floor = min(4.0f, *cmd_epap_ramp + orig_ps);
  if (*cmd_epap_ramp + new_ps < pressure_floor) {
    new_ps = pressure_floor - *cmd_epap_ramp;
  }
  *cmd_ps = new_ps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;
}

#include "my_asv.c"
#include "feat_triggercycle.c"
