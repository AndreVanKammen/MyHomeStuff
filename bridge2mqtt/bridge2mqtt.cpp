#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include "modbus.h"

#include "mosquittopp.h"

const char *serialPortName = "/dev/ttyUSB0";
const int baudRate = B115200;
const char *mqtt_host = "127.0.0.1";
const int mqtt_port = 1883;
const char *mainTopic = "power";
const char *modbus_host = "192.168.1.76";
const int modbus_port = 1502;
const int modbus_address = 40000;
const int modbus_address_len = 108;
const double modbus_interval = 5.0;
const char *modbus_topic = "power/SolarEdge";

struct termios tty;

//https://stackoverflow.com/questions/6357031/how-do-you-convert-a-byte-array-to-a-hexadecimal-string-in-c/
// converted to uint16_t
void uint16tox(char *xp, const uint16_t *bb, int n)
{
  const char xx[] = "0123456789ABCDEF";
  while (--n >= 0)
    xp[n] = xx[(bb[n >> 2] >> ((3 - (n & 3)) << 2)) & 0xF];
}

int main(int argc, char *argv[])
{
  modbus_t *modbusContext;
  uint16_t modbusRegisters[modbus_address_len + 1];
  char modbusHex[modbus_address_len * 4 + 1];

  modbusContext = modbus_new_tcp(modbus_host, modbus_port);
  // modbus_set_debug(modbusContext, true);
  modbus_set_slave(modbusContext, 1); // Without this no answer thanks for the massive time-waste
  modbus_set_response_timeout(modbusContext, 1, 0);
  bool hasModbus = modbus_connect(modbusContext) >= 0;
  if (!hasModbus)
  {
    printf("Error %i opening modbus: %s\n", errno, strerror(errno));
    exit(1);
  }

  // From https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
  int serial_port = open(serialPortName, O_RDWR);

  // Check for errors
  if (serial_port < 0)
  {
    printf("Error %i from serial open: %s\n", errno, strerror(errno));
    exit(1);
  }

  if (tcgetattr(serial_port, &tty) != 0)
  {
    printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
  }
  tty.c_cflag &= ~PARENB;        // Clear parity bit, disabling parity (most common)
  tty.c_cflag &= ~CSTOPB;        // Clear stop field, only one stop bit used in communication (most common)
  tty.c_cflag |= CS8;            // 8 bits per byte (most common)
  tty.c_cflag &= ~CRTSCTS;       // Disable RTS/CTS hardware flow control (most common)
  tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

  // Smart meter sends lines so we keep the line sending
  tty.c_lflag |= ICANON; // Use canonical mode
  // tty.c_lflag &= ~ICANON;

  tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP

  tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl

  // Smart meter sends lines so we keep the these?
  // tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
  tty.c_cc[VTIME] = 10; // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
  tty.c_cc[VMIN] = 0;

  cfsetispeed(&tty, baudRate);
  cfsetospeed(&tty, baudRate);

  // Save tty settings, also checking for error
  if (tcsetattr(serial_port, TCSANOW, &tty) != 0)
  {
    printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
  }

  // Initialise MQTT and connect to server
  struct mosquitto *mosq;
  mosquitto_lib_init();

  mosq = mosquitto_new(0, true, 0);

  if (!mosq)
  {
    fprintf(stderr, "MQTT initialisation failed!\n");
    exit(1);
  }

  if (mosquitto_connect(mosq, mqtt_host, mqtt_port, 60))
  {
    fprintf(stderr, "Unable to connect to MQTT server!\n");
    exit(1);
  }

  char buf[1000];
  char topic[100];
  int subTopicStartIx = strlen(mainTopic);
  memcpy(&topic, mainTopic, subTopicStartIx);
  topic[subTopicStartIx++] = '/';
  topic[subTopicStartIx] = 0;
  printf("topic: %s\n", topic);
  printf("Opened port!\n");

  time_t lastTime = time(0);
  while (true)
  {
    int rc = read(serial_port, buf, sizeof(buf) - 1);
    if (rc < 0)
    {
      printf("Error %i from read\n", rc);
    }
    else
    {
      int valStart = -1;
      int topicOutIx = subTopicStartIx;
      for (int ix = 0; ix < rc; ix++)
      {
        char c = buf[ix];
        if (c == '(')
        {
          valStart = ix;
        }
        else
        {
          if (valStart >= 0)
          {
            if (c == 13 || c == 10)
            {
              rc = ix;
            }
          }
          else
          {
            topic[topicOutIx++] = c;
          }
        }
      }

      buf[rc] = '\0';
      if (valStart >= 0)
      {
        topic[topicOutIx++] = 0;
        mosquitto_publish(mosq, NULL, topic, strlen(buf) - valStart, &buf[valStart], 0, 0);
        // printf("%s = %s\n",  topic, &buf[valStart]);
      }
      else
      {
        // printf("no start %s\n",  buf);
      }
    }
    if (hasModbus)
    {
      time_t newTime = time(0);
      if (difftime(newTime, lastTime) > modbus_interval)
      {
        lastTime = newTime;
        if (modbus_read_registers(modbusContext, modbus_address, modbus_address_len, modbusRegisters) < 0)
        {
          printf("Error %i reading modbus registers: %s\n", errno, strerror(errno));
          modbus_connect(modbusContext);
        }
        else
        {
          uint16tox(modbusHex, modbusRegisters, modbus_address_len * 4);
          modbusHex[modbus_address_len * 4] = 0;
          // printf("Read registers at %i: %s\n", modbus_address, modbusHex);
          mosquitto_publish(mosq, NULL, modbus_topic, modbus_address_len * 4, modbusHex, 0, 0);
        }
      }
    }
  }

  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();

  if (hasModbus)
  {
    modbus_close(modbusContext);
    modbus_free(modbusContext);
  }

  close(serial_port);

  return 0;
}
