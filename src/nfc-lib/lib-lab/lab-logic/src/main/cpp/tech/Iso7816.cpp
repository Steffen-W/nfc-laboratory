/*

  This file is part of NFC-LABORATORY.

  Copyright (C) 2024 Jose Vicente Campos Martinez, <josevcm@gmail.com>

  NFC-LABORATORY is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  NFC-LABORATORY is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with NFC-LABORATORY. If not, see <http://www.gnu.org/licenses/>.

*/

#include <cmath>
#include <cstring>
#include <functional>

#include <rt/Logger.h>

#include <lab/iso/Iso.h>

#include <tech/Iso7816.h>

#define SEARCH_MODE_RESET 0
#define SEARCH_MODE_SYNC 1
#define SEARCH_MODE_TS 2
#define SEARCH_MODE_ATR 3
#define SEARCH_MODE_T0 4

#define CH_IO 0
// #define CH_CLK 1
#define CH_RST 1
#define CH_VCC 2

#define ATR_MIN_LEN 2
#define ATR_MAX_LEN 32

#define ATR_TA_MASK 0x10
#define ATR_TB_MASK 0x20
#define ATR_TC_MASK 0x40
#define ATR_TD_MASK 0x80

#define PROTO_T0 0
#define PROTO_T1 1

#define PPS_MIN_LEN 3
#define PPS_MAX_LEN 6
#define PPS_CMD 0xFF

#define PPS_PPS1_MASK 0x10
#define PPS_PPS2_MASK 0x20
#define PPS_PPS3_MASK 0x40

#define T0_TPDU_MIN_LEN 5
#define T0_TPDU_MAX_LEN 255
#define T0_TPDU_CLA_OFFSET 0
#define T0_TPDU_INS_OFFSET 1
#define T0_TPDU_P1_OFFSET 2
#define T0_TPDU_P2_OFFSET 3
#define T0_TPDU_P3_OFFSET 4
#define T0_TPDU_PROC_OFFSET 5

#define T1_BLOCK_MIN_LEN 3
#define T1_BLOCK_MAX_LEN 255
#define T1_BLOCK_NAD_OFFSET 0
#define T1_BLOCK_PCB_OFFSET 1
#define T1_BLOCK_LEN_OFFSET 2
#define T1_BLOCK_INF_OFFSET 3

#define GT_THRESHOLD 0.5
#define WT_THRESHOLD 0.5

namespace lab {

enum SymbolType
{
   IncompleteSymbol = -1,
   TimeoutSymbol = 0,
   FullSymbol = 1,
   PowerLowSymbol = 8,
   ResetLowSymbol = 9,
};

enum CharacterType
{
   IncompleteCharacter = -1,
   TimeoutCharacter = 0,
   FullCharacter = 1,
   PowerLowCharacter = 8,
   ResetLowCharacter = 9
};

enum ConventionType
{
   DirectConvention = 1,
   InverseConvention = 2,
};

enum ResultType
{
   ResultInvalid = -1,
   ResultSuccess = 0,
   ResultFailed = 1,
};

enum RedundancyType
{
   LRCCode = 0,
   CRCCode = 1,
};

/*
 * status for protocol
 */
struct IsoProtocolStatus
{
   // type of protocol T0/T1/T2/T3/T4
   unsigned int protocolType;

   // error detection code to be used
   unsigned int errorCodeType;

   // modulation convention
   unsigned int symbolConvention;

   // frequency of clock signal
   double clockFrequency;

   // number of bits per second
   double symbolsPerSecond;

   // duration of one moment on the electrical circuit I/O (one bit) = 1 ETU
   double elementaryTimeUnit;

   // duration of one moment on the electrical circuit I/O (one bit), in samples
   double elementaryTime;

   // duration of half moment on the electrical circuit I/O (1 /2 bit), in samples
   double elementaryHalfTime;

   // index for clock rate conversion integer Fi
   unsigned int frequencyFactorIndex;

   // value for clock rate conversion integer Fn
   unsigned int frequencyFactor;

   // index for baud rate adjustment integer Di
   unsigned int baudRateFactorIndex;

   // value for baud rate adjustment integer Dn
   unsigned int baudRateFactor;

   // Extra time to add to GT when transmitting characters from device to card, in ETUs
   unsigned int extraGuardTimeUnits;

   // Extra time to add to GT when transmitting characters from device to card, in samples
   unsigned int extraGuardTime;

   // The CGT is the minimum delay between the leading edges of two consecutive characters, in ETUs
   unsigned int characterGuardTimeUnits;

   // The CGT is the minimum delay between the leading edges of two consecutive characters, in samples
   unsigned int characterGuardTime;

   // The CWT is the maximum delay between leading edge of two consecutive characters, in ETUs
   unsigned int characterWaitingTimeUnits;

   // The CWT is the maximum delay between leading edge of two consecutive characters, in samples
   unsigned int characterWaitingTime;

   // The BGT is the minimum delay between the leading edges of two consecutive blocks, in ETUs
   unsigned int blockGuardTimeUnits;

   // The BGT is the minimum delay between the leading edges of two consecutive blocks, in samples
   unsigned int blockGuardTime;

   // The BWT is the maximum delay between leading edge of two consecutive blocks, in ETUs
   unsigned int blockWaitingTimeUnits;

   // The BWT is the maximum delay between leading edge of two consecutive blocks, in samples
   unsigned int blockWaitingTime;

   // The maximum size of the information field in a block
   unsigned int maximumInformationSize;

