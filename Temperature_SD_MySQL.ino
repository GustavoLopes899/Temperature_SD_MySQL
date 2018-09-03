#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <Time.h>
#include <TimeLib.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

#define READINGS 100        // number of readings to take the average temperature
#define FIRSTCONNECT 0      // auxiliar variable to mysql function
#define RECONNECT 1         // auxiliar variable to mysql function
#define TAM_S 11            // TAM SMALL
#define TAM_L 75            // TAM LARGE
#define PARAMETERS 3        // auxiliar variable to readFile's function
#define PARAMETERS_SIZE 11  // auxiliar variable to readFile's function

//--------------- Pins ---------------//
const int tempPin = A3;
const int sdPin = 4;

//--------------- Aux ---------------//
float reading[READINGS];                  // variable to save all the readings to take the average
const float voltage_reference = 1.1;      // used to change the reference's voltage, could be changed depending of the board used
int count = 0;                            // variable to control the number of readings
float average = 0;                        // average temperature variable

//--------------- Ethernet ---------------//
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};    // mac address of the board
IPAddress ip(10, 156, 10, 13);                        // ip address of the board
EthernetServer server(80);                            // server to external access
char ch;

//--------------- MySQL ---------------//
IPAddress server_addr(10, 156, 10, 164);              // server ip address
const char user[] = "root";                           // mysql user
const char password[] = "root";                       // mysql password
const char DATABASE[] = "USE db_freezer";         // mysql command to use a specific database
char sentence[TAM_L];                                 // sentence to do a insert in database 
EthernetClient client;
MySQL_Connection conn((Client *)&client);

//--------------- Log ---------------//
File myFile;
const char fileName[] = "log_temp.txt";       // filenames's lenght is limmited to 8 characters
char actualHour[TAM_S] = "", actualMinute[TAM_S] = "", actualSecond[TAM_S] = "";
char actualYear[TAM_S] = "", actualMonth[TAM_S] = "", actualDay[TAM_S] = "";

//--------------- NTP Server ---------------//
EthernetUDP Udp;
const int NTP_PACKET_SIZE = 48;         // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];     // buffer to hold incoming & outgoing packets
unsigned int localPort = 8888;          // local port to listen for UDP packets
IPAddress timeServer(200, 160, 7, 186); // ntp.br server ip address
const int timeZone = -3;                // UTC -3  BRT Brasília Time
const int interval = 86400;             // Number of seconds between re-syncs (86400s = 24hs)

void setup() {
  Serial.begin(9600);
  while (!Serial);                      // Wait for serial port to connect. Needed for native USB port only
  analogReference(INTERNAL1V1);
  pinMode(tempPin, INPUT);
  Ethernet.begin(mac, ip);
  server.begin();

  //--------------- SD Inicialization ---------------//
  Serial.print("Initializing SD card... ");
  if (!SD.begin(sdPin)) {
    Serial.println("initialization failed!");
    Serial.println();
    while (1);
  }
  Serial.println("initialization done.");
  Serial.println();

  //--------------- MySQL Inicialization ---------------//
  connectMySQL(FIRSTCONNECT);

  //--------------- NTP Inicialization ---------------//
  Udp.begin(localPort);
  Serial.println("Waiting for sync in NTP server...");
  setSyncProvider(getNtpTime);

  getActualDate();
}

void loop() {
  // Check the connection with the database //
  MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
  cur_mem->execute(DATABASE);
  delete cur_mem;

  // test if backup file is empty //
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile.size() != 0 && conn.connected()) {
    Serial.println("File contains data... Trying to read it and write on database...");
    Serial.println();
    readFile();
  }
  myFile.close();

  checkChangeDay();
  reading[count] = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;
  Serial.print(F("Actual Temperature ["));
  if (count < 9) {
    Serial.print("0");
  }
  Serial.print(count + 1);
  Serial.print(F("]: "));
  Serial.print((float)reading[count]);
  Serial.println(F("°C"));
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html; charset=utf-8");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          client.println();
          // HTML Code //
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("<title>Temperature Sensor </title>");
          client.println("<div align=center>");
          client.println("<a href=https://www.climatempo.com.br target='_blank'/><img src=https://www.luisllamas.es/wp-content/uploads/2015/10/arduino-sensor-temperatura-interno.png></a><br />");
          client.println("<h2>Arduino Webserver with LM-35's sensor temperature</h2>");
          client.println("<tr><td><hr size=4 color=#0099FF> </td></tr>");
          client.print("<h2>Current temperature is: </h2>");
          client.print("<h1 style='color:blue;'>");
          client.println((float)reading[count]);
          client.print(" &#8451;");
          client.println("</h1>");
          client.println("<br/>");
          client.println("</div>");
          client.println("</html>");
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
  }
  count++;
  if (count == READINGS) {
    // Save average temperature after Y readings
    for (int i = 0; i < READINGS; i++) {
      average += reading[i] / READINGS;
    }
    Serial.println();
    Serial.print("Average Temperature: ");
    Serial.print(average);
    Serial.println("°C");
    Serial.println();
    getActualTime();
    char actualDate[TAM_S];
    sprintf(actualDate, "%d-%s-%s", year(), actualMonth, actualDay);
    char actualTime[TAM_S];
    sprintf(actualTime, "%s:%s:%s", actualHour, actualMinute, actualSecond);

    if (!conn.connected()) {
      Serial.println("Network's connection failed... Saving on temporary file on SD Card");
      Serial.println();
      // save average to log file - format '2018-07-28 16:13:45 20.54' //
      myFile = SD.open(fileName, FILE_WRITE);
      myFile.print(year());
      myFile.print("-");
      myFile.print(actualMonth);
      myFile.print("-");
      myFile.print(actualDay);
      myFile.print(" ");
      myFile.print(actualHour);
      myFile.print(":");
      myFile.print(actualMinute);
      myFile.print(":");
      myFile.print(actualSecond);
      myFile.print(" ");
      myFile.println(average);
      myFile.close();
      connectMySQL(RECONNECT);
    } else {
      // save data on db //
      char avg[TAM_S];
      dtostrf(average, 5, 2, avg);      // float to char array
      sprintf(sentence, "INSERT INTO temperatura (dataCaptura, horario, temperatura) VALUES ('%s', '%s', %s);", actualDate, actualTime, avg);
      MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
      cur_mem->execute(sentence);
      delete cur_mem;
      Serial.println("Average temperature saved on MySQL's database.");
      Serial.println();
    }
    count = 0;
    average = 0;
  }
  delay(500);    // wait before taking the reading again
}

