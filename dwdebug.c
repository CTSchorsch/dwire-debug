//        dwdebug - debugger for DebugWIRE on ATtiny45.

#include "SystemServices.c"
#include "SimpleOutput.c"
#include "SimpleInput.c"

typedef unsigned char u8;


#include "DeviceCommand.c"
#include "SerialPort.c"
#include "DwPort.c"
#include "Disassemble.c"
#include "DwDebugInput.c"
#include "RegistersCommand.c"
#include "StackCommand.c"
#include "UnassembleCommand.c"
#include "Dump.c"
#include "DumpCommand.c"
#include "UserInterface.c"



void TryConnectSerialPort() {
  MakeSerialPort(UsbSerialPortName, 1000000/128);
  if (!SerialPort) {return;}

  SerialBreak(400);

  u8 byte;
  while ((byte = DwReadByte()) == 0x00) {} // Eat 0x00 bytes generated by line at break (0v)
  while (byte == 0xFF) {DwReadByte();}     // Eat 0xFF bytes generated by line going high following break.
  if (byte != 0x55) {return;}

  DwConnect();
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




int Program(int argCount, char **argVector) {

  // #ifdef windows
  //   MakeSerialPort("COM6", 1000000/128);
  // #else
  //   MakeSerialPort("/dev/ttyUSB0", 1000000/128);
  // #endif

  // DwBreak();
  // DwConnect();

  UI();
  return 0;
}