   // flag to request protocol parameters change
   bool protocolParametersChange;
};

struct Iso7816::Impl
{
   rt::Logger *log = rt::Logger::getLogger("decoder.Iso7816");

   // global signal status
   IsoDecoderStatus *decoder;

   // detected modulation status
   IsoModulationStatus modulationStatus {};

   // protocol status
   IsoProtocolStatus protocolStatus {};

   // symbol status
   IsoSymbolStatus symbolStatus {};

   // character status
   IsoCharacterStatus characterStatus {};

   // frame processing status
   IsoFrameStatus frameStatus {};

   // bitrate parameters
   IsoBitrateParams bitrateParams {};

   explicit Impl(IsoDecoderStatus *decoder) : decoder(decoder)
   {
   }

   /*
    * Configure NFC-A modulation
    */
   void initialize(unsigned int sampleRate)
   {
      log->info("--------------------------------------------");
      log->info("initializing ISO-7816 decoder");
      log->info("--------------------------------------------");

      resetModulation();
   }

   bool detect(std::list<RawFrame> &frames)
   {
      switch (modulationStatus.searchModeState)
      {
         case SEARCH_MODE_RESET:
            return detectReset(frames); // detect reset signal rise edge

         case SEARCH_MODE_SYNC:
            return detectSync(frames); // detect first and second IO fall edges to measure ETU

         case SEARCH_MODE_TS:
            return detectTS(frames); // detect TS byte and set convention

         case SEARCH_MODE_ATR:
            return detectATR(frames); // detect ATR frame and setup communication parameters
      }

      return false;
   }

   /*
    * Wait for VCC and reset line to go UP
    */
   bool detectReset(std::list<RawFrame> &frames)
   {
      float vccValue = decoder->sampleData[CH_VCC];
      float resetEdge = decoder->sampleEdge[CH_RST];

      // search reset signal rise edge
      if (vccValue > 0 && resetEdge > 0 && decoder->signalClock > 2)
      {
         modulationStatus.searchModeState = SEARCH_MODE_SYNC;
         modulationStatus.searchStartTime = decoder->signalClock;
      }

      return false;
   }

   /*
    * Search for first and second IO fall edges to detect ETU
    */
   bool detectSync(std::list<RawFrame> &frames)
   {
      float dataEdge = decoder->sampleEdge[CH_IO];
      float resetEdge = decoder->sampleEdge[CH_RST];
      float vccEdge = decoder->sampleEdge[CH_VCC];

      // if VCC or RST signal goes low, restart search from beginning
      if (vccEdge < 0 || resetEdge < 0)
      {
         resetModulation();
         return false;
      }

      // wait TC guard time before start searching for first symbol
      if (decoder->signalClock < modulationStatus.searchStartTime)
         return false;

      // detect first IO fall edge
      if (!modulationStatus.syncStartTime)
      {
         // find first data signal fall edge
         if (dataEdge < 0)
            modulationStatus.syncStartTime = decoder->signalClock;

         return false;
      }

      // detect second IO fall edge
      if (!modulationStatus.syncEndTime)
      {
         // find first data signal rise edge
         if (dataEdge < 0)
            modulationStatus.syncEndTime = decoder->signalClock;

         return false;
      }

      log->info("detected SYNC pattern, start {} end {}", {modulationStatus.syncStartTime, modulationStatus.syncEndTime});

      // initialize TS character status
      characterStatus.start = modulationStatus.syncStartTime;
      characterStatus.end = 0;
      characterStatus.bits = 3; // first 3 bits detected including START bit
      characterStatus.data = 3; // first HH pattern detected (excluding START bit)
      characterStatus.flags = 0;
      characterStatus.parity = 0;

      // assume direct convention
      protocolStatus.symbolConvention = DirectConvention;

      // update protocol timings based on current ETU samples and set default Fi/Di values
      updateProtocol((modulationStatus.syncEndTime - modulationStatus.syncStartTime) / 3.0, 0, 1);

      // configure frame timing parameters
      frameStatus.guardTime = protocolStatus.characterGuardTime - GT_THRESHOLD * protocolStatus.elementaryTime;
      frameStatus.waitingTime = protocolStatus.characterWaitingTime + WT_THRESHOLD * protocolStatus.elementaryTime;

      // switch to read remain TS character
      modulationStatus.searchModeState = SEARCH_MODE_TS;
      modulationStatus.searchSyncTime = characterStatus.start + protocolStatus.elementaryTime * 3 + protocolStatus.elementaryHalfTime;
      modulationStatus.searchStartTime = 0;
      modulationStatus.searchEndTime = 0;

      // clear synchronization status
      modulationStatus.syncStartTime = 0;
      modulationStatus.syncEndTime = 0;

      return false;
   }

   /*
    * Complete the reception of TS byte and detect convention
    */
   bool detectTS(std::list<RawFrame> &frames)
   {
      switch (decodeCharacter())
      {
         case FullCharacter:
         {
            // check TS byte to detect convention
            switch (characterStatus.data)
            {
               case 0x3B:
                  protocolStatus.symbolConvention = DirectConvention;
                  break;

               case 0x03:
                  characterStatus.data = 0x3F; // inverse convention for 0x03
                  characterStatus.parity = !characterStatus.parity; // inverse convention for parity bit
                  protocolStatus.symbolConvention = InverseConvention;
                  break;

               default:
                  log->warn("detected unknown TS 0x{02X}", {characterStatus.data});
                  resetModulation();
                  return false;
            }

            // set status and timing for receive remain ATR bytes
            modulationStatus.searchModeState = SEARCH_MODE_ATR;

            // initialize frame status
            frameStatus.frameType = IsoATRFrame;
            frameStatus.frameStart = characterStatus.start;
            frameStatus.frameEnd = characterStatus.end;
            frameStatus.frameFlags = 0;
            frameStatus.frameSize = 0;
            frameStatus.frameData[frameStatus.frameSize++] = characterStatus.data;
            frameStatus.symbolRate = 1.0 / protocolStatus.elementaryTimeUnit;

            // clear status for next character
            characterStatus = {};

            log->info("\tcard is using {} convention", {ISO_7816_CONVENTION_TABLE[protocolStatus.symbolConvention]});
         }
      }

      return false;
   }

