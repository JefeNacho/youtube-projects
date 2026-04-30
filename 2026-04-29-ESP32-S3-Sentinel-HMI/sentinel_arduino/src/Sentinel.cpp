/**
 * Sentinel Arduino Library v1.0
 * Autor: Ignacio Aguilera
 */

#include "Sentinel.h"
#include <string.h>
#include <stdio.h>

// ===========================================================================
// Helper: separar nombre y extension
// ===========================================================================

const char* SentinelLogger::_splitExt(const char* filename,
                                       char* dst, uint8_t dstLen) {
    const char* ext = strrchr(filename, '.');
    uint8_t len = ext ? (uint8_t)(ext - filename) : (uint8_t)strlen(filename);
    if (len >= dstLen) len = dstLen - 1;
    strncpy(dst, filename, len);
    dst[len] = '\0';
    return ext;
}

// ===========================================================================
// SentinelFile
// ===========================================================================

SentinelFile::SentinelFile(Stream& port, const char* base, bool isCSV)
    : _port(&port), _isCSV(isCSV) {
    strncpy(_base, base, SENTINEL_NAME_LEN - 1);
    _base[SENTINEL_NAME_LEN - 1] = '\0';
}

void SentinelFile::write(float values[], uint8_t count, uint8_t decimals) {
    _port->print(F("CSV>"));
    _port->print(_base);
    _port->print(':');
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) _port->print(',');
        _port->print(values[i], decimals);
    }
    _port->println();
}

void SentinelFile::write(int values[], uint8_t count) {
    _port->print(F("CSV>"));
    _port->print(_base);
    _port->print(':');
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) _port->print(',');
        _port->print(values[i]);
    }
    _port->println();
}

void SentinelFile::write(int32_t values[], uint8_t count) {
    _port->print(F("CSV>"));
    _port->print(_base);
    _port->print(':');
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) _port->print(',');
        _port->print((long)values[i]);
    }
    _port->println();
}

void SentinelFile::write(const char* kv) {
    _port->print(F("KV>"));
    _port->print(_base);
    _port->print(':');
    _port->println(kv);
}

void SentinelFile::close() {
    if (_isCSV) {
        _port->print(F("CSV!"));
    } else {
        _port->print(F("JSON!"));
    }
    _port->println(_base);
}

// ===========================================================================
// SentinelLogger
// ===========================================================================

SentinelLogger::SentinelLogger(Stream& port, uint8_t t)
    : _port(port), _terminal(t) {}

SentinelLogger& SentinelLogger::terminal(uint8_t t) {
    _terminal = t;
    return *this;
}

void SentinelLogger::_prefix() {
    if (_terminal > 0) {
        _port.print('T');
        _port.print(_terminal);
        _port.print(':');
    }
}

void SentinelLogger::_colored(const char* color, const char* msg) {
    _prefix();
    _port.print('#'); _port.print(color); _port.print(' ');
    _port.print(msg);
    _port.println('#');
}

void SentinelLogger::_coloredF(const char* color, const __FlashStringHelper* msg) {
    _prefix();
    _port.print('#'); _port.print(color); _port.print(' ');
    _port.print(msg);
    _port.println('#');
}

// --------------------------------------------------------------------------
// Logging
// --------------------------------------------------------------------------

void SentinelLogger::print(const char* msg)                { _colored(SL_COL_NORMAL, msg); }
void SentinelLogger::print(const __FlashStringHelper* msg) { _coloredF(SL_COL_NORMAL, msg); }

void SentinelLogger::info(const char* msg) {
    _prefix();
    _port.print(F("#" SL_COL_INFO " [INFO] ")); _port.print(msg); _port.println('#');
}
void SentinelLogger::info(const __FlashStringHelper* msg) {
    _prefix();
    _port.print(F("#" SL_COL_INFO " [INFO] ")); _port.print(msg); _port.println('#');
}

void SentinelLogger::warn(const char* msg) {
    _prefix();
    _port.print(F("#" SL_COL_WARN " [WARN] ")); _port.print(msg); _port.println('#');
}
void SentinelLogger::warn(const __FlashStringHelper* msg) {
    _prefix();
    _port.print(F("#" SL_COL_WARN " [WARN] ")); _port.print(msg); _port.println('#');
}

void SentinelLogger::error(const char* msg) {
    _prefix();
    _port.print(F("#" SL_COL_ERROR " [ERR ] ")); _port.print(msg); _port.println('#');
}
void SentinelLogger::error(const __FlashStringHelper* msg) {
    _prefix();
    _port.print(F("#" SL_COL_ERROR " [ERR ] ")); _port.print(msg); _port.println('#');
}

void SentinelLogger::separator(char c, uint8_t len) {
    if (len > 40) len = 40;
    _prefix();
    _port.print(F("#" SL_COL_SEP " "));
    for (uint8_t i = 0; i < len; i++) _port.print(c);
    _port.println('#');
}

// --------------------------------------------------------------------------
// Archivos
// --------------------------------------------------------------------------

SentinelFile SentinelLogger::open(const char* filename,
                                   const char* headers[], uint8_t count) {
    char base[SENTINEL_NAME_LEN];
    const char* ext = _splitExt(filename, base, sizeof(base));

    bool isCSV = ext && strcmp(ext, ".csv") == 0;

    // Si es CSV y se proporcionaron headers, registrar el schema
    if (isCSV && headers && count > 0) {
        _port.print(F("CMD:CSV_SCHEMA:"));
        _port.print(base);
        _port.print(':');
        for (uint8_t i = 0; i < count; i++) {
            if (i > 0) _port.print(',');
            _port.print(headers[i]);
        }
        _port.println();
    }

    return SentinelFile(_port, base, isCSV);
}

// --------------------------------------------------------------------------
// Metricas y progreso
// --------------------------------------------------------------------------

void SentinelLogger::metric(const char* name, float value, const char* unit) {
    _port.print(F("MET>")); _port.print(name);
    _port.print(':'); _port.print(value, 4);
    _port.print(':'); _port.println(unit);
}

void SentinelLogger::metric(const char* name, int32_t value, const char* unit) {
    _port.print(F("MET>")); _port.print(name);
    _port.print(':'); _port.print((long)value);
    _port.print(':'); _port.println(unit);
}

void SentinelLogger::metric(const char* name, int value, const char* unit) {
    metric(name, (int32_t)value, unit);
}

void SentinelLogger::metric(const char* label, uint32_t current, uint32_t total) {
    if (total == 0) return;
    uint8_t pct    = (uint8_t)(100UL * current / total);
    uint8_t filled = (uint8_t)(20UL  * current / total);

    _prefix();
    _port.print(F("#" SL_COL_INFO " "));
    if (label && label[0]) { _port.print(label); _port.print(' '); }
    _port.print('[');
    for (uint8_t i = 0; i < 20; i++) {
        if      (i < filled)  _port.print('=');
        else if (i == filled) _port.print('>');
        else                  _port.print(' ');
    }
    _port.print(F("] ")); _port.print(pct); _port.print(F("% ("));
    _port.print(current); _port.print('/'); _port.print(total);
    _port.println(F(")#"));
}

void SentinelLogger::sendRaw(const char* line) {
    _port.println(line);
}
