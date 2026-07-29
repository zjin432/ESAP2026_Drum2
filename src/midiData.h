#ifndef MIDI_DATA_H
#define MIDI_DATA_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // must install ArduinoJson library

// Set maximum static note capacity per ESP32
#define MAX_NOTES 3000 

// Compact 8-byte Packed Note Structure
struct __attribute__((packed)) NoteEvent {
    uint8_t  pitch;         // 1 byte
    uint32_t startTimeMs;   // 4 bytes
    uint16_t durationMs;    // 2 bytes
    uint8_t  velocity;      // 1 byte
};

class MidiData {
public:
    MidiData(const char* serverIp, uint16_t serverPort = 5000);
    
    bool setNotes(const NoteEvent* notesArray, size_t count);
    bool fetchByTrackName(const char* songName, const char* trackName);
    bool fetchByTrackIndex(const char* songName, int trackIndex);
    void loadTestNotes();

    // Data Accessors
    size_t getNoteCount() const { return _noteCount; }
    size_t getMaxCapacity() const { return MAX_NOTES; }
    const NoteEvent* getNote(size_t index) const;
    void clear() { _noteCount = 0; }

    void printSummary() const;
    static void getNoteName(uint8_t pitch, char* buffer, size_t bufSize);

private:
    const char* _serverIp;
    uint16_t _serverPort;

    // Static memory allocation -- better than dynamic allocation for embedded applications
    NoteEvent _notes[MAX_NOTES]; 
    size_t _noteCount;

    String urlEncode(const String& str);
    bool fetchFromUrl(const String& url);
};

#endif