//commit: git add . && git commit -m "describe what changed" && git push

#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "midiData.h"
#include "songMidi.h"

int songSelector = 0;
bool ignoreSignal = false;
bool preloadedStarted = false;
NoteEvent preloadedNotes[MAX_NOTES];

const char* downloadSsid = "GL-MT300N-V2-4e0";
const char* downloadPassword = "goodlife";
MidiData midi("192.168.8.173", 5000);
const char* songName = "Song.mid";
const char* trackName = "Drums";

const char* udpSsid = "TP-Link_8A8C";
const char* udpPassword = "12488674";
IPAddress myStaticIP(192, 168, 0, 102);
IPAddress gatewayIP(192, 168, 1, 1);
IPAddress subnetMask(255, 255, 255, 0);

unsigned int localPort = 2808;
WiFiUDP Udp;
char packetBuffer[255];

int DownloadStatus = 0;

Servo drumServo1;
Servo drumServo2;
Servo drumServo3;

int servoPin1 = 4; //Tom 1
int servoPin2 = 5; //Tom 2
int servoPin3 = 10; // bass drum

float centerAngle1 = 90; 
float swing1 = -32.0;
float restAngle1 = centerAngle1 + swing1;
float strikeAngle1 = centerAngle1 - swing1;

float centerAngle2 = 90;
float swing2 = 32.0;
float restAngle2 = centerAngle2 + swing2;
float strikeAngle2 = centerAngle2 - swing2;

// Bass drum
float centerAngle3 = 90;
float swing3 = -32.0;
float restAngle3 = centerAngle3 + swing3;
float strikeAngle3 = centerAngle3 - swing3;

unsigned long strikeHoldMsec = 80; 
float maxBeatsPerSecond = 10.0; 
unsigned long minIntervalMsec = (unsigned long)(1000.0 / maxBeatsPerSecond); // 100ms
unsigned long minStickIntervalMsec = 200; 
float maxBassBeatsPerSecond = 4.0; 
unsigned long minBassIntervalMsec = (unsigned long)(1000.0 / maxBassBeatsPerSecond); // 500ms
unsigned long strikeDelayMsec1 = 100; 
unsigned long strikeDelayMsec2 = 100; 
unsigned long strikeDelayMsec3 = 95;

bool repeat = false;
long firstBeatDelayMsec = 1000; 

const int maxSongNotes = 1000;

bool isTomNote(int note) {
  return note == 45 || note == 47 || note == 48 || note == 50;
}

bool isBassNote(int note) {
  return note == 35 || note == 36;
}

unsigned long stick1Times[maxSongNotes];
int stick1Count = 0;
unsigned long stick2Times[maxSongNotes];
int stick2Count = 0;
unsigned long stick3Times[maxSongNotes];
int stick3Count = 0;

void loadPreloadedSong(int selector) {
  if (selector < 1 || selector > numSongs) {
    Serial.println("Invalid songSelector - ignoring");
    return;
  }
  const SongInfo& song = songs[selector - 1];
  Serial.print("Loading preloaded song: ");
  Serial.println(song.name);

  for (size_t i = 0; i < song.noteCount; i++) {
    preloadedNotes[i].pitch = (uint8_t)song.notes[i];
    preloadedNotes[i].startTimeMs = (uint32_t)song.startMs[i];
    preloadedNotes[i].durationMs = (uint16_t)(song.endMs[i] - song.startMs[i]);
    preloadedNotes[i].velocity = (uint8_t)song.velocities[i];
  }
  midi.setNotes(preloadedNotes, song.noteCount);
}

