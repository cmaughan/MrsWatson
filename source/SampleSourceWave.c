//
// SampleSourceWave.c - MrsWatson
// Created by Nik Reiman on 1/22/12.
// Copyright (c) 2012 Teragon Audio. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AudioSettings.h"
#include "SampleSourceWave.h"
#include "EventLogger.h"
#include "RiffFile.h"
#include "SampleSource.h"
#include "SampleSourcePcm.h"
#include "PlatformUtilities.h"

#if USE_LIBAUDIOFILE
#include "SampleSourceAudiofile.h"
#endif

#if ! USE_LIBAUDIOFILE
static boolByte _readWaveFileInfo(const char* filename, SampleSourceWaveData extraData) {
  int chunkOffset = 0;
  RiffChunk chunk = newRiffChunk();
  char format[4];

  if(readNextChunk(extraData->fileHandle, chunk, false)) {
    if(!isChunkIdEqualTo(chunk, "RIFF")) {
      logFileError(filename, "Invalid RIFF chunk descriptor");
      freeRiffChunk(chunk);
      return false;
    }

    // The WAVE file format has two sub-chunks, with the size of both calculated in the size field. Before
    // either of the subchunks, there are an extra 4 bytes which indicate the format type. We need to read
    // that before either of the subchunks can be parsed.
    fread(format, sizeof(byte), 4, extraData->fileHandle);
    if(strncmp(format, "WAVE", 4)) {
      logFileError(filename, "Invalid format description");
      freeRiffChunk(chunk);
      return false;
    }
  }
  else {
    logFileError(filename, "No chunks following descriptor");
    freeRiffChunk(chunk);
    return false;
  }

  if(readNextChunk(extraData->fileHandle, chunk, true)) {
    if(!isChunkIdEqualTo(chunk, "fmt ")) {
      logError(filename, "Invalid format chunk header");
      freeRiffChunk(chunk);
      return false;
    }
    if(chunk->size < 20) {
      logFileError(filename, "Invalid format chunk size");
      freeRiffChunk(chunk);
      return false;
    }

    extraData->audioFormat = convertByteArrayToUnsignedShort(chunk->data + chunkOffset);
    chunkOffset += 2;
    if(extraData->audioFormat != 1) {
      logUnsupportedFeature("Compressed WAVE files");
      freeRiffChunk(chunk);
      return false;
    }

    extraData->numChannels = convertByteArrayToUnsignedShort(chunk->data + chunkOffset);
    chunkOffset += 2;

    extraData->sampleRate = convertByteArrayToUnsignedInt(chunk->data + chunkOffset);
    chunkOffset += 4;

    extraData->byteRate = convertByteArrayToUnsignedInt(chunk->data + chunkOffset);
    chunkOffset += 4;

    extraData->blockAlign = convertByteArrayToUnsignedShort(chunk->data + chunkOffset);
    chunkOffset += 2;

    extraData->bitsPerSample = convertByteArrayToUnsignedShort(chunk->data + chunkOffset);
    if(extraData->bitsPerSample > 16) {
      logUnsupportedFeature("Bitrates greater than 16");
      freeRiffChunk(chunk);
      return false;
    }
    else if(extraData->bitsPerSample < 16) {
      logUnsupportedFeature("Bitrates lower than 16");
      freeRiffChunk(chunk);
      return false;
    }
  }
  else {
    logFileError(filename, "WAVE file has no chunks following format");
    freeRiffChunk(chunk);
    return false;
  }

  // We don't need the format data anymore, so free and re-alloc the chunk to avoid a small memory leak
  freeRiffChunk(chunk);
  chunk = newRiffChunk();

  // TODO: Option for reading entire file into memory
  if(readNextChunk(extraData->fileHandle, chunk, false)) {
    if(!isChunkIdEqualTo(chunk, "data")) {
      logFileError(filename, "WAVE file has invalid data chunk header");
      freeRiffChunk(chunk);
      return false;
    }

    logDebug("WAVE file has %d bytes", chunk->size);
  }

  return true;
}

