#pragma once

/**
 * @file nmea_parser.h
 * @brief NMEA 0183 sentence parser for GPS data
 * 
 * Parses common NMEA sentences from AT6668 GPS module:
 * - $GNGGA: Global positioning system fix data
 * - $GNRMC: Recommended minimum specific GPS data
 * - $GNVTG: Track made good and speed over ground
 */

#include "gps_types.h"
#include <stdint.h>

namespace adversary {
namespace gps {

/**
 * @brief NMEA sentence parser (static functions)
 */
class NMEAParser {
public:
    /**
     * @brief Parse GGA sentence (Global Positioning System Fix Data)
     * 
     * Format: $GNGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,q,ss,h.h,a.a,M,g.g,M,,*hh
     * 
     * @param sentence NMEA sentence string
     * @param outCoord Output coordinate structure
     * @return true if parsed successfully
     */
    static bool parseGGA(const char* sentence, GPSCoordinate* outCoord);
    
    /**
     * @brief Parse RMC sentence (Recommended Minimum Data)
     * 
     * Format: $GNRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,s.s,c.c,ddmmyy,,,A*hh
     * 
     * @param sentence NMEA sentence string
     * @param outCoord Output coordinate structure (optional)
     * @param outVel Output velocity structure (optional)
     * @return true if parsed successfully
     */
    static bool parseRMC(const char* sentence, GPSCoordinate* outCoord, GPSVelocity* outVel);
    
    /**
     * @brief Parse VTG sentence (Track Made Good and Ground Speed)
     * 
     * Format: $GNVTG,c.c,T,,,s.s,N,k.k,K,A*hh
     * 
     * @param sentence NMEA sentence string
     * @param outVel Output velocity structure
     * @return true if parsed successfully
     */
    static bool parseVTG(const char* sentence, GPSVelocity* outVel);
    
    /**
     * @brief Validate NMEA sentence checksum
     * 
     * XORs all characters between $ and * and compares with hex checksum
     * 
     * @param sentence NMEA sentence string (must include $ and *hh)
     * @return true if checksum is valid
     */
    static bool validateChecksum(const char* sentence);
    
private:
    /**
     * @brief Convert NMEA coordinate format (DDMM.MMMM) to decimal degrees
     * 
     * @param nmeaCoord Coordinate in DDMM.MMMM format
     * @param hemisphere 'N', 'S', 'E', or 'W'
     * @return Decimal degrees (negative for S/W)
     */
    static double nmeaToDecimal(double nmeaCoord, char hemisphere);
    
    /**
     * @brief Parse a float field from NMEA sentence
     * 
     * @param field Field string
     * @param outValue Output value pointer
     * @return true if parsed successfully
     */
    static bool parseFloat(const char* field, float* outValue);
    
    /**
     * @brief Parse a double field from NMEA sentence
     * 
     * @param field Field string
     * @param outValue Output value pointer
     * @return true if parsed successfully
     */
    static bool parseDouble(const char* field, double* outValue);
    
    /**
     * @brief Parse an integer field from NMEA sentence
     * 
     * @param field Field string
     * @param outValue Output value pointer
     * @return true if parsed successfully
     */
    static bool parseInt(const char* field, int* outValue);
};

} // namespace gps
} // namespace adversary