void buildPlaylists() {
  stick1Count = 0;
  stick2Count = 0;
  stick3Count = 0;

  size_t noteCount = midi.getNoteCount();
  if (noteCount == 0) {
    Serial.println("buildPlaylists: no notes loaded - nothing to play");
    return;
  }

  long timelineOffsetMs = firstBeatDelayMsec;

  bool nextTomGoesToStick1 = true;
  for (size_t i = 0; i < noteCount; i++) {
    const NoteEvent* note = midi.getNote(i);
    int pitch = note->pitch;
    unsigned long noteTime = note->startTimeMs + timelineOffsetMs;

    if (isTomNote(pitch)) {
      if (nextTomGoesToStick1) {
        if (stick1Count < maxSongNotes) {
          stick1Times[stick1Count++] = noteTime;
        }
        else {
          Serial.println("buildPlaylists: stick1 playlist full - dropping note");
        }
      }
      else {
        if (stick2Count < maxSongNotes) {
          stick2Times[stick2Count++] = noteTime;
        }
        else {
          Serial.println("buildPlaylists: stick2 playlist full - dropping note");
        }
      }
      nextTomGoesToStick1 = !nextTomGoesToStick1;
    }
    else if (isBassNote(pitch)) {
      if (stick3Count < maxSongNotes) {
        stick3Times[stick3Count++] = noteTime;
      }
      else {
        Serial.println("buildPlaylists: bass drum playlist full - dropping note");
      }
    }
  }
}

#define WAITINGTOSTART 0
#define PREP 1
#define PLAYING 2
#define ENDNOTE 3
#define DONE 4

int state1 = WAITINGTOSTART;
int noteIndex1 = 0;
unsigned long currentNextTime1 = 0;
unsigned long strikeStartTime1 = 0;
unsigned long lastStrikeTime1 = 0;

int state2 = WAITINGTOSTART;
int noteIndex2 = 0;
unsigned long currentNextTime2 = 0;
unsigned long strikeStartTime2 = 0;
unsigned long lastStrikeTime2 = 0;

int state3 = WAITINGTOSTART;
int noteIndex3 = 0;
unsigned long currentNextTime3 = 0;
unsigned long strikeStartTime3 = 0;
unsigned long lastStrikeTime3 = 0;

unsigned long lastTomStrikeTime = 0;

unsigned long waitingTime = 0;

#define TESTNOTE_IDLE 0
#define TESTNOTE_WAITING 1
#define TESTNOTE_STRIKING 2

int testNoteState = TESTNOTE_IDLE;
unsigned long testNoteTriggerTime = 0;
unsigned long testNoteStrikeStartTime = 0;

bool globalTomCooldownOk(unsigned long now) {
  return (lastTomStrikeTime == 0 || now - lastTomStrikeTime >= minIntervalMsec);
}

bool isPlaying1(unsigned long now, unsigned long currentTime) {
  if (noteIndex1 >= stick1Count || currentTime < currentNextTime1 || lastStrikeTime1 != 0 && now - lastStrikeTime1 < minStickIntervalMsec || !globalTomCooldownOk(now)) {
    return false;
  }
  return true;
}

void PlayNote1(unsigned long now) {
  drumServo1.write(strikeAngle1);
  strikeStartTime1 = now;
  lastStrikeTime1 = now;
  lastTomStrikeTime = now;
  state1 = PLAYING;
}

bool isStopping1(unsigned long now) {
  return now - strikeStartTime1 >= strikeHoldMsec;
}

void StopNote1() {
  drumServo1.write(restAngle1);
  state1 = ENDNOTE;
}

void PrepNote1() {
  noteIndex1++;
  if (noteIndex1 < stick1Count) {
    unsigned long target = stick1Times[noteIndex1];
    currentNextTime1 = (target > strikeDelayMsec1) ? (target - strikeDelayMsec1) : 0;
  }
  state1 = PREP;
}

bool isPlaying2(unsigned long now, unsigned long currentTime) {
  if (noteIndex2 >= stick2Count || currentTime < currentNextTime2 || lastStrikeTime2 != 0 && now - lastStrikeTime2 < minStickIntervalMsec || !globalTomCooldownOk(now)) {
    return false;
  }
  return true;
}

void PlayNote2(unsigned long now) {
  drumServo2.write(strikeAngle2);
  strikeStartTime2 = now;
  lastStrikeTime2 = now;
  lastTomStrikeTime = now;
  state2 = PLAYING;
}

