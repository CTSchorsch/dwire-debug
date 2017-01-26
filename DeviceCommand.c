/// DeviceCommand.c



#ifdef windows

  char *DosDevices = 0;
  char *CurrentDevice = 0;

  void LoadDosDevices() {
    if (DosDevices == (char*)-1) {CurrentDevice=0; return;}
    int size = 24576;
    int used = 0;
    while (1) {
      DosDevices = Allocate(size);
      used = QueryDosDevice(0, DosDevices, size);
      if (used) {break;}
      int error = GetLastError();
      Free(DosDevices);
      if (error != ERROR_INSUFFICIENT_BUFFER) {break;}
      size *= 2;
    }
    if (used) {CurrentDevice = DosDevices;}
  }

  void AdvanceCurrentDevice() {
    while (*CurrentDevice) {CurrentDevice++;}
    CurrentDevice++;
    if (!*CurrentDevice) {CurrentDevice = 0;}
  }

  void NextUsbSerialPort() {
    if (!DosDevices) {LoadDosDevices();}
    while (CurrentDevice  &&  strncmp("COM", CurrentDevice, 3)) {AdvanceCurrentDevice();}
    if (CurrentDevice) {
      strncpy(UsbSerialPortName, CurrentDevice, 6);
      UsbSerialPortName[6] = 0;
      AdvanceCurrentDevice();
    } else {
      UsbSerialPortName[0] = 0;
      Free(DosDevices);
      DosDevices = 0;
      CurrentDevice = 0;
    }
  }

#else

  #include <dirent.h>

  DIR *DeviceDir = 0;

  void NextUsbSerialPort() {
    UsbSerialPortName[0] = 0;
    if (!DeviceDir) {DeviceDir = opendir("/dev");
    if (!DeviceDir) {return;}}
    while (!UsbSerialPortName[0]) {
      struct dirent *entry = readdir(DeviceDir);
      if (!entry) {closedir(DeviceDir); DeviceDir=0; return;}
      if (!strncmp("ttyUSB", entry->d_name, 6))
        {strncpy(UsbSerialPortName, entry->d_name, 255); UsbSerialPortName[255] = 0;}
    }
  }

#endif




int ReadByte() {
  u8 byte = 0;
  int bytesRead;

  bytesRead = Read(SerialPort, &byte, 1);
  if (bytesRead == 1) return byte; else return -1;
}


int GetBreakResponseByte(int breaklen) {
  int byte  = 0;

  SerialBreak(breaklen);
  byte = ReadByte();
  if (byte != 0) {Ws(", warning, byte read after break is non-zero.");}

  //Ws(", skipping [");
  while (byte == 0)    {/*Wc('0');*/ byte = ReadByte();} // Skip zero bytes generated by break.
  while (byte == 0xFF) {/*Wc('F');*/ byte = ReadByte();} // Skip 0xFF bytes generated by line going high following break.
  //Ws("]");

  return byte;
}



void Wbits(int byte) {
  if (byte < 0) {Wd(byte,1);}
  else for (int i=7; i>=0; i--) {Wc(((byte >> i) & 1) ? '1' : '0');}
}




int scaleby(int byte) {
  // Return amount to adjust baudrate by before the next test.
  // The scale is returned as a percentage.
  // E.g. Returns 95 to suggest a reduction by 5%.
  // Returns 100 only when the byte is exactly as expected.

  if (byte == 0x55) return 100;

  int lengthcount[9] = {0}; // Number of runs of each length

  int i = 7;
  while (((byte >> i) & 1) && (i > 1)) i--;  // Ignore leading ones as they likely come from preceeding stop bits.

  int remaining = i+1;  // Number of remaining bits we have to work with.

  int c = (byte >> i) & 1;  // Current bit value
  int s = 0; // Start of current run
  while (i > 0) {
    i--;
    int b = (byte >> i) & 1;
    if (b != c) { // Change of line polarity
      lengthcount[s-i]++;
      c = b;
      s = i;
    }
  }
  lengthcount[s+1]++; // Length of final run

  // If all of the remaining bits are 0, it means the first '0' bit of the
  // actual 01010101 response is at least that wide at the current baud clock.

  if (lengthcount[8-remaining]) // Other than leading '1's, all bits are '0'
    switch(remaining) {
      case 1:  return 20;
      case 2:  return 20;
      case 3:  return 20;
      case 4:  return 30;
      case 5:  return 40;
      case 6:  return 60;
      default: return 95;
    }

  // Now interpret run lengths
  if (lengthcount[3] > 1) return 60; // There are at least 2 runs 3 bits wide
  if (lengthcount[2] > 1) return 85; // There are at least 2 runs 2 bits wide
  if (lengthcount[1] > 1) return 95; // at least two runs just 1 bit long, we're very close.
  if (lengthcount[4])     return 40; // There is a 4 bit run
  if (lengthcount[5])     return 30; // There is a 6 bit run
  return 25; // There is a 7 or more bit run
}




