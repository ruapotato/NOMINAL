/* util.c — allocation, growable buffers, deterministic RNG, and our own math.
 *
 * The math here exists so that a replay produced on one machine reproduces
 * bit-for-bit on another; the host libm makes no such promise. See D3.
 */
#include "nom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------- allocation */
void *nom_alloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) { fprintf(stderr, "nominal: out of memory\n"); abort(); }
    return p;
}

void *nom_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "nominal: out of memory\n"); abort(); }
    return q;
}

void nom_free(void *p) { free(p); }

char *nom_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = nom_alloc(n);
    memcpy(d, s, n);
    return d;
}

/* ---------------------------------------------------------------- buffers */
void buf_init(Buf *b) { b->p = NULL; b->len = b->cap = 0; }
void buf_free(Buf *b) { nom_free(b->p); buf_init(b); }
void buf_clear(Buf *b) { b->len = 0; if (b->p) b->p[0] = 0; }

static void buf_reserve(Buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + extra + 1) cap *= 2;
    b->p = nom_realloc(b->p, cap);
    b->cap = cap;
}

void buf_put(Buf *b, const void *data, size_t n)
{
    if (!n) return;
    buf_reserve(b, n);
    memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = 0;
}

void buf_puts(Buf *b, const char *s) { if (s) buf_put(b, s, strlen(s)); }
void buf_putc(Buf *b, char c) { buf_put(b, &c, 1); }

void buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { buf_put(b, tmp, (size_t)n); return; }
    char *big = nom_alloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_put(b, big, (size_t)n);
    nom_free(big);
}

/* Fixed-point decimal rendering. printf("%f") drags in locale and libc
 * rounding differences; a replay must not depend on either. */
void buf_putnum(Buf *b, double v, int decimals)
{
    if (v != v) { buf_puts(b, "nan"); return; }
    if (v > 1e18) { buf_puts(b, "inf"); return; }
    if (v < -1e18) { buf_puts(b, "-inf"); return; }
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;

    int neg = v < 0;
    if (neg) v = -v;

    int64_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;

    /* round half away from zero, in integer space */
    int64_t whole = (int64_t)v;
    double  frac  = v - (double)whole;
    int64_t fscaled = (int64_t)(frac * (double)scale + 0.5);
    if (fscaled >= scale) { whole += 1; fscaled -= scale; }

    if (neg && (whole || fscaled)) buf_putc(b, '-');
    buf_printf(b, "%lld", (long long)whole);
    if (decimals > 0) {
        buf_putc(b, '.');
        char digits[16];
        for (int i = decimals - 1; i >= 0; i--) {
            digits[i] = (char)('0' + (fscaled % 10));
            fscaled /= 10;
        }
        buf_put(b, digits, (size_t)decimals);
    }
}

/* -------------------------------------------------------------------- rng */
void rng_seed(Rng *r, uint64_t seed) { r->s = seed + 0x9E3779B97F4A7C15ULL; }