   /*
    * Decode ATR frame
    */
   bool detectATR(std::list<RawFrame> &frames)
   {
      int result = ResultInvalid;

      switch (decodeCharacter())
      {
         case FullCharacter:
         {
            // update frame status and add character to frame data
            frameStatus.frameEnd = characterStatus.end;
            frameStatus.frameFlags |= characterStatus.flags;
            frameStatus.frameData[frameStatus.frameSize++] = characterStatus.data;

            // reset character status
            characterStatus = {};
         }

         // when no character detected assume ATR reception is complete
         case TimeoutCharacter:
         {
            // check ATR format
            if ((result = isATR(frameStatus.frameData, frameStatus.frameSize)) != ResultSuccess)
               break;

            // build request frame
            RawFrame request = RawFrame(Iso7816Tech, frameStatus.frameType);

            request.setFrameRate(frameStatus.symbolRate);
            request.setSampleStart(frameStatus.frameStart);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setFrameFlags(frameStatus.frameFlags);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setSampleRate(decoder->sampleRate);
            request.setDateTime(decoder->streamTime + request.timeStart());
            request.setTimeStart(static_cast<double>(frameStatus.frameStart) / static_cast<double>(decoder->sampleRate));
            request.setTimeEnd(static_cast<double>(frameStatus.frameEnd) / static_cast<double>(decoder->sampleRate));

            // add bytes to frame and flip to prepare read
            request.put(frameStatus.frameData, frameStatus.frameSize).flip();

            // process frame to capture ATR parameters
            process(request);

            // add to frame list
            frames.push_back(request);

            // set detected tech type
            bitrateParams.techType = Iso7816Tech;

            // setup communication parameters
            decoder->bitrate = &bitrateParams;
            decoder->modulation = &modulationStatus;

            // return request frame data
            return true;
         }
      }

      // if ATR bytes do not comply with ISO7816 format, reset modulation
      if (result == ResultFailed)
         resetModulation();

      return false;
   }

   /*
    * Decode next request / response frame
    */
   void decode(hw::SignalBuffer &samples, std::list<RawFrame> &frames)
   {
      switch (protocolStatus.protocolType)
      {
         case 0:
         {
            decodeStreamT0(samples, frames);
            return;
         }
         case 1:
         {
            decodeStreamT1(samples, frames);
            return;
         }
         default:
         {
            decodeStreamTx(samples, frames);
         }
      }
   }

   /*
    * Decode T0 protocol stream
    */
   void decodeStreamT0(hw::SignalBuffer &samples, std::list<RawFrame> &frames)
   {
      while (decoder->nextSample(samples))
      {
         if (decodeFrameT0())
         {
            // frames must contain at least one byte
            if (frameStatus.frameSize == 0)
            {
               frameStatus = {.frameType = IsoExchangeFrame};
               modulationStatus = {};
               characterStatus = {};
               return;
            }

            log->info("new frame, {}->{}, length {} bytes", {frameStatus.frameStart, frameStatus.frameEnd, frameStatus.frameSize});

            // build request frame
            RawFrame request = RawFrame(Iso7816Tech, frameStatus.frameType);

            request.setFrameRate(frameStatus.symbolRate);
            request.setSampleStart(frameStatus.frameStart);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setFrameFlags(frameStatus.frameFlags);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setSampleRate(decoder->sampleRate);
            request.setDateTime(decoder->streamTime + request.timeStart());
            request.setTimeStart(static_cast<double>(frameStatus.frameStart) / static_cast<double>(decoder->sampleRate));
            request.setTimeEnd(static_cast<double>(frameStatus.frameEnd) / static_cast<double>(decoder->sampleRate));

            // add bytes to frame and flip to prepare read
            request.put(frameStatus.frameData, frameStatus.frameSize).flip();

            // process frame
            process(request);

            // add to frame list
            frames.push_back(request);

            // return request frame data
            return;
         }
      }
   }

   /*
    * Decode T1 protocol stream
    */
   void decodeStreamT1(hw::SignalBuffer &samples, std::list<RawFrame> &frames)
   {
      while (decoder->nextSample(samples))
      {
         if (decodeFrameT1())
         {
            // frames must contain at least one byte
            if (frameStatus.frameSize == 0)
            {
               frameStatus = {.frameType = IsoExchangeFrame};
               modulationStatus = {};
               characterStatus = {};
               return;
            }

            log->info("new frame, {}->{}, length {} bytes", {frameStatus.frameStart, frameStatus.frameEnd, frameStatus.frameSize});

            // build request frame
            RawFrame request = RawFrame(Iso7816Tech, frameStatus.frameType);

            request.setFrameRate(frameStatus.symbolRate);
            request.setSampleStart(frameStatus.frameStart);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setFrameFlags(frameStatus.frameFlags);
            request.setSampleEnd(frameStatus.frameEnd);
            request.setSampleRate(decoder->sampleRate);
            request.setDateTime(decoder->streamTime + request.timeStart());
            request.setTimeStart(static_cast<double>(frameStatus.frameStart) / static_cast<double>(decoder->sampleRate));
            request.setTimeEnd(static_cast<double>(frameStatus.frameEnd) / static_cast<double>(decoder->sampleRate));

            // add bytes to frame and flip to prepare read
            request.put(frameStatus.frameData, frameStatus.frameSize).flip();

            // process frame
            process(request);

            // add to frame list
            frames.push_back(request);

            // return request frame data
            return;
         }
      }
   }