int TryBaudRate(int baudrate, int breaklen) {
  // Returns: > 100 - approx factor by which rate is too high (as multiple of 10)
  //          = 100 - rate generates correct 0x55 response
  //          = 0   - port does not support this baud rate

  Ws("Trying "); Ws(UsbSerialPortName);
  Ws(", baud rate "); Wd(baudrate,5);
  Ws(", breaklen "); Wd(breaklen,4);
  Flush();
  MakeSerialPort(UsbSerialPortName, baudrate);
  if (!SerialPort) {
    Wsl(". Cannot set this baud rate, probably not an FT232.");
    return 0;
  }

  int byte = GetBreakResponseByte(breaklen);
  CloseSerialPort();
  SerialPort = 0;

  if (byte < 0) {
    Wsl(", No response, giving up."); return 0;
  } else {
    Ws(", received "); Wbits(byte);
  }

  int scale = scaleby(byte);
  if (scale == 100) Wsl(": expected result.");
  return scale;
}




void FindBaudRate() {

  int baudrate = 40000; // Start well above the fastest dwire baud rate based
                        // on the max specified ATtiny clock of 20MHz.
  int breaklen = 50;    // 50ms allows for clocks down to 320KHz.
                        // For 8 MHz break len can be as low as 2ms.

  // First try lower and lower clock rates until we get a good result.
  // The baud rate for each attempt is based on a rough measurement of
  // the relative size of pulses seen in the byte returned after break.

  int scale = TryBaudRate(baudrate, breaklen);
  if (scale == 0) {return;}

  while (scale != 100) {
    Ws(", scale "); Wd(scale,1); Wsl("%");
    baudrate = (baudrate * scale) / 100;
    scale = TryBaudRate(baudrate, breaklen);
  }

  if (scale == 0) return;

  // We have hit a baudrate that returns the right result this one time.
  // Now find a lower and upper bound of working rates in order to
  // finally choose the middle rate.

  breaklen = 100000 / baudrate; // Now we have the approx byte len we can use a shorter break.

  Wsl("Finding upper bound.");

  int upperbound = baudrate;
  do {
    int trial = (upperbound * 102) / 100;
    scale = TryBaudRate(trial, breaklen);
    if (scale == 100) upperbound = trial;
  } while (scale == 100);
  Wl();
  Wsl("Finding lower bound.");

  int lowerbound = baudrate;
  do {
    int trial = (lowerbound * 98) / 100;
    scale = TryBaudRate(trial, breaklen);
    if (scale == 100) lowerbound = trial;
  } while (scale == 100);
  Wl();

  // Finally open the port with the middle of the working range.

  baudrate = (lowerbound + upperbound) / 2;
  Wl(); Ws("Chosen baud rate "); Wd(baudrate,1); Wl(); Wl();
  MakeSerialPort(UsbSerialPortName, baudrate);

  // And check it actually worked.

  int byte = GetBreakResponseByte(breaklen);
  if (byte != 0x55) {CloseSerialPort(); SerialPort = 0;}
}




void TryConnectSerialPort() {
  jmp_buf oldFailPoint;
  memcpy(oldFailPoint, FailPoint, sizeof(FailPoint));

  if (setjmp(FailPoint)) {
    SerialPort = 0;
  } else {
    FindBaudRate();
    if (SerialPort) {DwConnect();}
  }

  memcpy(FailPoint, oldFailPoint, sizeof(FailPoint));
}




// Implement ConnectSerialPort called from DwPort.c
void ConnectSerialPort() {
  SerialPort = 0;
  if (UsbSerialPortName[0]) {
    TryConnectSerialPort();
    if (!SerialPort) {Ws("Couldn't connect to DebugWIRE device on "); Fail(UsbSerialPortName);}
  } else {
    while (!SerialPort) {
      NextUsbSerialPort();
      if (!UsbSerialPortName[0]) {Fail("Couldn't find a DebugWIRE device.");}
      TryConnectSerialPort();
    }
  }
}




void DeviceCommand() {
  if (SerialPort) {CloseSerialPort();}
  Sb();
  Ran(ArrayAddressAndLength(UsbSerialPortName));
  ConnectSerialPort();
}
