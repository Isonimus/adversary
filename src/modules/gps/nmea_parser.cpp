/**
 * @file nmea_parser.cpp
 * @brief NMEA 0183 sentence parser implementation
 */

#include "nmea_parser.h"
#include <string.h>
#include <stdlib.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <ctype.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../../../test/common/arduino_mocks.h"
#endif

namespace adversary {
namespace gps {

bool NMEAParser::validateChecksum(const char* sentence) {
    if (!sentence || sentence[0] != '$') {
        return false;
    }
    
    // Find the * character
    const char* asterisk = strchr(sentence, '*');
    if (!asterisk || strlen(asterisk) < 3) {
        return false;  // No checksum or incomplete
    }
    
    // Calculate XOR checksum of all characters between $ and *
    uint8_t calc = 0;
    for (const char* p = sentence + 1; p < asterisk; p++) {
        calc ^= *p;
    }
    
    // Parse hex checksum after *
    char checksumStr[3] = {asterisk[1], asterisk[2], '\0'};
    uint8_t expected = (uint8_t)strtol(checksumStr, nullptr, 16);
    
    return calc == expected;
}

double NMEAParser::nmeaToDecimal(double nmeaCoord, char hemisphere) {
    // NMEA format: DDMM.MMMM or DDDMM.MMMM
    // Extract degrees and minutes
    int degrees = (int)(nmeaCoord / 100);
    double minutes = nmeaCoord - (degrees * 100);
    
    // Convert to decimal degrees
    double decimal = degrees + (minutes / 60.0);
    
    // Apply hemisphere (S and W are negative)
    if (hemisphere == 'S' || hemisphere == 'W') {
        decimal = -decimal;
    }
    
    return decimal;
}

bool NMEAParser::parseFloat(const char* field, float* outValue) {
    if (!field || !outValue || field[0] == '\0') {
        return false;
    }
    *outValue = atof(field);
    return true;
}

bool NMEAParser::parseDouble(const char* field, double* outValue) {
    if (!field || !outValue || field[0] == '\0') {
        return false;
    }
    *outValue = atof(field);
    return true;
}

bool NMEAParser::parseInt(const char* field, int* outValue) {
    if (!field || !outValue || field[0] == '\0') {
        return false;
    }
    *outValue = atoi(field);
    return true;
}

bool NMEAParser::parseGGA(const char* sentence, GPSCoordinate* outCoord) {
    if (!sentence || !outCoord || !validateChecksum(sentence)) {
        return false;
    }
    
    // Accept any NMEA talker ID ($GPxxx, $GNxxx, $GLxxx, $GAxxx, $BDxxx, …)
    // NMEA format: '$' + 2-char talker + message-type + ','
    if (strncmp(sentence + 3, "GGA,", 4) != 0) {
        return false;
    }
    
    // Tokenize manually to handle empty fields (consecutive commas)
    const char* p = sentence;
    int fieldIndex = 0;
    
    double lat = 0, lon = 0;
    char latHem = 'N', lonHem = 'E';
    int fixQuality = 0, sats = 0;
    float hdop = 99.9f, altitude = 0.0f;
    
    while (p && *p != '\0' && *p != '*' && fieldIndex < 15) {
        const char* nextComma = strchr(p, ',');
        const char* nextAsterisk = strchr(p, '*');
        const char* end = nullptr;
        
        if (nextComma && (!nextAsterisk || nextComma < nextAsterisk)) {
            end = nextComma;
        } else {
            end = nextAsterisk;
        }
        
        char token[32] = {0};
        if (end) {
            size_t len = end - p;
            if (len >= sizeof(token)) len = sizeof(token) - 1;
            memcpy(token, p, len);
            token[len] = '\0';
            p = end + 1;
        } else {
            strncpy(token, p, sizeof(token) - 1);
            p = nullptr;
        }
        
        switch (fieldIndex) {
            case 0: break;  // Sentence type
            case 1:  // Time: hhmmss.ss
                if (token[0] != '\0') {
                    int raw;
                    parseInt(token, &raw);
                    outCoord->hour = raw / 10000;
                    outCoord->minute = (raw / 100) % 100;
                    outCoord->second = raw % 100;
                }
                break;
            case 2:  // Latitude
                if (token[0] != '\0') parseDouble(token, &lat);
                break;
            case 3:  // Latitude hemisphere
                if (token[0] != '\0') latHem = token[0];
                break;
            case 4:  // Longitude
                if (token[0] != '\0') parseDouble(token, &lon);
                break;
            case 5:  // Longitude hemisphere
                if (token[0] != '\0') lonHem = token[0];
                break;
            case 6:  // Fix quality
                if (token[0] != '\0') parseInt(token, &fixQuality);
                break;
            case 7:  // Satellites
                if (token[0] != '\0') parseInt(token, &sats);
                break;
            case 8:  // HDOP
                if (token[0] != '\0') parseFloat(token, &hdop);
                break;
            case 9:  // Altitude
                if (token[0] != '\0') parseFloat(token, &altitude);
                break;
        }
        
        fieldIndex++;
        if (!end || *end == '*') break;
    }
    
    // Populate output
    outCoord->latitude = nmeaToDecimal(lat, latHem);
    outCoord->longitude = nmeaToDecimal(lon, lonHem);
    outCoord->altitude = altitude;
    outCoord->satellites = sats;
    outCoord->fixQuality = fixQuality;
    outCoord->hdop = hdop;
    outCoord->valid = (fixQuality > 0 && sats >= 4);
    outCoord->timestamp = time(nullptr);  // Use system time (may be synced)
    
    // DEBUG: Log parsed values periodically
    static uint32_t lastParseLog = 0;
    if (millis() - lastParseLog > 3000) {
        Serial.printf("[NMEA] GGA parsed: fix=%d sats=%d hdop=%.1f valid=%d lat=%.6f lon=%.6f\n",
                      fixQuality, sats, hdop, outCoord->valid ? 1 : 0,
                      outCoord->latitude, outCoord->longitude);
        lastParseLog = millis();
    }
    
    // Return true if sentence was successfully parsed (even without fix)
    // Caller can check outCoord->valid for fix status
    return true;
}

bool NMEAParser::parseRMC(const char* sentence, GPSCoordinate* outCoord, GPSVelocity* outVel) {
    if (!sentence || !validateChecksum(sentence)) {
        return false;
    }
    
    // Accept any talker ID
    if (strncmp(sentence + 3, "RMC,", 4) != 0) {
        return false;
    }
    
    // Tokenize manually
    const char* p = sentence;
    int fieldIndex = 0;
    
    double lat = 0, lon = 0;
    char latHem = 'N', lonHem = 'E';
    char status = 'V';
    float speed = 0.0f, course = 0.0f;
    
    while (p && *p != '\0' && *p != '*' && fieldIndex < 13) {
        const char* nextComma = strchr(p, ',');
        const char* nextAsterisk = strchr(p, '*');
        const char* end = nullptr;
        
        if (nextComma && (!nextAsterisk || nextComma < nextAsterisk)) {
            end = nextComma;
        } else {
            end = nextAsterisk;
        }
        
        char token[32] = {0};
        if (end) {
            size_t len = end - p;
            if (len >= sizeof(token)) len = sizeof(token) - 1;
            memcpy(token, p, len);
            token[len] = '\0';
            p = end + 1;
        } else {
            strncpy(token, p, sizeof(token) - 1);
            p = nullptr;
        }
        
        switch (fieldIndex) {
            case 0: break;
            case 1:  // Time: hhmmss.ss
                if (token[0] != '\0') {
                    int raw;
                    parseInt(token, &raw);
                    outCoord->hour = raw / 10000;
                    outCoord->minute = (raw / 100) % 100;
                    outCoord->second = raw % 100;
                }
                break;
            case 2:  // Status
                if (token[0] != '\0') status = token[0];
                break;
            case 3:  // Latitude
                if (token[0] != '\0') parseDouble(token, &lat);
                break;
            case 4:  // Latitude hemisphere
                if (token[0] != '\0') latHem = token[0];
                break;
            case 5:  // Longitude
                if (token[0] != '\0') parseDouble(token, &lon);
                break;
            case 6:  // Longitude hemisphere
                if (token[0] != '\0') lonHem = token[0];
                break;
            case 7:  // Speed
                if (token[0] != '\0') parseFloat(token, &speed);
                break;
            case 8:  // Course
                if (token[0] != '\0') parseFloat(token, &course);
                break;
            case 9:  // Date: ddmmyy
                if (token[0] != '\0') {
                    int raw;
                    parseInt(token, &raw);
                    outCoord->day = raw / 10000;
                    outCoord->month = (raw / 100) % 100;
                    int year = raw % 100;
                    if (year < 80) outCoord->year = 2000 + year;
                    else outCoord->year = 1900 + year;
                }
                break;
        }
        
        fieldIndex++;
        if (!end || *end == '*') break;
    }
    
    bool valid = (status == 'A');
    
    // Populate coordinate even if not valid (so we can see data before fix)
    if (outCoord) {
        outCoord->latitude = nmeaToDecimal(lat, latHem);
        outCoord->longitude = nmeaToDecimal(lon, lonHem);
        outCoord->valid = valid;  // Mark as valid/invalid based on status
        outCoord->timestamp = time(nullptr);
    }
    
    // Populate velocity if requested
    if (outVel) {
        outVel->speedKmh = speed * 1.852f;  // knots to km/h
        outVel->courseTrue = course;
        outVel->valid = valid;
    }
    
    return valid;
}

bool NMEAParser::parseVTG(const char* sentence, GPSVelocity* outVel) {
    if (!sentence || !outVel || !validateChecksum(sentence)) {
        return false;
    }
    
    // Accept any talker ID
    if (strncmp(sentence + 3, "VTG,", 4) != 0) {
        return false;
    }
    
    // Tokenize manually
    const char* p = sentence;
    int fieldIndex = 0;
    
    float course = 0.0f, speedKmh = 0.0f;
    char mode = 'N';
    
    while (p && *p != '\0' && *p != '*' && fieldIndex < 10) {
        const char* nextComma = strchr(p, ',');
        const char* nextAsterisk = strchr(p, '*');
        const char* end = nullptr;
        
        if (nextComma && (!nextAsterisk || nextComma < nextAsterisk)) {
            end = nextComma;
        } else {
            end = nextAsterisk;
        }
        
        char token[32] = {0};
        if (end) {
            size_t len = end - p;
            if (len >= sizeof(token)) len = sizeof(token) - 1;
            memcpy(token, p, len);
            token[len] = '\0';
            p = end + 1;
        } else {
            strncpy(token, p, sizeof(token) - 1);
            p = nullptr;
        }
        
        switch (fieldIndex) {
            case 0: break;
            case 1:  // Course
                if (token[0] != '\0') parseFloat(token, &course);
                break;
            case 7:  // Speed
                if (token[0] != '\0') parseFloat(token, &speedKmh);
                break;
            case 9:  // Mode
                if (token[0] != '\0') mode = token[0];
                break;
        }
        
        fieldIndex++;
        if (!end || *end == '*') break;
    }
    
    outVel->courseTrue = course;
    outVel->speedKmh = speedKmh;
    outVel->valid = (mode == 'A' || mode == 'D');
    
    return outVel->valid;
}

} // namespace gps
} // namespace adversary