   /*
    * Decode other protocol stream
    */
   void decodeStreamTx(hw::SignalBuffer &samples, std::list<RawFrame> &frames)
   {
      while (decoder->nextSample(samples))
      {
      }
   }

   /*
    * Decode one T0 TPDU frame
    */
   bool decodeFrameT0()
   {
      int result;

      if ((result = decodeCharacter()) == FullCharacter)
      {
         // initialize frame start time on first character
         if (!frameStatus.frameStart)
            frameStatus.frameStart = characterStatus.start;

         // update frame and add character to frame data
         frameStatus.frameEnd = characterStatus.end;
         frameStatus.frameFlags |= characterStatus.flags;
         frameStatus.frameData[frameStatus.frameSize++] = characterStatus.data;

         // reset character status
         characterStatus = {};

         // check if frame is protocol parameter selection
         if (isPPS(frameStatus.frameData, frameStatus.frameSize) == ResultSuccess)
         {
            frameStatus.frameType = protocolStatus.protocolParametersChange ? IsoResponseFrame : IsoRequestFrame;
            return true;
         }

         // check if frame is TPDU request
         if (isTPDU(frameStatus.frameData, frameStatus.frameSize) == ResultSuccess)
         {
            frameStatus.frameType = IsoExchangeFrame;
            return true;
         }

         // could shouldn't, but check maximum frame size to release frame processing
         if (frameStatus.frameSize == protocolStatus.maximumInformationSize)
            return true;

         return false;
      }

      // if no more characters to process, assume frame is complete
      return result == TimeoutCharacter;
   }

   /*
    * Decode one T1 frame
    */
   bool decodeFrameT1()
   {
      int result;

      if ((result = decodeCharacter()) == FullCharacter)
      {
         // initialize frame start time on first character
         if (!frameStatus.frameStart)
            frameStatus.frameStart = characterStatus.start;

         // update frame and add character to frame data
         frameStatus.frameEnd = characterStatus.end;
         frameStatus.frameFlags |= characterStatus.flags;
         frameStatus.frameData[frameStatus.frameSize++] = characterStatus.data;

         // reset character status
         characterStatus = {};

         // check if frame is protocol parameter selection
         if (isBlock(frameStatus.frameData, frameStatus.frameSize) == ResultSuccess)
            return true;

         // could shouldn't, but check maximum frame size to release frame processing
         if (frameStatus.frameSize == protocolStatus.maximumInformationSize)
            return true;

         return false;
      }

      // if no more characters to process, assume frame is complete
      return result == TimeoutCharacter;
   }

   /*
    * Decode one character
    */
   int decodeCharacter()
   {
      switch (decodeSymbol())
      {
         case FullSymbol:
         {
            // start bit
            if (characterStatus.bits == 0)
            {
               characterStatus.data = 0;
               characterStatus.start = symbolStatus.start;
            }

            // data bits
            else if (characterStatus.bits < 9)
            {
               characterStatus.data |= protocolStatus.symbolConvention == DirectConvention ? symbolStatus.data << (characterStatus.bits - 1) : symbolStatus.data << (8 - characterStatus.bits);
            }

            // parity bit
            else if (characterStatus.bits == 9)
            {
               characterStatus.end = symbolStatus.end;
               characterStatus.parity = symbolStatus.data;
               characterStatus.flags |= checkParity(characterStatus.data, characterStatus.parity) ? ParityError : 0;
            }

            if (characterStatus.bits >= 9)
            {
               // for protocol T0, there are addition error bit to check
               if (protocolStatus.protocolType == PROTO_T0)
               {
                  if (characterStatus.bits == 10)
                  {
                     // next character must start before guard time after the leading edge of the last character
                     modulationStatus.searchStartTime = characterStatus.start + frameStatus.guardTime; // guard time for start receiving next character
                     modulationStatus.searchEndTime = characterStatus.start + frameStatus.waitingTime; // limit for start receiving next character
                     modulationStatus.searchSyncTime = 0; // to search for start bit sync must be 0

                     // if error signal is HIGH, character is completed and parity OK
                     if (symbolStatus.value)
                        return FullCharacter;

                     // reset character status to receive again
                     characterStatus = {};

                     // and set invalid character to search for next start bit
                     return IncompleteCharacter;
                  }
               }

               // for protocol T1, there are no error bits
               else if (protocolStatus.protocolType == PROTO_T1)
               {
                  // next character must start before guard time after the leading edge of the last character
                  modulationStatus.searchStartTime = characterStatus.start + frameStatus.guardTime; // guard time for start receiving next character
                  modulationStatus.searchEndTime = characterStatus.start + frameStatus.waitingTime; // limit for start receiving next character
                  modulationStatus.searchSyncTime = 0; // to search for start bit sync must be 0

                  return FullCharacter;
               }
            }

            // search for next bit
            characterStatus.bits++;

            // set next symbol synchronization point
            modulationStatus.searchSyncTime = characterStatus.start + protocolStatus.elementaryTime * characterStatus.bits + protocolStatus.elementaryHalfTime;

            // search for next symbol
            return IncompleteCharacter;
         }

         case ResetLowSymbol:
            return ResetLowCharacter;

         case TimeoutSymbol:
            return TimeoutCharacter;
      }

      return IncompleteCharacter;
   }

