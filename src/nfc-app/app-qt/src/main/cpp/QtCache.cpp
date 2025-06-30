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

#include <QDebug>
#include <QFile>
#include <QThread>
#include <QPointer>

#include <lab/data/RawFrame.h>

#include <hw/SignalType.h>
#include <hw/SignalBuffer.h>

#include "QtCache.h"

#include "QtApplication.h"

struct CacheHeader
{
   char magic[4];
   uint32_t version;
   uint32_t info[6];
};

struct CacheBlock
{
   float position;
   int samples;
};

struct CacheEntry
{
   float offset;
   char sample;
};

struct QtCache::Impl
{
   // configuration
   QThread *writer;

   // configuration
   QSettings settings;

   // cache for frames can be in memory
   QList<lab::RawFrame> frames;

   // cache for signal buffers is in disk
   QMap<QString, QFile *> signal;

   explicit Impl(QObject *parent) : writer(new QThread(parent))
   {
      // switch cache to writer thread
      parent->moveToThread(writer);

      // start writer thread
      writer->start();
   }

   // destructor
   ~Impl()
   {
      // stop writer thread
      if (writer->isRunning())
      {
         writer->quit();
         writer->wait();
      }

      delete writer;
   }

   void append(const lab::RawFrame &frame)
   {

   }

   void append(const hw::SignalBuffer &buffer)
   {
      switch (buffer.type())
      {
         case hw::SignalType::SIGNAL_TYPE_LOGIC_SAMPLES:
            cacheSignal(buffer, "logic", buffer.id());
            break;
         case hw::SignalType::SIGNAL_TYPE_RADIO_SAMPLES:
            cacheSignal(buffer, "radio", buffer.id());
            break;
         default:
            break;
      }
   }

   void cacheSignal(hw::SignalBuffer buffer, QString type, int id)
   {
      QFile *file = cacheFile(type, id);

      if (file->isOpen())
      {

      }
   }

   QFile *cacheFile(const QString &type, unsigned int id)
   {
      // create cache name
      QString name = QString("%1-%2").arg(type, QString::number(id));

      if (signal.contains(name))
         return signal[name];

      // build fileName
      QString fileName = QtApplication::tempPath().absoluteFilePath(name + ".dat");

      // store in cache
      signal[name] = new QFile(fileName);

      // open the file
      if (!signal[name]->open(QIODevice::ReadWrite | QIODevice::Truncate))
         qWarning() << "Failed to open cache file:" << fileName;

      return signal[name];
   }
};

QtCache::QtCache() : impl(new Impl(this))
{
}

void QtCache::append(const lab::RawFrame &frame)
{
}

void QtCache::append(const hw::SignalBuffer &buffer)
{
}

void QtCache::clear()
{
}

long QtCache::frames() const
{
   return 0;
}

long QtCache::samples() const
{
   return 0;
}
