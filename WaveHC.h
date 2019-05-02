/*!
 * @file WaveHC.h
 *
 * This library is a highly modified version of Ladyada's Wave Shield library.
 * I have made many changes that may have introduced bugs.  
 * 
 * 
 * Adafruit invests time and resources providing this open source code, 
 * please support Adafruit and open-source hardware by purchasing 
 * products from Adafruit!
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */

#ifndef WaveHC_h
#define WaveHC_h
#include <FatReader.h>
#include <Settings.h>
/**
 * \file
 * WaveHC class
 */

//------------------------------------------------------------------------------
/**
 * \class WaveHC
 * \brief Wave file player.
 *
 * Play wave files from FAT16 and FAT32 file systems
 * on SD and SDHC flash memory cards.
 *
 */
class WaveHC {
public:
  /** Wave file number of channels. Mono = 1, Stereo = 2 */
  uint8_t Channels;
  /** Wave file sample rate. Must be not greater than 44100/sec. */
  uint32_t dwSamplesPerSec;
  /** Wave file bits per sample.  Must be 8 or 16. */
  uint8_t BitsPerSample;
  /** Remaining bytes to be played in Wave file data chunk. */
  uint32_t remainingBytesInChunk;
  /** Has the value true if a wave file is playing else false. */
  volatile uint8_t isplaying;
  /** Number of times data was not available from the SD in the DAC ISR */
  uint32_t errors;

#if DVOLUME
  /** Software volume control. Reduce volume by 6 dB per step. See DAC ISR. */
  uint8_t volume;
#endif // DVOLUME
  /** FatReader instance for current wave file. */
  FatReader* fd;
  
  WaveHC(void);
  uint8_t create(FatReader &f);
  /*!
   @brief Return the size of the WAV file 
   @returns the size of the WAV file
  */
  uint32_t getSize(void) {return fd->fileSize();}
  uint8_t isPaused(void);
  void pause(void);
  void play(void);
  int16_t readWaveData(uint8_t *buff, uint16_t len);
  void resume(void);
  void seek(uint32_t pos);
  void setSampleRate(uint32_t samplerate);
  void stop(void);
};

#endif //WaveHC_h
