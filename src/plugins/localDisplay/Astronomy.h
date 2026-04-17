// -----------------------------------------------------------------------------------
// Low-precision ephemeris for solar system bodies.
// Accuracy: Moon ~2°, planets ~1–2° — sufficient for a pointer toy.
// All public functions return RA in decimal hours (0–24) and Dec in degrees (±90).
// Input: Julian Day number (full JD, e.g. day + hour/24.0 from JulianDate struct).
// Source: Jean Meeus, "Astronomical Algorithms", 2nd ed., chapters 32 & 47.
#pragma once

#include <math.h>

namespace Astronomy {

// ---------------------------------------------------------------------------
// Internal helpers

static inline double _wrap360(double d) {
  d = fmod(d, 360.0);
  return (d < 0.0) ? d + 360.0 : d;
}

static inline double _obliquity(double jd) {
  return 23.439 - 3.563e-7 * (jd - 2451545.0);
}

static void _eclToEqu(double lon, double lat, double eps,
                      double *ra, double *dec) {
  double l = lon * M_PI / 180.0;
  double b = lat * M_PI / 180.0;
  double e = eps * M_PI / 180.0;
  *dec = asin(sin(b)*cos(e) + cos(b)*sin(e)*sin(l)) * 180.0 / M_PI;
  double raRad = atan2(sin(l)*cos(e) - tan(b)*sin(e), cos(l));
  *ra  = _wrap360(raRad * 180.0 / M_PI) / 15.0;
}

// ---------------------------------------------------------------------------
// Internal: shared planet solver (Meeus Table 33.a orbital elements).
// Accepts linear (unwrapped) element values in degrees; wrapping done here.

static void _outerPlanet(double jd, double T,
                          double L_lin, double a, double e,
                          double i_lin, double Om_lin, double wp_lin,
                          double *ra, double *dec) {
  double L      = _wrap360(L_lin);
  double wp     = _wrap360(wp_lin);
  double Om_rad = _wrap360(Om_lin) * M_PI / 180.0;
  double i_rad  = i_lin            * M_PI / 180.0;
  double wp_rad = wp               * M_PI / 180.0;

  double Mp = _wrap360(L - wp) * M_PI / 180.0;
  double E  = Mp + e * sin(Mp) * (1.0 + e * cos(Mp));
  for (int n = 0; n < 2; n++) E -= (E - e * sin(E) - Mp) / (1.0 - e * cos(E));

  double v  = 2.0 * atan2(sqrt(1.0 + e) * sin(E / 2.0),
                           sqrt(1.0 - e) * cos(E / 2.0));
  double r  = a * (1.0 - e * cos(E));
  double u  = v + wp_rad - Om_rad;  // argument of latitude = true anomaly + argument of perihelion (ω̃ - Ω)

  double xh = r * (cos(Om_rad)*cos(u) - sin(Om_rad)*sin(u)*cos(i_rad));
  double yh = r * (sin(Om_rad)*cos(u) + cos(Om_rad)*sin(u)*cos(i_rad));
  double zh = r *  sin(u) * sin(i_rad);

  double Le = _wrap360(100.464572 + 35999.372 * T) * M_PI / 180.0;
  double xe = cos(Le), ye = sin(Le);

  double dx  = xh - xe, dy = yh - ye, dz = zh;
  double lon = _wrap360(atan2(dy, dx) * 180.0 / M_PI);
  double lat = atan2(dz, sqrt(dx*dx + dy*dy)) * 180.0 / M_PI;
  _eclToEqu(lon, lat, _obliquity(jd), ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Moon (~0.3° accuracy, Meeus Ch.22 with main perturbation terms)

static void moon(double jd, double *ra, double *dec) {
  double d  = jd - 2451545.0;
  double L  = _wrap360(218.316 + 13.176396 * d);
  double M  = _wrap360(134.963 + 13.064993 * d) * M_PI / 180.0;
  double F  = _wrap360(93.272  + 13.229350 * d) * M_PI / 180.0;
  double D  = _wrap360(297.850 + 12.190749 * d) * M_PI / 180.0;  // mean elongation
  double Ms = _wrap360(357.529 +  0.985608 * d) * M_PI / 180.0;  // Sun's mean anomaly
  double lon = L
    + 6.289 * sin(M)
    - 1.274 * sin(2*D - M)
    + 0.658 * sin(2*D)
    - 0.214 * sin(2*M)
    - 0.186 * sin(Ms)
    - 0.059 * sin(2*D - 2*M)
    - 0.057 * sin(2*D - M + Ms)
    + 0.053 * sin(2*D + M)
    + 0.046 * sin(2*D - Ms);
  double lat =
    + 5.128 * sin(F)
    - 0.280 * sin(M + F)
    - 0.277 * sin(M - F)
    - 0.173 * sin(2*D - F)
    - 0.055 * sin(2*D - M + F);
  _eclToEqu(lon, lat, _obliquity(jd), ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Mercury (~1° accuracy, Meeus Table 33.a)

static void mercury(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    252.250906 + 149472.674986 * T,  // L
    0.387098,                         // a (AU)
    0.205631   + 0.000020     * T,   // e
    7.004986   - 0.002780     * T,   // i (deg)
    48.330893  + 1.186189     * T,   // Om (deg)
    77.456119  + 1.556473     * T,   // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Venus (~1° accuracy, Meeus Table 33.a)

static void venus(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    181.979801 + 58519.213030 * T,   // L
    0.723330,                         // a (AU)
    0.006771 - 0.000048 * T,         // e
    3.394662  + 0.001004 * T,        // i (deg)
    76.679920  + 0.901003 * T,       // Om (deg)
    131.563707 + 1.402507 * T,       // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Mars (~2° accuracy, Meeus Table 33.a)

static void mars(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    355.433275 + 19140.299    * T,   // L
    1.523679,                         // a (AU)
    0.093401   + 0.000092     * T,   // e
    1.849691   - 0.000818     * T,   // i (deg)
    49.558093  + 0.772020     * T,   // Om (deg)
    336.060234 + 1.840852     * T,   // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Jupiter (~1° accuracy, Meeus Table 33.a)

static void jupiter(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    34.351519  + 3034.905675 * T,    // L
    5.202603,                         // a (AU)
    0.048498   + 0.000163    * T,    // e
    1.303270   - 0.019877    * T,    // i (deg)
    100.464407 + 1.021352    * T,    // Om (deg)
    14.331207  + 1.612227    * T,    // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Saturn (~1° accuracy, Meeus Table 33.a)

static void saturn(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    50.077444  + 1222.113777 * T,    // L
    9.536676,                         // a (AU)
    0.054151   - 0.000150    * T,    // e
    2.488878   - 0.003400    * T,    // i (deg)
    113.665503 - 0.279655    * T,    // Om (deg)
    93.056787  + 1.963722    * T,    // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Uranus (~1° accuracy, Meeus Table 33.a)

static void uranus(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    314.055005 + 429.863546  * T,    // L
    19.191264,                        // a (AU)
    0.047168   - 0.000016    * T,    // e
    0.769986   - 0.004954    * T,    // i (deg)
    74.005957  + 0.521628    * T,    // Om (deg)
    173.005159 + 1.486378    * T,    // wp (deg)
    ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Neptune (~1° accuracy, Meeus Table 33.a)

static void neptune(double jd, double *ra, double *dec) {
  double T = (jd - 2451545.0) / 36525.0;
  _outerPlanet(jd, T,
    304.348665 + 219.885914  * T,    // L
    30.068963,                        // a (AU)
    0.008586   + 0.000001    * T,    // e
    1.769953   - 0.009821    * T,    // i (deg)
    131.784057 + 1.102204    * T,    // Om (deg)
    48.120276  + 0.029150    * T,    // wp (deg)
    ra, dec);
}

} // namespace Astronomy