static boolByte _writeWaveFileInfo(SampleSourceWaveData extraData) {
  RiffChunk chunk = newRiffChunk();

  memcpy(chunk->id, "RIFF", 4);
  if(fwrite(chunk->id, sizeof(byte), 4, extraData->fileHandle) != 4) {
    logError("Could not write RIFF header");
    freeRiffChunk(chunk);
    return false;
  }

  // Write the size, but this will need to be set again when the file is finished writing
  if(fwrite(&(chunk->size), sizeof(unsigned int), 1, extraData->fileHandle) != 1) {
    logError("Could not write RIFF chunk size");
    freeRiffChunk(chunk);
    return false;
  }

  memcpy(chunk->id, "WAVE", 4);
  if(fwrite(chunk->id, sizeof(byte), 4, extraData->fileHandle) != 4) {
    logError("Could not WAVE format");
    freeRiffChunk(chunk);
    return false;
  }

  // Write the format header
  memcpy(chunk->id, "fmt ", 4);
  chunk->size = 20;
  if(fwrite(chunk->id, sizeof(byte), 4, extraData->fileHandle) != 4) {
    logError("Could not write format header");
    freeRiffChunk(chunk);
    return false;
  }

  if(fwrite(&(chunk->size), sizeof(unsigned int), 1, extraData->fileHandle) != 1) {
    logError("Could not write format chunk size");
    freeRiffChunk(chunk);
    return false;
  }

  // TODO: These calls will not work on big-endian platforms

  if(fwrite(&(extraData->audioFormat), sizeof(unsigned short), 1, extraData->fileHandle) != 1) {
    logError("Could not write audio format");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->numChannels), sizeof(unsigned short), 1, extraData->fileHandle) != 1) {
    logError("Could not write channel count");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->sampleRate), sizeof(unsigned int), 1, extraData->fileHandle) != 1) {
    logError("Could not write sample rate");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->byteRate), sizeof(unsigned int), 1, extraData->fileHandle) != 1) {
    logError("Could not write byte rate");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->blockAlign), sizeof(unsigned short), 1, extraData->fileHandle) != 1) {
    logError("Could not write block align");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->bitsPerSample), sizeof(unsigned short), 1, extraData->fileHandle) != 1) {
    logError("Could not write bits per sample");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(extraData->extraParams), sizeof(byte), 4, extraData->fileHandle) != 4) {
    logError("Could not write extra PCM parameters");
    freeRiffChunk(chunk);
    return false;
  }

  memcpy(chunk->id, "data", 4);
  if(fwrite(chunk->id, sizeof(byte), 4, extraData->fileHandle) != 4) {
    logError("Could not write format header");
    freeRiffChunk(chunk);
    return false;
  }
  if(fwrite(&(chunk->size), sizeof(unsigned int), 1, extraData->fileHandle) != 1) {
    logError("Could not write data chunk size");
    freeRiffChunk(chunk);
    return false;
  }

  return true;
}
#endif

static boolByte _openSampleSourceWave(void *sampleSourcePtr, const SampleSourceOpenAs openAs) {
  SampleSource sampleSource = (SampleSource)sampleSourcePtr;
#if USE_LIBAUDIOFILE
  SampleSourceAudiofileData extraData = sampleSource->extraData;
#else
  SampleSourceWaveData extraData = (SampleSourceWaveData)sampleSource->extraData;
#endif

  if(openAs == SAMPLE_SOURCE_OPEN_READ) {
#if USE_LIBAUDIOFILE
    extraData->fileHandle = afOpenFile(sampleSource->sourceName->data, "r", NULL);
    if(extraData->fileHandle != NULL) {
      setNumChannels(afGetVirtualChannels(extraData->fileHandle, AF_DEFAULT_TRACK));
      setSampleRate((float)afGetRate(extraData->fileHandle, AF_DEFAULT_TRACK));
    }
#else
    extraData->fileHandle = fopen(sampleSource->sourceName->data, "rb");
    if(extraData->fileHandle != NULL) {
      if(_readWaveFileInfo(sampleSource->sourceName->data, extraData)) {
        extraData->pcmData->fileHandle = extraData->fileHandle;
        setNumChannels(extraData->numChannels);
        setSampleRate(extraData->sampleRate);
      }
      else {
        fclose(extraData->fileHandle);
        extraData->fileHandle = NULL;
      }
    }
#endif
  }
  else if(openAs == SAMPLE_SOURCE_OPEN_WRITE) {
#if USE_LIBAUDIOFILE
    AFfilesetup outfileSetup = afNewFileSetup();
    afInitFileFormat(outfileSetup, AF_FILE_WAVE);
    afInitByteOrder(outfileSetup, AF_DEFAULT_TRACK, AF_BYTEORDER_LITTLEENDIAN);
    afInitChannels(outfileSetup, AF_DEFAULT_TRACK, getNumChannels());
    afInitRate(outfileSetup, AF_DEFAULT_TRACK, getSampleRate());
    afInitSampleFormat(outfileSetup, AF_DEFAULT_TRACK, AF_SAMPFMT_TWOSCOMP, DEFAULT_BITRATE);
    extraData->fileHandle = afOpenFile(sampleSource->sourceName->data, "w", outfileSetup);
#else
    extraData->fileHandle = fopen(sampleSource->sourceName->data, "wb");
    if(extraData->fileHandle != NULL) {
      extraData->audioFormat = 1;
      extraData->numChannels = (unsigned short)getNumChannels();
      extraData->sampleRate = (unsigned int)getSampleRate();
      extraData->bitsPerSample = 16;
      extraData->byteRate = extraData->sampleRate * extraData->numChannels * extraData->bitsPerSample / 8;
      extraData->blockAlign = extraData->numChannels * extraData->bitsPerSample / 8;
      if(_writeWaveFileInfo(extraData)) {
        extraData->pcmData->fileHandle = extraData->fileHandle;
      }
      else {
        fclose(extraData->fileHandle);
        extraData->fileHandle = NULL;
      }
    }
#endif
  }
  else {
    logInternalError("Invalid type for openAs in WAVE file");
    return false;
  }

  if(extraData->fileHandle == NULL) {
    logError("WAVE file '%s' could not be opened for '%s'",
      sampleSource->sourceName->data, openAs == SAMPLE_SOURCE_OPEN_READ ? "reading" : "writing");
    return false;
  }

  sampleSource->openedAs = openAs;
  return true;
}

