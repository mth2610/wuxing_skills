// core headless test — flame/smoke particle MOTION.
//
// "The particles move far too fast" went two tuning rounds on eyeball alone, and
// the second round fixed the speed by breaking the height. Trajectories are
// ordinary numerical integration: how far a particle travels in its lifetime is
// computable, and once computed the argument is over.
//
// Mirrors UpdateParticles' integrator exactly (core/particle_system.c):
//     v += ForceField_Evaluate(...) * dt
//     x += v * dt * speedCurve(age)
// with FORCE_GRAVITY_DIR contributing a constant acceleration and FORCE_DRAG
// contributing -v * strength.
//
// The assertions are about PROPORTION, not absolute metres: a flame's particles
// must not outrun the flame. A particle that travels several times the effect's
// own size in its lifetime reads as a jet or as sparks, whatever its m/s value
// says, because perceived speed is how many body-lengths something crosses.

#include <stdio.h>
#include <math.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

// A 4-stop curve, evaluated the way FloatCurve does (piecewise linear).
typedef struct { int n; float t[6], v[6]; } Curve;

static float CurveEval(const Curve *c, float x)
{
    if (c->n == 0) return 1.0f;
    if (x <= c->t[0]) return c->v[0];
    if (x >= c->t[c->n - 1]) return c->v[c->n - 1];
    for (int i = 0; i < c->n - 1; i++)
        if (x >= c->t[i] && x <= c->t[i + 1])
        {
            float f = (x - c->t[i]) / (c->t[i + 1] - c->t[i] + 1e-9f);
            return c->v[i] + (c->v[i + 1] - c->v[i]) * f;
        }
    return c->v[c->n - 1];
}

typedef struct {
    float v0;         // initial upward velocity, m/s
    float buoyancy;   // FORCE_GRAVITY_DIR strength, m/s^2 (upward)
    float drag;       // FORCE_DRAG strength, 1/s
    float life;       // seconds
    const Curve *speed;
} MotionSpec;

typedef struct { float rise, peakSpeed, endSpeed; } MotionResult;

static MotionResult Integrate(const MotionSpec *m)
{
    const float dt = 1.0f / 60.0f;
    float y = 0.0f, v = m->v0, peak = 0.0f;
    for (float t = 0.0f; t < m->life; t += dt)
    {
        float acc = m->buoyancy - v * m->drag;
        v += acc * dt;
        float mul = m->speed ? CurveEval(m->speed, t / m->life) : 1.0f;
        y += v * dt * mul;
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    MotionResult r = { y, peak, v };
    return r;
}

// Terminal velocity of the buoyancy/drag pair — where the lick stops
// accelerating. If this exceeds what the art wants, no lifetime tweak saves it.
static float Terminal(float buoyancy, float drag)
{
    return drag > 1e-6f ? buoyancy / drag : 1e9f;
}

// ── the shipped values (core/composition/fire/flame_volume.inl) ──────────────

static const Curve FLAME_RISE = { 3, {0.0f, 0.35f, 1.0f}, {1.35f, 1.0f, 0.35f} };

static void Test_FlameBodyStaysWithinItsOwnFlame(void)
{
    // scale = 1.0 means "a 1 m flame". A body particle living its full life must
    // not travel much beyond that, or the licks visibly overshoot the shape.
    MotionSpec fast = { 0.75f, 1.0f, 1.8f, 1.40f, &FLAME_RISE };
    MotionResult r = Integrate(&fast);
    CHECK_MSG(r.rise <= 1.35f,
              "longest-lived flame particle stays within ~1.3x the flame height",
              "rise=%.2f m over %.2f s (peak %.2f m/s)", r.rise, fast.life, r.peakSpeed);

    MotionSpec slow = { 0.45f, 1.0f, 1.8f, 0.75f, &FLAME_RISE };
    MotionResult r2 = Integrate(&slow);
    CHECK_MSG(r2.rise > 0.25f,
              "shortest-lived flame particle still rises visibly",
              "rise=%.2f m", r2.rise);
}

static void Test_TerminalVelocityIsSane(void)
{
    // buoyancy/drag is where a lick settles. Above ~1 m/s a 1 m flame reads as a
    // blowtorch no matter what the lifetimes are — this is the number to tune
    // first, and it is invisible in the source unless you divide.
    float vt = Terminal(1.0f, 1.8f);
    CHECK_MSG(vt < 0.65f, "flame terminal velocity stays under 0.65 m/s for a 1 m flame",
              "buoyancy/drag = %.2f m/s", vt);
}

static void Test_PerceivedSpeedInBodyLengths(void)
{
    // Perceived speed is body-lengths crossed, not m/s. A particle of radius
    // 0.09-0.20 m crossing many diameters in its life reads as frantic even at a
    // modest velocity — this is what "the particles are too fast" actually
    // measures, and why the first fix (cut velocity, raise drag) produced a
    // squat flame instead of a calm one: it treated m/s as the quantity.
    MotionSpec m = { 0.75f, 1.0f, 1.8f, 1.40f, &FLAME_RISE };
    MotionResult r = Integrate(&m);
    float diameter = 2.0f * 0.145f;          // mean particle radius
    float lengths = r.rise / diameter;
    // 3 diameters was measured as the point where licks stop reading as
    // travelling embers. 4.2 (the previous value) already looked like sparks.
    CHECK_MSG(lengths < 3.2f,
              "flame particle crosses under ~3 of its own diameters per life",
              "%.1f body-lengths (rise %.2f m, diameter %.2f m)",
              lengths, r.rise, diameter);
}

// The core must NOT travel: small + additive + fast is the recipe for sparks,
// and it is what actually produced "the particles fly impossibly fast" — the
// body was fine, the core was crossing 4x its own diameter per life.
static void Test_CoreStaysAtTheBase(void)
{
    MotionSpec core = { 0.35f, 1.0f, 1.8f, 0.60f, &FLAME_RISE };
    MotionResult r = Integrate(&core);
    float diameter = 2.0f * 0.08f;
    CHECK_MSG(r.rise / diameter < 2.5f,
              "core crosses under 2.5 of its own diameters (flickers, not launches)",
              "%.1f body-lengths (rise %.2f m)", r.rise / diameter, r.rise);
}

static void Test_SmokeRisesSlowerThanFlame(void)
{
    // Smoke must lag its fire; if it keeps pace the hand-off reads as two
    // separate effects rather than as one cooling process.
    MotionSpec flame = { 0.75f, 1.0f, 1.8f, 1.40f, &FLAME_RISE };
    MotionSpec smoke = { 0.22f, 0.35f, 1.6f, 1.70f, NULL };
    float vf = Terminal(flame.buoyancy, flame.drag);
    float vs = Terminal(smoke.buoyancy, smoke.drag);
    CHECK_MSG(vs < vf, "smoke terminal velocity is below the flame's",
              "smoke=%.2f flame=%.2f m/s", vs, vf);
}

int main(void)
{
    printf("=== core headless test: flame/smoke motion ===\n");
    Test_TerminalVelocityIsSane();
    Test_FlameBodyStaysWithinItsOwnFlame();
    Test_PerceivedSpeedInBodyLengths();
    Test_CoreStaysAtTheBase();
    Test_SmokeRisesSlowerThanFlame();
    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