bool isStopping2(unsigned long now) {
  return now - strikeStartTime2 >= strikeHoldMsec;
}

void StopNote2() {
  drumServo2.write(restAngle2);
  state2 = ENDNOTE;
}

void PrepNote2() {
  noteIndex2++;
  if (noteIndex2 < stick2Count) {
    unsigned long target = stick2Times[noteIndex2];
    currentNextTime2 = (target > strikeDelayMsec2) ? (target - strikeDelayMsec2) : 0;
  }
  state2 = PREP;
}

bool isPlaying3(unsigned long now, unsigned long currentTime) {
  if (noteIndex3 >= stick3Count || currentTime < currentNextTime3 || lastStrikeTime3 != 0 && now - lastStrikeTime3 < minBassIntervalMsec) {
    return false;
  }
  return true;
}

void PlayNote3(unsigned long now) {
  drumServo3.write(strikeAngle3);
  strikeStartTime3 = now;
  lastStrikeTime3 = now;
  state3 = PLAYING;
}

bool isStopping3(unsigned long now) {
  return now - strikeStartTime3 >= strikeHoldMsec;
}

void StopNote3() {
  drumServo3.write(restAngle3);
  state3 = ENDNOTE;
}

void PrepNote3() {
  noteIndex3++;
  if (noteIndex3 < stick3Count) {
    unsigned long target = stick3Times[noteIndex3];
    currentNextTime3 = (target > strikeDelayMsec3) ? (target - strikeDelayMsec3) : 0;
  }
  state3 = PREP;
}

void startPiece(unsigned long now) {
  waitingTime = now;
  noteIndex1 = 0;
  noteIndex2 = 0;
  noteIndex3 = 0;
  currentNextTime1 = (stick1Count > 0 && stick1Times[0] > strikeDelayMsec1) ? (stick1Times[0] - strikeDelayMsec1) : 0;
  currentNextTime2 = (stick2Count > 0 && stick2Times[0] > strikeDelayMsec2) ? (stick2Times[0] - strikeDelayMsec2) : 0;
  currentNextTime3 = (stick3Count > 0 && stick3Times[0] > strikeDelayMsec3) ? (stick3Times[0] - strikeDelayMsec3) : 0;
  state1 = PREP;
  state2 = PREP;
  state3 = PREP;
}

void startTestNote(unsigned long now) {
  unsigned long targetImpactTime = now + 1000;
  testNoteTriggerTime = (targetImpactTime > strikeDelayMsec1) ? (targetImpactTime - strikeDelayMsec1) : now;
  testNoteState = TESTNOTE_WAITING;
}

int checkUDP() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }
    if (packetBuffer[0] == 'T') {
      return 1;
    }
    if (packetBuffer[0] == 'G') {
      return 2;
    }
  }
  return 0;
}

void setup() {
  drumServo1.setPeriodHertz(50);
  drumServo1.attach(servoPin1, 500, 2400);
  drumServo1.write(restAngle1);

  drumServo2.setPeriodHertz(50);
  drumServo2.attach(servoPin2, 500, 2400);
  drumServo2.write(restAngle2);

  drumServo3.setPeriodHertz(50);
  drumServo3.attach(servoPin3, 500, 2400);
  drumServo3.write(restAngle3);

  Serial.begin(115200);
  delay(1000);
}