// function to get a formatted date to char array //
void getActualDate() {
  if (month() < 10) {
    sprintf(actualMonth, "0%d", month());
  } else {
    sprintf(actualMonth, "%d", month());
  }
  if (day() < 10) {
    sprintf(actualDay, "0%d", day());
  } else {
    sprintf(actualDay, "%d", day());
  }
}

// function to check changes on day and update, if necessary //
void checkChangeDay() {
  char checkNewDay[TAM_L];
  if (day() < 10) {
    sprintf(checkNewDay, "0%d", day());
  } else {
    sprintf(checkNewDay, "%d", day());
  }
  if (checkNewDay != actualDay) {
    getActualDate();
  }
}

// function to get a formatted time to char array //
void getActualTime() {
  if (hour() < 10) {
    sprintf(actualHour, "0%d", hour());
  } else {
    sprintf(actualHour, "%d", hour());
  }
  if (minute() < 10) {
    sprintf(actualMinute, "0%d", minute());
  } else {
    sprintf(actualMinute, "%d", minute());
  }
  if (second() < 10) {
    sprintf(actualSecond, "0%d", second());
  } else {
    sprintf(actualSecond, "%d", second());
  }
}

// function to initialize NTP synchronization //
time_t getNtpTime() {
  while (Udp.parsePacket() > 0) ;               // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response OK");
      Serial.println();
      setSyncInterval(interval);
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :(");
  Serial.println();
  return 0;                                    // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address //
void sendNTPpacket(IPAddress &address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

// function to connect on MySQL server //
boolean connectMySQL(int type) {
  if (!conn.connected()) {
    switch (type) {
      case FIRSTCONNECT: {
          Serial.print("Initializing MySQL connection... ");
          break;
        }
      case RECONNECT: {
          Serial.print("Reconnecting MySQL server... ");
          break;
        }
    }
    if (conn.connect(server_addr, 3306, user, password)) {
      delay(1000);
      Serial.println("connection sucessfull.");
      Serial.println();
      MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
      cur_mem->execute(DATABASE);
      delete cur_mem;
      return true;
    } else {
      Serial.println("connection failed.");
      Serial.println();
      return false;
    }
  }
}

// function which reads a backup file and try to save to a db //
void readFile() {
  int index = 0;
  char temp[TAM_L];
  char ch;
  char data[PARAMETERS][PARAMETERS_SIZE];
  int count = 0;
  Serial.println("Reading file...");
  Serial.println();
  File myFile = SD.open(fileName);
  if (myFile) {
    while (myFile.available()) {
      ch = myFile.read();
      while (ch != ' ' && ch != '\n') {
        data[count][index] = ch;
        ch = myFile.read();
        index++;
      }
      data[count][index] = 0;
      count++;
      index = 0;
      if (count == PARAMETERS) {
        // prints to debug //
        //for (int i = 0; i < count; i++) {
        //  Serial.print("Data[");
        //  Serial.print(i);
        //  Serial.print("]= ");
        //  Serial.println(data[i]);
        //}
        sprintf(temp, "INSERT INTO temperatura (dataCaptura, horario, temperatura) VALUES ('%s', '%s', %s);", data[0], data[1], data[2]);
        
        // Check the connection with the database //
        MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
        cur_mem->execute(DATABASE);
        delete cur_mem;

        if (connectMySQL(RECONNECT)) {
          MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
          cur_mem->execute(temp);
          delete cur_mem;
          Serial.println("Data saved from backup file.");
          for (int i = 0; i < count; i++) {
            data[i][0] = 0;
          }
          count = 0;
          delay(500);
        } else {
          return;
        }
      }
    }
  } else {
    Serial.println("Error opening the log file.");
    return;
  }
  Serial.println();
  Serial.println("Backup file successful saved on database.");
  Serial.println();
  myFile.close();
  SD.remove(fileName);
}