#if ! USE_LIBAUDIOFILE
static boolByte _readBlockFromWaveFile(void* sampleSourcePtr, SampleBuffer sampleBuffer) {
  SampleSource sampleSource = (SampleSource)sampleSourcePtr;
  SampleSourceWaveData extraData = (SampleSourceWaveData)(sampleSource->extraData);
  return readPcmDataFromFile(extraData->pcmData, sampleBuffer, &(sampleSource->numFramesProcessed));
}

static boolByte _writeBlockFromWaveFile(void* sampleSourcePtr, const SampleBuffer sampleBuffer) {
  boolByte result;
  SampleSource sampleSource = (SampleSource)sampleSourcePtr;
  SampleSourceWaveData extraData = (SampleSourceWaveData)(sampleSource->extraData);
  result = writePcmDataToFile(extraData->pcmData, sampleBuffer, &(extraData->numSamplesWritten));
  sampleSource->numFramesProcessed = extraData->numSamplesWritten;
  return result;
}

static void _freeSampleSourceDataWave(void* sampleSourceDataPtr) {
  SampleSourceWaveData extraData = (SampleSourceWaveData)sampleSourceDataPtr;
  unsigned int numBytesWritten;

  // Write correct chunk sizes to file's data chunk
  fseek(extraData->fileHandle, 44, SEEK_SET);
  numBytesWritten = extraData->numSamplesWritten * extraData->bitsPerSample / 8;
  fwrite(&numBytesWritten, sizeof(unsigned int), 1, extraData->fileHandle);
  // Add 40 bytes for fmt chunk size and write the RIFF chunk size
  numBytesWritten += 40;
  fseek(extraData->fileHandle, 4, SEEK_SET);
  fwrite(&numBytesWritten, sizeof(unsigned int), 1, extraData->fileHandle);
  fflush(extraData->fileHandle);

  freeSampleSourceDataPcm(extraData->pcmData);
  if(extraData->fileHandle != NULL) {
    fclose(extraData->fileHandle);
    extraData->fileHandle = NULL;
  }
  free(extraData);
}
#endif

SampleSource newSampleSourceWave(const CharString sampleSourceName) {
  SampleSource sampleSource = (SampleSource)malloc(sizeof(SampleSourceMembers));
#if USE_LIBAUDIOFILE
  SampleSourceAudiofileData extraData = (SampleSourceAudiofileData)malloc(sizeof(SampleSourceAudiofileDataMembers));
#else
  SampleSourceWaveData extraData = (SampleSourceWaveData)malloc(sizeof(SampleSourceWaveDataMembers));
#endif

  sampleSource->sampleSourceType = SAMPLE_SOURCE_TYPE_WAVE;
  sampleSource->openedAs = SAMPLE_SOURCE_OPEN_NOT_OPENED;
  sampleSource->sourceName = newCharString();
  copyCharStrings(sampleSource->sourceName, sampleSourceName);
  sampleSource->numChannels = getNumChannels();
  sampleSource->sampleRate = getSampleRate();
  sampleSource->numFramesProcessed = 0;

  sampleSource->openSampleSource = _openSampleSourceWave;
#if USE_LIBAUDIOFILE
  sampleSource->readSampleBlock = readBlockFromAudiofile;
  sampleSource->writeSampleBlock = writeBlockFromAudiofile;
  sampleSource->freeSampleSourceData = freeSampleSourceDataAudiofile;
#else
  sampleSource->readSampleBlock = _readBlockFromWaveFile;
  sampleSource->writeSampleBlock = _writeBlockFromWaveFile;
  sampleSource->freeSampleSourceData = _freeSampleSourceDataWave;
#endif

#if USE_LIBAUDIOFILE
  extraData->fileHandle = NULL;
  extraData->interlacedBuffer = NULL;
  extraData->pcmBuffer = NULL;
#else
  extraData->fileHandle = NULL;
  extraData->audioFormat = 0;
  extraData->numChannels = 0;
  extraData->sampleRate = 0;
  extraData->byteRate = 0;
  extraData->blockAlign = 0;
  extraData->bitsPerSample = 0;
  extraData->extraParams = 0;
  extraData->numSamplesWritten = 0;

  extraData->pcmData = (SampleSourcePcmData)malloc(sizeof(SampleSourcePcmDataMembers));
  extraData->pcmData->isStream = false;
  extraData->pcmData->fileHandle = NULL;
  extraData->pcmData->dataBufferNumItems = 0;
  extraData->pcmData->interlacedPcmDataBuffer = NULL;
#endif

  sampleSource->extraData = extraData;

  return sampleSource;
}
