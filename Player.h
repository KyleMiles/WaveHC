#pragma once

#define PLAYBUFFLEN 128UL

#include <Settings.h>
#include <FatReader.h>
#include <WaveUtil.h>

#define CHANNEL_COUNT 8

// status of sd
#define SD_READY 1     ///< buffer is ready to be played
#define SD_FILLING 2   ///< buffer is being filled from DS


struct File
{
  File() : active(false), index(-1), file_reader(FatReader()), remainingBytesInChunk(0) { }

  bool active;
  int16_t index;
  FatReader file_reader;
  uint32_t remainingBytesInChunk;
};


class Player
{
 public:
  Player();
  ~Player();

  bool play(const char* const filename);
  void toggle_mute();

  bool is_playing() const;
  bool has_error() const;

  void dac_handler();
  void sd_handler();

 private:
  uint8_t sdstatus = 0;
  uint8_t buffer1[PLAYBUFFLEN];
  uint8_t buffer2[PLAYBUFFLEN];
  uint8_t buffer3[PLAYBUFFLEN];

  bool mute = false;

  int16_t channel_top = 0;
  File* channels[CHANNEL_COUNT];
  void decrement_channel_index(int16_t reference_index);

  FatReader find_and_load(const char* const filename);
  bool verify_file(FatReader& f);

  int16_t readWaveData(uint8_t *buff, uint16_t len, File* const file);

  // SD and DAC buffer stuff
  uint8_t* playend;  // End position for current buffer
  uint8_t* playpos;  // Position of next sample
  uint8_t* sdbuff;   // SD fill buffer
  uint8_t* sdend;    // End of data in sd buffer

  uint8_t volume;

  SdReader card;    // This object holds the information for the card
  FatVolume vol;    // This holds the information for the partition on the card
  FatReader root;   // This holds the information for the volumes root directory
};
