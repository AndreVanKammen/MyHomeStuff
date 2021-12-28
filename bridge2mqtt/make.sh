sudo systemctl stop bridge2mqtt.service
gcc -Wall bridge2mqtt.cpp -o bridge2mqtt -l mosquitto -l modbus && {
  sudo cp bridge2mqtt.service /etc/systemd/system/
  sudo systemctl start bridge2mqtt.service
  sudo systemctl enable bridge2mqtt.service
  sudo systemctl status bridge2mqtt.service
}