   /*
    * Decode symbol
    */
   unsigned int decodeSymbol()
   {
      const float dataEdge = decoder->sampleEdge[CH_IO];
      const float resetEdge = decoder->sampleEdge[CH_RST];
      const float vccEdge = decoder->sampleEdge[CH_VCC];
      const bool dataValue = decoder->sampleData[CH_IO] > 0;

      // during search, VCC must be HIGH
      if (vccEdge < 0)
      {
         resetModulation();
         return PowerLowSymbol;
      }

      // during search, RST must be HIGH
      if (resetEdge < 0)
      {
         resetModulation();
         return ResetLowSymbol;
      }

      // wait guard time before searching for new symbol
      if (modulationStatus.searchStartTime && decoder->signalClock < modulationStatus.searchStartTime)
         return IncompleteSymbol;

      // detect end of modulation search
      if (modulationStatus.searchEndTime && decoder->signalClock >= modulationStatus.searchEndTime)
         return TimeoutSymbol;

      // detect start bit
      if (!modulationStatus.searchSyncTime && dataEdge < 0)
      {
         // set synchronization point for start bit after detect leading edge
         modulationStatus.searchStartTime = 0; // disable search start limit
         modulationStatus.searchEndTime = 0; // disable search end limit
         modulationStatus.searchSyncTime = decoder->signalClock + protocolStatus.elementaryHalfTime; // set synchronization point for start bit
      }

      // capture symbol value at synchronization point
      if (!modulationStatus.searchSyncTime || decoder->signalClock < modulationStatus.searchSyncTime)
         return IncompleteSymbol;

      // update symbol status
      symbolStatus.value = dataValue;
      symbolStatus.sync = modulationStatus.searchSyncTime; // set symbol synchronization point
      symbolStatus.start = modulationStatus.searchSyncTime - protocolStatus.elementaryHalfTime; // set symbol start time at leading edge
      symbolStatus.end = modulationStatus.searchSyncTime + protocolStatus.elementaryHalfTime; // set symbol end time at trailing edge
      symbolStatus.data = protocolStatus.symbolConvention == DirectConvention ? dataValue : !dataValue; // symbol value

      return FullSymbol;
   }

   /*
    * Process request or response frame
    */
   void process(RawFrame &frame)
   {
      do
      {
         if (processATR(frame))
            break;

         if (processPPS(frame))
            break;

         if (processTPDU(frame))
            break;
      }
      while (false);

      // for request frame set response timings
      switch (frame.frameType())
      {
         case IsoRequestFrame:
         {
            frameStatus.frameType = IsoResponseFrame;
            frameStatus.guardTime = protocolStatus.characterGuardTime - GT_THRESHOLD * protocolStatus.elementaryTime;
            frameStatus.waitingTime = protocolStatus.blockWaitingTime + WT_THRESHOLD * protocolStatus.elementaryTime;
            break;
         }
         case IsoResponseFrame:
         {
            frameStatus.frameType = IsoRequestFrame;
            frameStatus.guardTime = protocolStatus.characterGuardTime - GT_THRESHOLD * protocolStatus.elementaryTime;
            frameStatus.waitingTime = protocolStatus.blockWaitingTime + WT_THRESHOLD * protocolStatus.elementaryTime;
            break;
         }
         default:
         {
            frameStatus.guardTime = protocolStatus.characterGuardTime - GT_THRESHOLD * protocolStatus.elementaryTime;
            frameStatus.waitingTime = protocolStatus.characterWaitingTime + WT_THRESHOLD * protocolStatus.elementaryTime;
         }
      }

      // clear search to detect first start bit of next frame
      modulationStatus.searchStartTime = 0;
      modulationStatus.searchEndTime = 0;
      modulationStatus.searchSyncTime = 0;

      // reset frame timing parameters and clear frame data
      frameStatus.frameStart = 0;
      frameStatus.frameEnd = 0;
      frameStatus.frameFlags = 0;
      frameStatus.frameSize = 0;
      frameStatus.symbolRate = 1.0 / protocolStatus.elementaryTimeUnit;
   }

