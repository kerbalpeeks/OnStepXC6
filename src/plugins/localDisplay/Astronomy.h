// -----------------------------------------------------------------------------------
// Low-precision ephemeris for solar system bodies.
// Accuracy: Moon ~2°, Jupiter ~1° — sufficient for a pointer toy.
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

// Mean obliquity of the ecliptic (degrees) for a given JD
static inline double _obliquity(double jd) {
  return 23.439 - 3.563e-7 * (jd - 2451545.0);
}

// Convert ecliptic longitude/latitude (degrees) to RA (hours) and Dec (degrees)
static void _eclToEqu(double lon, double lat, double eps,
                      double *ra, double *dec) {
  double l = lon * M_PI / 180.0;
  double b = lat * M_PI / 180.0;
  double e = eps * M_PI / 180.0;
  *dec = asin(sin(b)*cos(e) + cos(b)*sin(e)*sin(l)) * 180.0 / M_PI;
  double raRad = atan2(sin(l)*cos(e) - tan(b)*sin(e), cos(l));
  *ra  = _wrap360(raRad * 180.0 / M_PI) / 15.0;  // degrees → hours
}

// ---------------------------------------------------------------------------
// Public: Moon position
// Uses Meeus low-precision method (~2° accuracy).

static void moon(double jd, double *ra, double *dec) {
  double d   = jd - 2451545.0;
  double L   = _wrap360(218.316 + 13.176396 * d);   // ecliptic longitude of Moon
  double M   = _wrap360(134.963 + 13.064993 * d);   // Moon's mean anomaly
  double F   = _wrap360(93.272  + 13.229350 * d);   // Moon's argument of latitude
  double lon = L + 6.289 * sin(M * M_PI / 180.0);   // ecliptic longitude
  double lat =     5.128 * sin(F * M_PI / 180.0);   // ecliptic latitude
  _eclToEqu(lon, lat, _obliquity(jd), ra, dec);
}

// ---------------------------------------------------------------------------
// Public: Jupiter position
// Simplified orbital elements with first-order secular rates (~1° accuracy).

static void jupiter(double jd, double *ra, double *dec) {
  double T  = (jd - 2451545.0) / 36525.0;          // Julian centuries from J2000

  // Orbital elements of Jupiter (Meeus Table 33.a)
  double L  = _wrap360(34.351519  + 3034.905675 * T);   // mean longitude (deg)
  double a  = 5.202603;                                   // semi-major axis (AU)
  double e  = 0.048498  + 0.000163 * T;                  // eccentricity
  double i  = (1.303270  - 0.019877 * T) * M_PI / 180.0; // inclination (rad)
  double Om = _wrap360(100.464407 + 1.021352 * T) * M_PI / 180.0; // long. asc. node (rad)
  double wp = _wrap360(14.331207  + 1.612227 * T);                 // lon. perihelion (deg)

  // Mean anomaly (deg → rad)
  double Mj = _wrap360(L - wp) * M_PI / 180.0;
  double wp_rad = wp * M_PI / 180.0;

  // Solve Kepler's equation by two Newton–Raphson steps
  double E = Mj + e * sin(Mj) * (1.0 + e * cos(Mj));
  for (int n = 0; n < 2; n++) E -= (E - e * sin(E) - Mj) / (1.0 - e * cos(E));

  // Heliocentric distance and true longitude
  double v  = 2.0 * atan2(sqrt(1.0 + e) * sin(E / 2.0),
                           sqrt(1.0 - e) * cos(E / 2.0));
  double r  = a * (1.0 - e * cos(E));
  double u  = v + wp_rad;   // true longitude in orbit plane (rad)

  // Heliocentric ecliptic Cartesian coordinates (AU)
  double xh = r * (cos(Om)*cos(u) - sin(Om)*sin(u)*cos(i));
  double yh = r * (sin(Om)*cos(u) + cos(Om)*sin(u)*cos(i));
  double zh = r *  sin(u) * sin(i);

  // Earth's heliocentric position (simplified circular orbit, ~1° accuracy)
  double Le = _wrap360(100.464572 + 35999.372 * T) * M_PI / 180.0;
  double xe = cos(Le),  ye = sin(Le);

  // Geocentric ecliptic coordinates
  double dx = xh - xe, dy = yh - ye, dz = zh;
  double lon = _wrap360(atan2(dy, dx) * 180.0 / M_PI);
  double lat = atan2(dz, sqrt(dx*dx + dy*dy)) * 180.0 / M_PI;

  _eclToEqu(lon, lat, _obliquity(jd), ra, dec);
}

} // namespace Astronomy