uint64_t rng_next(Rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double rng_unit(Rng *r)
{
    /* 53 bits into [0,1); exact in binary, so identical everywhere */
    return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

int32_t rng_range(Rng *r, int32_t lo, int32_t hi)
{
    if (hi <= lo) return lo;
    uint64_t span = (uint64_t)(hi - lo) + 1;
    return lo + (int32_t)(rng_next(r) % span);
}

/* ------------------------------------------------------------------ fmath */
static const double NOM_PI  = 3.14159265358979311600;
static const double NOM_2PI = 6.28318530717958623200;
static const double NOM_PI2 = 1.57079632679489655800;
static const double NOM_LN2 = 0.69314718055994530942;

double nom_fabs(double x) { return x < 0 ? -x : x; }

double nom_floor(double x)
{
    if (x != x) return x;
    if (x > 4.5e15 || x < -4.5e15) return x;   /* already integral */
    double t = (double)(int64_t)x;
    return (t > x) ? t - 1.0 : t;
}

double nom_sqrt(double x)
{
    if (x != x) return x;
    if (x <= 0.0) return 0.0;
    /* seed from the exponent field: halving it halves the magnitude's log */
    union { double d; uint64_t u; } v;
    v.d = x;
    v.u = (v.u >> 1) + 0x1FF8000000000000ULL;
    double y = v.d;
    for (int i = 0; i < 6; i++) y = 0.5 * (y + x / y);
    return y;
}

/* sin on |x| <= pi/2, Taylor through x^11 (~1e-13 worst case at pi/2) */
static double sin_core(double x)
{
    double x2 = x * x;
    return x * (1.0
         + x2 * (-1.0 / 6.0
         + x2 * (1.0 / 120.0
         + x2 * (-1.0 / 5040.0
         + x2 * (1.0 / 362880.0
         + x2 * (-1.0 / 39916800.0))))));
}

/* cos on |x| <= pi/2, Taylor through x^12 */
static double cos_core(double x)
{
    double x2 = x * x;
    return 1.0
         + x2 * (-1.0 / 2.0
         + x2 * (1.0 / 24.0
         + x2 * (-1.0 / 720.0
         + x2 * (1.0 / 40320.0
         + x2 * (-1.0 / 3628800.0
         + x2 * (1.0 / 479001600.0))))));
}

/* reduce to [-pi, pi] */
static double reduce_pi(double x)
{
    if (x > 1e12 || x < -1e12) return 0.0;   /* refuse to pretend; game values never get here */
    double n = nom_floor(x / NOM_2PI + 0.5);
    return x - n * NOM_2PI;
}

double nom_sin(double x)
{
    x = reduce_pi(x);
    if (x > NOM_PI2)  return  sin_core(NOM_PI - x);
    if (x < -NOM_PI2) return  sin_core(-NOM_PI - x);
    return sin_core(x);
}

double nom_cos(double x)
{
    x = reduce_pi(x);
    if (x < 0) x = -x;                 /* cos is even */
    if (x > NOM_PI2) return -cos_core(NOM_PI - x);
    return cos_core(x);
}

/* atan on |t| <= tan(pi/12), Taylor through t^21 */
static double atan_small(double t)
{
    double t2 = t * t, term = t, sum = t;
    for (int k = 1; k <= 10; k++) {
        term *= t2;
        double d = (double)(2 * k + 1);
        sum += ((k & 1) ? -term : term) / d;
    }
    return sum;
}

static double nom_atan(double x)
{
    int neg = x < 0;
    if (neg) x = -x;
    int inv = 0;
    if (x > 1.0) { x = 1.0 / x; inv = 1; }
    int shift = 0;
    const double TAN_PI_12 = 0.26794919243112270647;
    const double SQRT3     = 1.73205080756887729353;
    if (x > TAN_PI_12) {
        x = (x * SQRT3 - 1.0) / (SQRT3 + x);
        shift = 1;
    }
    double r = atan_small(x);
    if (shift) r += NOM_PI / 6.0;
    if (inv)   r = NOM_PI2 - r;
    return neg ? -r : r;
}

double nom_atan2(double y, double x)
{
    if (x == 0.0 && y == 0.0) return 0.0;
    if (x == 0.0) return y > 0 ? NOM_PI2 : -NOM_PI2;
    double a = nom_atan(y / x);
    if (x > 0) return a;
    return (y >= 0) ? a + NOM_PI : a - NOM_PI;
}

static double nom_exp(double x)
{
    if (x > 700.0)  return 1e308;
    if (x < -700.0) return 0.0;
    double k = nom_floor(x / NOM_LN2 + 0.5);
    double r = x - k * NOM_LN2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 14; i++) { term *= r / (double)i; sum += term; }
    /* scale by 2^k by constructing the exponent directly */
    int ki = (int)k;
    union { double d; uint64_t u; } v;
    if (ki > 1023) ki = 1023;
    if (ki < -1022) { sum *= 2.2250738585072014e-308; ki += 1022; }
    v.u = ((uint64_t)(ki + 1023)) << 52;
    return sum * v.d;
}

static double nom_log(double x)
{
    if (x <= 0.0) return -1e308;
    union { double d; uint64_t u; } v;
    v.d = x;
    int k = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFULL) | (1023ULL << 52);  /* m in [1,2) */
    double m = v.d;
    if (m > 1.41421356237309504880) { m *= 0.5; k += 1; }   /* centre on sqrt(2) */
    double s = (m - 1.0) / (m + 1.0), s2 = s * s;
    double term = s, sum = s;
    for (int i = 1; i <= 9; i++) { term *= s2; sum += term / (double)(2 * i + 1); }
    return 2.0 * sum + (double)k * NOM_LN2;
}

double nom_pow(double b, double e)
{
    /* exact path for integer exponents — the only one the sim uses */
    if (e == nom_floor(e) && e >= -64.0 && e <= 64.0) {
        int n = (int)e, neg = n < 0;
        if (neg) n = -n;
        double r = 1.0;
        for (int i = 0; i < n; i++) r *= b;
        return neg ? 1.0 / r : r;
    }
    if (b <= 0.0) return 0.0;
    return nom_exp(e * nom_log(b));
}