   /*
    * Process ATR frame
    */
   bool processATR(RawFrame &frame)
   {
      if (frame.frameType() != IsoATRFrame)
         return false;

      log->debug("process ATR frame");

      bool updateParameters = false;

      int i = 1, n = 2, k = 1, p = 0;

      do
      {
         // check presence of TAk
         if (frame[i] & ATR_TA_MASK)
         {
            unsigned int ta = frame[n++];

            switch (k)
            {
               case 1:
               {
                  // TA1 encodes the value of the clock rate conversion integer (Fi), the baud rate adjustment integer(Di)
                  // and the maximum value of the frequency supported by the card
                  unsigned int fi = ta >> 4;
                  unsigned int di = ta & 0x0f;
                  unsigned int dn = ISO_DI_TABLE[di];
                  unsigned int fn = ISO_FI_TABLE[fi];
                  unsigned int fm = ISO_FM_TABLE[fi];

                  log->debug("\tTA1 Fi {}, maximum frequency {.2} MHz ({} clock cycles)", {fi, fm / 1E6, fn});
                  log->debug("\tTA1 Di {}, baud rate divisor 1/{}", {di, dn});

                  break;
               }
               case 3:
               {
                  protocolStatus.maximumInformationSize = ta;

                  log->debug("\tTA3 IFSC {}, maximum information field size for the card", {ta});

                  break;
               }
            }
         }

         // check presence of TBk
         if (frame[i] & ATR_TB_MASK)
         {
            unsigned int tb = frame[n++];

            switch (k)
            {
               case 3:
               {
                  unsigned int bwi = tb >> 4;
                  unsigned int cwi = tb & 0x0f;
                  unsigned int bwt = 11 + ISO_BWT_TABLE[bwi];
                  unsigned int cwt = 11 + ISO_CWT_TABLE[cwi];

                  updateParameters = true;
                  protocolStatus.blockWaitingTimeUnits = bwt;
                  protocolStatus.characterWaitingTimeUnits = cwt;

                  log->debug("\tTB3 BWI {}, maximum delay between two blocks ({} ETUs)", {bwi, bwt});
                  log->debug("\tTB3 CWI {}, maximum delay between two characters ({} ETUs)", {cwi, bwt});

                  break;
               }
            }

         }

         // check presence of TCk
         if (frame[i] & ATR_TC_MASK)
         {
            unsigned int tc = frame[n++];
            unsigned int dn = ISO_DI_TABLE[protocolStatus.baudRateFactorIndex];

            switch (k)
            {
               case 1:
               {
                  // update protocol extra guard time
                  updateParameters = true;
                  protocolStatus.extraGuardTimeUnits = tc;
                  log->debug("\tTC1 extra guard time is {} ETUs", {protocolStatus.extraGuardTimeUnits});
                  break;
               }
               case 2:
               {
                  // update protocol waiting time
                  updateParameters = true;
                  protocolStatus.characterWaitingTimeUnits = tc > 0 ? tc * 960 * dn : ISO_7816_CWT_DEF;
                  log->debug("\tTC2 waiting time is {} ETUs", {protocolStatus.characterWaitingTimeUnits});
                  break;
               }
               case 3:
               {
                  // update protocol waiting time
                  log->debug("\tTC3 error detection code to be used: {}", {tc & 1 ? "CRC" : "LRC"});
                  break;
               }
            }
         }

         // check presence of TDk
         if (frame[i] & ATR_TD_MASK)
         {
            // next protocol parameters
            p = frame[i] & 0x0f;

            // next structural byte
            i = n++;

            // next structure level
            k++;
         }
      }
      while ((i == n - 1) && n < frame.size());

      unsigned int hb = frame[1] & 0x0f;

      log->debug("\thistorical bytes {}", {hb});

      // update protocol parameters
      if (updateParameters)
         updateProtocol(protocolStatus.elementaryTime, protocolStatus.frequencyFactorIndex, protocolStatus.baudRateFactorIndex);

      return true;
   }

   /*
    * Process PPS frame
    */
   bool processPPS(RawFrame &frame)
   {
      if (frame[0] != PPS_CMD)
         return false;

      log->debug("process PPS {}", {!protocolStatus.protocolParametersChange ? "request" : "response"});

      int i = 1;

      unsigned int pps0 = frame[i++];

      if (!protocolStatus.protocolParametersChange)
      {
         log->debug("\trequest protocol T{}", {pps0 & 0x0f});
      }

      if (pps0 & PPS_PPS1_MASK)
      {
         unsigned pps1 = frame[i++];
         unsigned int fi = pps1 >> 4;
         unsigned int di = pps1 & 0x0f;

         // required protocol values
         double fn = ISO_FI_TABLE[fi];
         double dn = ISO_DI_TABLE[di];

         // new elementary time unit
         double et = protocolStatus.elementaryTime * (fn * protocolStatus.baudRateFactor) / (protocolStatus.frequencyFactor * dn);
         double etu = et * decoder->sampleTime;

         if (protocolStatus.protocolParametersChange)
         {
            protocolStatus.protocolType = pps0 & 0x0f;

            // update protocol parameters
            updateProtocol(et, fi, di);

            // in protocol T1, next frame must be IsoRequestFrame
            if (protocolStatus.protocolType == 1)
            {
               frameStatus.frameType = IsoRequestFrame;
            }
         }
         else
         {
            log->debug("\trequest frequency adjustment, FI {} ({.0} clock cycles)", {fi, fn});
            log->debug("\trequest baud rate divisor, DI {} (1/{.0})", {di, dn});
            log->debug("\trequest elementary time unit, ETU {.3} us ({.2} samples)", {1000000.0 * etu, et});

            // request protocol parameters change on card response
            protocolStatus.protocolParametersChange = true;
         }
      }

      if (pps0 & PPS_PPS2_MASK)
      {
      }

      if (pps0 & PPS_PPS3_MASK)
      {
      }

      return true;
   }

   /*
    * Process PPS frame
    */
   bool processTPDU(RawFrame &frame)
   {
      if (frame.frameType() != IsoExchangeFrame)
         return false;

      if (frame.size() < T0_TPDU_MIN_LEN || frame.size() > T0_TPDU_MAX_LEN)
         return false;

      // APDU CLA 0xFF is reserved por PPS
      if (frame[T0_TPDU_CLA_OFFSET] == PPS_CMD)
         return false;

      return true;
   }