void loop() {
  unsigned long now = millis();
  unsigned long currentTime = now - waitingTime;

  if (songSelector == 0) {
    if (DownloadStatus == 0) {
      WiFi.begin(downloadSsid, downloadPassword);
      Serial.print("Connecting to download WiFi");
      while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
      }
      Serial.println();
      Serial.println("Download WiFi connected");

      Serial.print("Fetching ");
      Serial.print(songName);
      Serial.print(" track ");
      Serial.println(trackName);
      midi.fetchByTrackName(songName, trackName);

      buildPlaylists();

      Serial.print("Stick 1 playlist: ");
      Serial.print(stick1Count);
      Serial.println(" notes");
      Serial.print("Stick 2 playlist: ");
      Serial.print(stick2Count);
      Serial.println(" notes");
      Serial.print("Bass drum playlist: ");
      Serial.print(stick3Count);
      Serial.println(" notes");

      DownloadStatus = ignoreSignal ? 2 : 1;
    }

    if (DownloadStatus == 1) {
      WiFi.disconnect();
      delay(200);
      WiFi.config(myStaticIP, gatewayIP, subnetMask);
      WiFi.begin(udpSsid, udpPassword);

      Serial.print("Connecting to UDP command WiFi");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println();
      Serial.println("UDP command WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      Udp.begin(localPort);
      Serial.print("Listening for UDP start/sync signal on port ");
      Serial.println(localPort);
      Serial.println("Waiting for 'G' to start the piece ('T' to test sync)...");

      DownloadStatus = 2;
    }

    if (DownloadStatus != 2) {
      return; // still connecting - skip the rest of loop() this pass
    }

    if (ignoreSignal) {
      if (state1 == WAITINGTOSTART) {
        Serial.println("ignoreSignal enabled - starting piece automatically");
        startPiece(now);
      }
    }
    else {
      int msg = checkUDP();
      if (msg == 1) {
        Serial.println("Received 'T' - test sync note in ~1s");
        startTestNote(now);
      }
      else if (msg == 2) {
        Serial.println("Received 'G' - starting piece");
        startPiece(now);
      }
    }
  }
  else {
    if (!preloadedStarted) {
      Serial.print("songSelector = ");
      Serial.print(songSelector);
      Serial.println(" - loading preloaded song, skipping WiFi");
      loadPreloadedSong(songSelector);
      buildPlaylists();

      Serial.print("Stick 1 playlist: ");
      Serial.print(stick1Count);
      Serial.println(" notes");
      Serial.print("Stick 2 playlist: ");
      Serial.print(stick2Count);
      Serial.println(" notes");
      Serial.print("Bass drum playlist: ");
      Serial.print(stick3Count);
      Serial.println(" notes");

      startPiece(now);
      currentTime = now - waitingTime;
      preloadedStarted = true;
    }
  }

  if (testNoteState == TESTNOTE_WAITING && now >= testNoteTriggerTime) {
    drumServo1.write(strikeAngle1);
    testNoteStrikeStartTime = now;
    testNoteState = TESTNOTE_STRIKING;
  }
  if (testNoteState == TESTNOTE_STRIKING && now - testNoteStrikeStartTime >= strikeHoldMsec) {
    drumServo1.write(restAngle1);
    testNoteState = TESTNOTE_IDLE;
  }

  if (state1 == PREP && isPlaying1(now, currentTime)) {
    PlayNote1(now);
  }
  if (state1 == PLAYING && isStopping1(now)) {
    StopNote1();
  }
  if (state1 == ENDNOTE) {
    PrepNote1();
  }

  if (state2 == PREP && isPlaying2(now, currentTime)) {
    PlayNote2(now);
  }
  if (state2 == PLAYING && isStopping2(now)) {
    StopNote2();
  }
  if (state2 == ENDNOTE) {
    PrepNote2();
  }

  if (state3 == PREP && isPlaying3(now, currentTime)) {
    PlayNote3(now);
  }
  if (state3 == PLAYING && isStopping3(now)) {
    StopNote3();
  }
  if (state3 == ENDNOTE) {
    PrepNote3();
  }

  if (state1 != DONE && state1 != WAITINGTOSTART &&
      noteIndex1 >= stick1Count && noteIndex2 >= stick2Count && noteIndex3 >= stick3Count) {
    if (repeat) {
      Serial.println("Piece finished - repeating.");
      startPiece(now);
    } 
    else {
      state1 = DONE;
      state2 = DONE;
      state3 = DONE;
      drumServo3.write(restAngle3);
      drumServo3.detach();
      Serial.println("Piece finished.");
    }
  }
}