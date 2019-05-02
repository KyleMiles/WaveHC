/**
 * \file
 * WaveHC class
 */

/**
 * If nonzero, optimize the player for contiguous files.  It takes
 * longer to open a file but can play contiguous files at higher rates.
 * Disable if you need minimum latency for open.  Also see open by index.
 */
#define OPTIMIZE_CONTIGUOUS 1

 /**
  * Software volume control should be compatible with Ladyada's library.
  * Uses shift to decrease volume by 6 dB per step. See DAC ISR in WaveHC.cpp.
  * Must be set after call to WaveHC::create().
  * Decreases MAX_CLOCK_RATE to 22050.
  */
#define DVOLUME 0

/**
 * Set behavior for files that exceed MAX_CLOCK_RATE or MAX_BYTE_RATE.
 * If RATE_ERROR_LEVEL = 2, rate too high errors are fatal.
 * If RATE_ERROR_LEVEL = 1, rate too high errors are warnings.
 * If RATE_ERROR_LEVEL = 0, rate too high errors are ignored.
 */
#define RATE_ERROR_LEVEL 2

/**
 * Set attributes for music files
 * Cannot play multiple files together if they don't all have the same bit-rate
 */
#define GLOBAL_BIT_DEPTH 16
#if GLOBAL_BIT_DEPTH != 8 && GLOBAL_BIT_DEPTH != 16
#error GLOBAL_BIT_DEPTH must be either 8 or 16
#endif

#define GLOBAL_CHANNELS 1
#if GLOBAL_CHANNELS != 1 && GLOBAL_CHANNELS != 2
#error GLOBAL_CHANNELS must be either mono or stereo
#endif

#define GLOBAL_SAMPLERATE 16000

//------------------------------------------------------------------------------
// Set the size for wave data buffers.  Must be 256 or 512.
#if defined(__AVR_ATmega168P__) || defined(__AVR_ATmega168__)

/** Buffer length for for 168 Arduino. */
#define PLAYBUFFLEN 256UL
#else // __AVR_ATmega168P__

/** Buffer length for Arduinos other than 168. */
#define PLAYBUFFLEN 512UL
#endif //__AVR_ATmega168P__

// Define max allowed SD read rate in bytes/sec.
#if PLAYBUFFLEN == 512UL && OPTIMIZE_CONTIGUOUS
/** Maximum SD read rate for 512 byte buffer and contiguous file */
#define MAX_BYTE_RATE 88200
#else // MAX_BYTE_RATE
/** Maximum SD read rate for 256 byte buffer or fragmented file */
#define MAX_BYTE_RATE 44100
#endif // MAX_BYTE_RATE

// Define maximum clock rate for DAC.
#if !DVOLUME
/** maximum DAC clock rate */
#define MAX_CLOCK_RATE 44100
#else // DVOLUME
/** Decreased clock rate if volume control is used */
#define MAX_CLOCK_RATE 22050
#endif //DVOLUME

#if GLOBAL_SAMPLERATE*GLOBAL_CHANNELS > MAX_CLOCK_RATE
#error GLOBAL_CHANNELS must be either mono or stereo
#endif

//------------------------------------------------------------------------------
/**
 * Choose which library to use (Player or WaveHC)
 * They define the same interrupts, so only one can be active at a time
 */
#define USE_WAVE_HC 0