   /*
    * Reset modulation status
    */
   void resetModulation()
   {
      log->warn("reset modulation status");

      // clear modulation parameters
      modulationStatus = {};

      // clear symbol status
      symbolStatus = {};

      // clear character status
      characterStatus = {};

      // clear frame processing status
      frameStatus = {};

      // clear bitrate parameters
      bitrateParams = {};

      // clear protocol parameters
      protocolStatus = {};

      // clear current detected bitrate
      decoder->bitrate = nullptr;

      // clear current detected modulation
      decoder->modulation = nullptr;

      // initialize default protocol parameters for start decoding
      protocolStatus.maximumInformationSize = ISO_7816_IFSC_DEF;
      protocolStatus.characterGuardTimeUnits = ISO_7816_CGT_DEF;
      protocolStatus.characterWaitingTimeUnits = ISO_7816_CWT_DEF;
      protocolStatus.extraGuardTimeUnits = ISO_7816_EGT_DEF;
      protocolStatus.blockGuardTimeUnits = ISO_7816_BGT_DEF;
      protocolStatus.blockWaitingTimeUnits = ISO_7816_BWT_DEF;

      // update modulation timings based on protocol parameters
      updateProtocol(0, ISO_7816_FI_DEF, ISO_7816_DI_DEF);

      // initialize frame parameters to default protocol parameters
      frameStatus.frameType = IsoATRFrame;
      frameStatus.guardTime = protocolStatus.characterGuardTime;
      frameStatus.waitingTime = protocolStatus.characterWaitingTime;
   }

   /*
    * Update protocol timmings based on number of samples for one ETU
    */
   void updateProtocol(double elementaryTime, unsigned int fi, unsigned int di)
   {
      // update frequency and baud rate factors
      protocolStatus.frequencyFactorIndex = fi;
      protocolStatus.baudRateFactorIndex = di;
      protocolStatus.frequencyFactor = ISO_FI_TABLE[protocolStatus.frequencyFactorIndex];
      protocolStatus.baudRateFactor = ISO_DI_TABLE[protocolStatus.baudRateFactorIndex];

      // initialize default protocol parameters for start decoding
      protocolStatus.elementaryTime = elementaryTime;
      protocolStatus.elementaryHalfTime = elementaryTime / 2;
      protocolStatus.elementaryTimeUnit = elementaryTime * decoder->sampleTime;
      protocolStatus.characterGuardTime = elementaryTime * protocolStatus.characterGuardTimeUnits;
      protocolStatus.characterWaitingTime = elementaryTime * protocolStatus.characterWaitingTimeUnits * protocolStatus.baudRateFactor;
      protocolStatus.blockGuardTime = elementaryTime * protocolStatus.blockGuardTimeUnits;
      protocolStatus.blockWaitingTime = elementaryTime * protocolStatus.blockWaitingTimeUnits * protocolStatus.baudRateFactor;
      protocolStatus.extraGuardTime = elementaryTime * protocolStatus.extraGuardTimeUnits;

      // update clock frequency
      protocolStatus.clockFrequency = elementaryTime > 0 ? protocolStatus.frequencyFactor / (protocolStatus.elementaryTimeUnit * protocolStatus.baudRateFactor) : 0;

      // reset protocol parameters change flag
      protocolStatus.protocolParametersChange = false;

      // trace new protocol parameters
      log->info("set protocol parameters, T{} ", {protocolStatus.protocolType});
      log->info("\tclock frequency.......: {.2} MHz", {protocolStatus.clockFrequency / 1000000.0});
      log->info("\tfrequency adjustment..: Fi {} Fn {}", {protocolStatus.frequencyFactorIndex, protocolStatus.frequencyFactor});
      log->info("\tbaud rate adjustment..: Di {} Dn {}", {protocolStatus.baudRateFactorIndex, protocolStatus.baudRateFactor});
      log->info("\telementary time unit..: 1 ETU {.3} us ({.2} samples)", {1000000.0 * protocolStatus.elementaryTimeUnit, protocolStatus.elementaryTime});
      log->info("\tcharacter guard time..: {} ETUs ({.3} us, {.2} samples)", {protocolStatus.characterGuardTimeUnits, 1000000.0 * protocolStatus.characterGuardTime * decoder->sampleTime, protocolStatus.characterGuardTime});
      log->info("\tcharacter waiting time: {} ETUs ({.3} us, {.2} samples)", {protocolStatus.characterWaitingTimeUnits, 1000000.0 * protocolStatus.characterWaitingTime * decoder->sampleTime, protocolStatus.characterWaitingTime});
      log->info("\tblock guard time......: {} ETUs ({.3} us, {.2} samples)", {protocolStatus.blockGuardTimeUnits, 1000000.0 * protocolStatus.blockGuardTime * decoder->sampleTime, protocolStatus.blockGuardTime});
      log->info("\tblock waiting time....: {} ETUs ({.3} us, {.2} samples)", {protocolStatus.blockWaitingTimeUnits, 1000000.0 * protocolStatus.blockWaitingTime * decoder->sampleTime, protocolStatus.blockWaitingTime});
      log->info("\textra guard time......: {} ETUs ({.3} us, {.2} samples)", {protocolStatus.extraGuardTimeUnits, 1000000.0 * protocolStatus.extraGuardTime * decoder->sampleTime, protocolStatus.extraGuardTime});
   }

