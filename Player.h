#pragma once

#include <Settings.h>
#include <FatReader.h>
#include <WaveUtil.h>

#define CHANNEL_COUNT 16


struct File
{
  File() : active(false), index(-1), file(nullptr), remainingBytesInChunk(0) { }

  bool active;
  short index;
  FatReader* file;
  uint32_t remainingBytesInChunk;
};


class Player
{
 public:
  Player();

  bool play(const char* const filename);
  void toggle_mute();

  bool is_playing() const;
  bool has_error() const;

  void dac_handler();
  void sd_handler();

 private:
  bool mute = false;

  short channel_top = 0;
  File* channels[CHANNEL_COUNT];
  void decrement_channel_index(short reference_index);

  FatReader* find_and_load(const char* const filename);
  bool verify_file(FatReader* const f);

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
