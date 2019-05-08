#if ARDUINO < 100
#include <WProgram.h>
#else  // ARDUINO
#include <Arduino.h>
#endif  // ARDUINO

#include <Player.h>
#include <mcpDac.h>


namespace WavePlayer
{
  Player* player = nullptr;
}


// Logging
void sdErrorCheck(const SdReader& card)
{
  if (!card.errorCode()) return;
  PgmPrint("\r\nSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  PgmPrint(", ");
  Serial.println(card.errorData(), HEX);
  while(1);
}

void error_P(const char *str, const SdReader& card)
{
  PgmPrint("Error: ");
  SerialPrint_P(str);
  sdErrorCheck(card);
  while(1);
}

void error_log(const char *str)
{
  PgmPrint("Error: ");
  SerialPrint_P(str);
  while(1);
}

#define error(msg, card) error_P(PSTR(msg), card)


// Player:
Player::Player()
{
  if (!Serial)
    Serial.begin(115200);

  if (WavePlayer::player != nullptr)
    error_log("There can only be one player!!");

  WavePlayer::player = this;

  if (!card.init())
    error("Card init. failed!", card);  // Something went wrong, lets print out why

  card.partialBlockRead(true);

  // Now we will look for a FAT partition!
  uint8_t part;
  for (part = 0; part < 5; part++) {   // we have up to 5 slots to look in
    if (vol.init(card, part)) 
      break;
  }
  if (part == 5)
    error("No valid FAT partition!", card);

  // Try to open the root directory
  if (!root.openRoot(vol))
    error("Can't open root dir!", card);

  for (int i = 0; i < CHANNEL_COUNT; ++i)
    channels[i] = new File();
  
  // Setup Buffers
  playpos = buffer1;
  playend = buffer1;

  sdbuff = buffer2;
  sdend = sdbuff;
  
  sdstatus = SD_READY;

  // Setup mode for DAC ports
  mcpDacInit();
  
  // Set up timer one
  // Normal operation - no pwm not connected to pins
  TCCR1A = 0;
  // no prescaling, CTC mode
  TCCR1B = _BV(WGM12) | _BV(CS10); 
  // Sample rate - play stereo interleaved
  OCR1A =  F_CPU / (GLOBAL_SAMPLERATE*GLOBAL_CHANNELS);
  // SD fill interrupt happens at TCNT1 == 1
  OCR1B = 1;
}

Player::~Player()
{
  for (int i = 0; i < CHANNEL_COUNT; ++i)
    delete channels[i];
  
  WavePlayer::player = nullptr;
}

bool Player::play(const char* const filename)
{
  // I promise to give you a slot, so it shouldn't hurt to verify the file before or after choose that slot
  cli();
  FatReader file = find_and_load(filename);
  if (!file.isOpen())
  {
    putstring_nl("Could not find file");
    sei();
    return false;
  }

  if (!verify_file(file))
  {
    sei();
    return false;
  }
  sei();

  File* overwrite = nullptr;

  for (int i = 0; i < CHANNEL_COUNT; ++i)
  {
    if (!channels[i]->active)
    {
      cli();
      channels[i]->active = true;
      channels[i]->index = channel_top; channel_top += 1;
      channels[i]->file_reader = file;
      channels[i]->remainingBytesInChunk = 0;
      sei();

      if (readWaveData(0, 0, channels[i]) >= 0)  // position to data
      {
        if (!mute)
          TIMSK1 |= _BV(OCIE1A);  // Enable timer interrupt for DAC ISR
        return true;
      }
      else
        return false;
    }
    else if (channels[i]->index == 0)
      overwrite = channels[i];  // Overwrite the oldest channel
  }

  decrement_channel_index(0);  // Shift all channel indexes down

  // Overwrite the overwrite
  cli();
  overwrite->index = CHANNEL_COUNT-1;
  overwrite->file_reader = file;
  overwrite->remainingBytesInChunk = 0;
  sei();

  if (!mute)
    TIMSK1 |= _BV(OCIE1A);  // Enable timer interrupt for DAC ISR
  return (readWaveData(0, 0, overwrite) >= 0);  // position to data
}

void Player::toggle_mute()
{
  mute = !mute;
  if (!mute)
    TIMSK1 |= _BV(OCIE1A);   // Turn on interrupt
  else
    TIMSK1 &= ~_BV(OCIE1A);  // Turn off interrupt
}

bool Player::is_playing() const
{
  return !mute;
}

bool Player::has_error() const
{
  if (!card.errorCode())
    return false;
  
  PgmPrint("\r\nSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  PgmPrint(", ");
  Serial.println(card.errorData(), HEX);

  return true;
}

void Player::decrement_channel_index(int16_t reference_index)
{
  for (int i = 0; i < CHANNEL_COUNT; ++i)
    if (channels[i]->index > reference_index)
      channels[i]->index -= 1;
}

FatReader _find_and_load(const char* const filename, FatReader current_path, FatVolume& vol, const SdReader& card)
{
  FatReader current_file;  // Current file
  dir_t dirBuf;  // Read buffer

  // Read every file in the directory one at a time
  while (current_path.readDir(dirBuf) > 0)
  {
    // Skip it if not a subdirectory and not the file we're looking for
    if (!DIR_IS_SUBDIR(dirBuf) && strncmp((char *)dirBuf.name, filename, 11))
      continue;

    // Try to open the file
    if (!current_file.open(vol, dirBuf))
      error("file.open failed", card);

    // Recurse if subdir, otherwise return the file
    if (current_file.isDir())
    {
      FatReader temp = _find_and_load(filename, current_file, vol, card);
      if (temp.isOpen())
        return temp;
    }
    else
      return current_file;
  }

  return FatReader();
}

FatReader Player::find_and_load(const char* const filename)
{
  return _find_and_load(filename, root, vol, card);
}

// File Verification
/**
 * Read a wave file's metadata and verify that they match global settings.
 *
 * param[in] f A open FatReader instance for the wave file.
 *
 * return The value one, true, is returned for success and
 * the value zero, false, is returned for failure.  Reasons
 * for failure include I/O error, an invalid wave file or a wave
 *  file with features that WaveHC does not support.
 */
bool Player::verify_file(FatReader& f)
{
  // 18 byte buffer
  // can use this since Arduino and RIFF are Little Endian
  union
  {
    struct
    {
      char     id[4];
      uint32_t size;
      char     data[4];
    } riff;  // riff chunk
    
    struct
    {
      uint16_t compress;
      uint16_t channels;
      uint32_t sampleRate;
      uint32_t bytesPerSecond;
      uint16_t blockAlign;
      uint16_t bitsPerSample;
      uint16_t extraBytes;
    } fmt; // fmt data
  } buf;
  
 #if OPTIMIZE_CONTIGUOUS
  // set optimized read for contiguous files
  f.optimizeContiguous();
 #endif // OPTIMIZE_CONTIGUOUS

  // must start with WAVE header
  if (f.read(&buf, 12) != 12 || strncmp(buf.riff.id, "RIFF", 4) || strncmp(buf.riff.data, "WAVE", 4))
    return false;

  // next chunk must be fmt
  if (f.read(&buf, 8) != 8 || strncmp(buf.riff.id, "fmt ", 4))
    return false;

  // fmt chunk size must be 16 or 18
  uint16_t size = buf.riff.size;
  if (size == 16 || size == 18)
    if (f.read(&buf, size) != (int16_t)size)
      return false;
  else
    buf.fmt.compress = 0;  // compressed data - force error

  // // Confirming file info
  // if (buf.fmt.compress != 1 || (size == 18 && buf.fmt.extraBytes != 0))
  // {
  //   putstring_nl("Compression not supported");
  //   return false;
  // }
  if (buf.fmt.channels != GLOBAL_CHANNELS)
  {
    putstring_nl("File's Channel Count Does Not Match Settings!");
    return false;
  }
  if (buf.fmt.bitsPerSample != GLOBAL_BIT_DEPTH)
  {
    putstring_nl("File's Bit Depth Does Not Match Settings!");
    return false;
  }
  if (buf.fmt.sampleRate != GLOBAL_SAMPLERATE)
  {
    putstring_nl("File's Sample Rate Does Not Match Settings!");
    return false;
  }

  uint32_t clockRate = buf.fmt.sampleRate*buf.fmt.channels;
  uint32_t byteRate = clockRate*buf.fmt.bitsPerSample/8;
  
 #if RATE_ERROR_LEVEL > 0
  if (clockRate > MAX_CLOCK_RATE || byteRate > MAX_BYTE_RATE)
  {
    putstring_nl("Sample rate too high!");
    if (RATE_ERROR_LEVEL > 1)
      return false;
  }
  else if (byteRate > 44100 && !f.isContiguous())
  {
    putstring_nl("High rate fragmented file!");
    if (RATE_ERROR_LEVEL > 1)
      return false;
  }
 #endif // RATE_ERROR_LEVEL > 0

  return true;
}

int16_t Player::readWaveData(uint8_t *buff, uint16_t len, File* const file)
{
  if (file->remainingBytesInChunk == 0)
  {
    struct
    {
      char     id[4];
      uint32_t size;
    } header;

    while (1)
    {
      if (file->file_reader.read(&header, 8) != 8)
        return -1;

      if (!strncmp(header.id, "data", 4))
      {
        file->remainingBytesInChunk = header.size;
        break;
      }
 
      // if not "data" then skip it!
      if (!file->file_reader.seekCur(header.size))
        return -1;
    }
  }

  // make sure buffers are aligned on SD sectors
  uint16_t maxLen = len - file->file_reader.readPosition() % len;
  if (len > maxLen)
    len = maxLen;
  if (len > file->remainingBytesInChunk)
    len = file->remainingBytesInChunk;

  int16_t ret = file->file_reader.read(buff, len);
  if (ret > 0)
    file->remainingBytesInChunk -= ret;
  return ret;
}

// Interupts:
#if USE_WAVE_HC == 0
  ISR(TIMER1_COMPA_vect)  // timer interrupt for DAC
  {
    if (WavePlayer::player == nullptr)
      return;

    WavePlayer::player->dac_handler();
  }

  ISR(TIMER1_COMPB_vect) // this is the interrupt that fills the playbuffer
  {
    // turn off calling interrupt
    TIMSK1 &= ~_BV(OCIE1B);

    WavePlayer::player->sd_handler();
  }
#endif

inline void send_DAC(const uint8_t dh, const uint8_t dl)
{
  // dac chip select low
  mcpDacCsLow();
  
  // send DAC config bits
  mcpDacSdiLow();
  mcpDacSckPulse();  // DAC A
  mcpDacSckPulse();  // unbuffered
  mcpDacSdiHigh();
  mcpDacSckPulse();  // 1X gain
  mcpDacSckPulse();  // no SHDN
  
  // send high 8 bits
  mcpDacSendBit(dh,  7);
  mcpDacSendBit(dh,  6);
  mcpDacSendBit(dh,  5);
  mcpDacSendBit(dh,  4);
  mcpDacSendBit(dh,  3);
  mcpDacSendBit(dh,  2);
  mcpDacSendBit(dh,  1);
  mcpDacSendBit(dh,  0);
  
  // send low 4 bits
  mcpDacSendBit(dl,  7);
  mcpDacSendBit(dl,  6);
  mcpDacSendBit(dl,  5);
  mcpDacSendBit(dl,  4);
  
  // chip select high - done
  mcpDacCsHigh();
}

void Player::dac_handler()
{
  if (channel_top <= 0)
    return;

  if (playpos >= playend)
  {
    if (sdstatus != SD_READY)
      return;

    // swap double buffers
    playpos = sdbuff;
    playend = sdend;
    if (sdbuff != buffer1)
      sdbuff = buffer1;
    else
      sdbuff = buffer2;

    // interrupt to call SD reader
    sdstatus = SD_FILLING;
    TIMSK1 |= _BV(OCIE1B);
  }

  uint8_t dh, dl;
 #if GLOBAL_BIT_DEPTH == 16  // 16-bit is signed
  #if DVOLUME
   uint16_t tmp = (((0X80 ^ playpos[1]) << 8) | playpos[0]) >> WavePlayer::player->volume;
   dh = tmp >> 8;
   dl = tmp;
  #else
   dh = 0X80 ^ playpos[1];
   dl = playpos[0];
  #endif
  playpos += 2;

 #elif GLOBAL_BIT_DEPTH == 8  // 8-bit is unsigned
  #if DVOLUME
   uint16_t tmp = playpos[0] << (8 - playing->volume);
   dh = tmp >> 8;
   dl = tmp;
  #else
   dh = playpos[0];
   dl = 0;
  #endif
  playpos++;
 #endif

  send_DAC(dh, dl);
}

void Player::sd_handler()
{
  if (sdstatus != SD_FILLING)
    return;

  sdend = sdbuff;
  bool first_write = true;

  for (int channel_index = 0; channel_index < CHANNEL_COUNT; ++channel_index)
  {
    if (!channels[channel_index]->active)
      continue;

    sei();  // enable interrupts while reading the SD
    int16_t read_data = readWaveData(buffer3, PLAYBUFFLEN, channels[channel_index]);
    // int16_t read_data = readWaveData(sdbuff, PLAYBUFFLEN, channels[channel_index]);

    if (read_data > 0)
    {
      if (sdend-read_data < sdbuff)  // If we read more than what's already in the buffer
        sdend += read_data;

      if (first_write)
      {
        first_write = false;
        
        for (int buffer_offset = 0; buffer_offset < read_data; buffer_offset+=2)
        {
          if (buffer_offset < read_data)
            *((int16_t*)(sdbuff+buffer_offset)) = *((int16_t*)(buffer3+buffer_offset)); // Copy Over data
            // *((int16_t*)(sdbuff+buffer_offset)) = (*((int16_t*)(buffer3+buffer_offset)))/channel_top; // Copy Over data
          else
            sdbuff[buffer_offset] = 0x00; // Clear buffer to zero
        }
      }
      else
        for (int buffer_offset = 0; buffer_offset < read_data; buffer_offset+=2)
          *((int16_t*)(sdbuff+buffer_offset)) += *((int16_t*)(buffer3+buffer_offset)); // Copy Over data
          // *((int16_t*)(sdbuff+buffer_offset)) += (*((int16_t*)(buffer3+buffer_offset)))/channel_top; // Copy Over data
      
      // cli();
      // sdstatus = SD_READY;
      // return;
    }
    else
    {
      channels[channel_index]->active = false;

      channels[channel_index]->file_reader = FatReader();

      decrement_channel_index(channels[channel_index]->index);
      channels[channel_index]->index = -1;
      channel_top -= 1;

      channels[channel_index]->remainingBytesInChunk = 0;
    }
  }
  sdstatus = SD_READY;
  cli();
}