   /*
    * Check ISO7816 ATR format
    */
   static int isATR(const unsigned char *atr, unsigned int size)
   {
      if (size < ATR_MIN_LEN)
         return ResultInvalid;

      if (size > ATR_MAX_LEN)
         return ResultFailed;

      int i = 1, n = 1;
      int hb = atr[n++] & 0x0f;
      int ck = 0;

      do
      {
         if (atr[i] & ATR_TA_MASK) n++; // skip TAi
         if (atr[i] & ATR_TB_MASK) n++; // skip TBi
         if (atr[i] & ATR_TC_MASK) n++; // skip TCi
         if (atr[i] & ATR_TD_MASK) i = n++; // check next TDi

      }
      while ((i == n - 1) && n < size);

      // check frame size with historical bytes
      if (size < n + hb + 1)
         return ResultInvalid;

      // finally check LCR parity
      for (int j = 1; j < size; j++)
         ck ^= atr[j];

      // check parity
      return !ck ? ResultSuccess : ResultFailed;
   }

   /*
    * Check ISO7816 PPS format
    */
   static int isPPS(const unsigned char *pps, unsigned int size)
   {
      if (size < PPS_MIN_LEN || size > PPS_MAX_LEN)
         return ResultInvalid;

      // check PPS command byte
      if (pps[0] != PPS_CMD)
         return ResultInvalid;

      int n = PPS_MIN_LEN, ck = 0;

      // check presence of PPS1, PPS2 and PPS3
      if (pps[1] & PPS_PPS1_MASK) n++;
      if (pps[1] & PPS_PPS2_MASK) n++;
      if (pps[1] & PPS_PPS3_MASK) n++;

      // check frame size
      if (size != n)
         return ResultInvalid;

      // finally check LCR parity
      for (int i = 0; i < size; i++)
         ck ^= pps[i];

      // check parity
      return !ck ? ResultSuccess : ResultFailed;
   }

   /*
    * Check ISO7816 TPDU format
    */
   static int isTPDU(const unsigned char *tpdu, unsigned int size)
   {
      if (size < T0_TPDU_MIN_LEN)
         return ResultInvalid;

      // ISO/IEC 7816-4 enforces 'FF' as invalid value of CLA (used for PPS)
      if (tpdu[T0_TPDU_CLA_OFFSET] == PPS_CMD)
         return ResultInvalid;

      // ISO/IEC 7816-4 enforces '6X' and '9X' as invalid values of INS.
      if (tpdu[T0_TPDU_INS_OFFSET] & 0xf0 == 0x60 || tpdu[T0_TPDU_INS_OFFSET] & 0xf0 == 0x90)
         return ResultInvalid;

      for (unsigned int offset = T0_TPDU_PROC_OFFSET; offset < size; offset++)
      {
         // check if procedure byte is NULL (INS), then device must transmit all remaining data
         if (tpdu[offset] == 0x60)
            continue;

         // check if procedure byte is SW1
         if ((tpdu[offset] & 0xF0) == 0x60 || (tpdu[offset] & 0xF0) == 0x90)
            return size == offset + 2 ? ResultSuccess : ResultInvalid;

         // check if procedure byte is ACK (INS), then device must transmit all remaining data
         if (tpdu[offset] == tpdu[T0_TPDU_INS_OFFSET])
            offset += tpdu[T0_TPDU_P3_OFFSET];

            // check if procedure byte is ACK (INS^0xFF), then device must transmit ONE byte
         else if (tpdu[offset] == tpdu[T0_TPDU_INS_OFFSET] ^ 0xFF)
            offset++;
      }

      return ResultInvalid;
   }

   /*
 * Check ISO7816 Block format
 */
   int isBlock(const unsigned char *block, unsigned int size)
   {
      if (size < T1_BLOCK_MIN_LEN || size > T1_BLOCK_MAX_LEN)
         return ResultInvalid;

      // ISO/IEC 7816-4 enforces 'FF' as invalid value of NAD (used for PPS)
      if (block[T1_BLOCK_NAD_OFFSET] == PPS_CMD)
         return ResultInvalid;

      // get block length
      int len = block[T1_BLOCK_LEN_OFFSET], rc = 0;

      if (protocolStatus.errorCodeType == LRCCode)
      {
         if (size != T1_BLOCK_MIN_LEN + len + 1)
            return ResultInvalid;

         // finally check LCR parity
         for (int i = 1; i < size; i++)
            rc ^= block[i];

         // check parity
         return !rc ? ResultSuccess : ResultFailed;
      }

      if (protocolStatus.errorCodeType == CRCCode)
      {

      }

      return ResultInvalid;
   }

   /*
    * Check ISO7816 byte parity
    */
   static bool checkParity(unsigned int value, unsigned int parity)
   {
#pragma GCC ivdep
      for (unsigned int i = 0; i < 8; i++)
      {
         if ((value & (1 << i)) != 0)
         {
            parity = parity ^ 1;
         }
      }

      return parity;
   }
};

Iso7816::Iso7816(IsoDecoderStatus *decoder) : self(new Impl(decoder))
{
}

Iso7816::~Iso7816()
{
   delete self;
}

/*
 * Configure ISO7816 modulation
 */
void Iso7816::initialize(unsigned int sampleRate)
{
   self->initialize(sampleRate);
}

/*
 * Detect ISO7816 modulation
 */
bool Iso7816::detect(std::list<RawFrame> &frames)
{
   return self->detect(frames);
}

/*
 * Decode next poll or listen frame
 */
void Iso7816::decode(hw::SignalBuffer &samples, std::list<RawFrame> &frames)
{
   self->decode(samples, frames);
}

}
