#include "MidiData.h"

static const char* const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

MidiData::MidiData(const char* serverIp, uint16_t serverPort)
    : _serverIp(serverIp), _serverPort(serverPort), _noteCount(0) {}

String MidiData::urlEncode(const String& str) {
    String encoded = str;
    encoded.replace(" ", "%20");
    return encoded;
}

void MidiData::getNoteName(uint8_t pitch, char* buffer, size_t bufSize) {
    uint8_t noteIdx = pitch % 12;
    int octave = ((int)pitch / 12) - 1;
    snprintf(buffer, bufSize, "%s%d", NOTE_NAMES[noteIdx], octave);
}

bool MidiData::fetchByTrackName(const char* songName, const char* trackName) {
    String url = "http://" + String(_serverIp) + ":" + String(_serverPort) 
               + "/track_by_name/" + String(songName) + "/" + urlEncode(String(trackName));
    return fetchFromUrl(url);
}

bool MidiData::fetchByTrackIndex(const char* songName, int trackIndex) {
    String url = "http://" + String(_serverIp) + ":" + String(_serverPort) 
               + "/song/" + String(songName) + "/" + String(trackIndex);
    return fetchFromUrl(url);
}

bool MidiData::fetchFromUrl(const String& url) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MidiData] Error: Wi-Fi not connected!");
        return false;
    }

    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();
    bool success = false;

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream());

        if (!error) {
            _noteCount = 0; // Reset count
            JsonArray notesArray = doc["notes"].as<JsonArray>();

            for (JsonObject n : notesArray) {
                if (_noteCount >= MAX_NOTES) {
                    Serial.printf("[MidiData] WARNING: Reached maximum static capacity (%d notes)! Truncating.\n", MAX_NOTES);
                    break;
                }

                _notes[_noteCount].pitch       = n["n"];
                _notes[_noteCount].startTimeMs = n["t"];
                _notes[_noteCount].durationMs  = n["d"];
                _notes[_noteCount].velocity    = n["v"];
                _noteCount++;
            }

            Serial.printf("[MidiData] Successfully loaded %d notes \n from %s.\n", _noteCount, url.c_str());
            success = true;
        } else {
            Serial.printf("[MidiData] JSON parse error: %s\n", error.c_str());
        }
    } else {
        Serial.printf("[MidiData] HTTP GET failed, code: %d\n", httpCode);
    }

    http.end();
    return success;
}

bool MidiData::setNotes(const NoteEvent* notesArray, size_t count) {
    if (!notesArray || count == 0) return false;

    _noteCount = 0; // Reset existing notes
    size_t copyCount = (count > MAX_NOTES) ? MAX_NOTES : count;

    // Fast memory copy into static array
    memcpy(_notes, notesArray, copyCount * sizeof(NoteEvent));
    _noteCount = copyCount;

    if (count > MAX_NOTES) {
        Serial.printf("[MidiData] Warning: Provided array truncated from %u to %u MAX_NOTES.\n", 
                      count, MAX_NOTES);
    }

    Serial.printf("[MidiData] Set %u test notes from main sketch.\n", _noteCount);
    return true;
}

const NoteEvent* MidiData::getNote(size_t index) const {
    if (index < _noteCount) {
        return &_notes[index];
    }
    return nullptr;
}

void MidiData::printSummary() const {
    Serial.printf("\n--- STORED NOTE DATA (%d / %d MAX) ---\n", _noteCount, MAX_NOTES);
    char noteBuf[6];

    Serial.printf(" Time  | Note | Duration | Vel,  first five notes\n");
    for (size_t i = 0; i < 5; i++) { // all notes up to _noteCount
        const NoteEvent& n = _notes[i];
        getNoteName(n.pitch, noteBuf, sizeof(noteBuf));

        Serial.printf(" %4u, %6u, %4u, %d\n",
                      n.pitch, n.startTimeMs, n.durationMs, n.velocity);
    }
    Serial.println("----------------------------------------\n");
}