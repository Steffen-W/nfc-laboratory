/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <sdr/AirspyDevice.h>
#include <sdr/RealtekDevice.h>
#include <sdr/DeviceFactory.h>

namespace sdr {

std::vector<std::string> DeviceFactory::deviceList()
{
   std::vector<std::string> devices;

   // add AirSpy devices
   for (const auto &entry: sdr::AirspyDevice::listDevices())
      devices.push_back(entry);

   // add RTl-SDR devices
   for (const auto &entry: sdr::RealtekDevice::listDevices())
      devices.push_back(entry);

   return devices;
}

RadioDevice *DeviceFactory::newInstance(const std::string &name)
{
   if (name.find("airspy://") == 0)
      return new AirspyDevice(name);

   if (name.find("rtlsdr://") == 0)
      return new RealtekDevice(name);

   //   if (name.startsWith("lime://"))
//      return new LimeDevice(name, parent);

   return nullptr;
}

}