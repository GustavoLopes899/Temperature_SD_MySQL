// Program used to monitor the temperature with LM35 sensor and save data on MySQL's database //
// If the connection is not possible, save the data on SD card to future recovery //

#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <Time.h>
#include <TimeLib.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

#define READINGS 10             // number of readings to take the average temperature
#define FIRSTCONNECT 0          // auxiliar variable to mysql function
#define RECONNECT 1             // auxiliar variable to mysql function

/*--------------- Pins ---------------*/
const int ledPin = 8;
const int tempPin = A3;
const int sdPin = 4;

/*--------------- Aux ---------------*/
String led_operation;                     // variable used to control the led on http access
char ch;                                  // reads char by char to build led_operation variable
int led_status = 0;                       // used to control the status of led in the html page
float reading[READINGS];                  // variable to save all the readings to take the average
const float voltage_reference = 1.1;      // used to change the reference's voltage, could be changed depending of the board used
int count = 0;
float average = 0;

/*--------------- Ethernet ---------------*/
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};      // mac address of the board
IPAddress ip(10, 156, 10, 13);                          // ip address of the board
EthernetServer server(80);                              // server to external access

/*--------------- MySQL ---------------*/
IPAddress server_addr(10, 156, 10, 164);                // server ip address
char user[] = "root";                                   // mysql user
char password[] = "root";                               // mysql password
char DATABASE[] = "USE db_freezer";                     // mysql command to use a specific database
String sentence;                                        // sentence to do a insert in database
EthernetClient client;
MySQL_Connection conn((Client *)&client);

/*--------------- Log ---------------*/
File myFile;
String fileName = "log_temp.txt";       // filenames's lenght is limmited to 8 characters
String actualHour = "", actualMinute = "", actualSecond = "";
String actualMonth = "", actualDay = "";

/*--------------- NTP Server ---------------*/
EthernetUDP Udp;
const int NTP_PACKET_SIZE = 48;         // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];     // buffer to hold incoming & outgoing packets
unsigned int localPort = 8888;          // local port to listen for UDP packets
IPAddress timeServer(200, 160, 7, 186); // ntp.br
const int timeZone = -3;                // UTC -3  BRT Brasília Time
const int interval = 86400;             // Number of seconds between re-syncs (86400s = 24hs)

void setup() {
  Serial.begin(9600);
  while (!Serial);                      // wait for serial port to connect. Needed for native USB port only
  analogReference(INTERNAL1V1);
  pinMode(tempPin, INPUT);
  pinMode(ledPin, OUTPUT);
  Ethernet.begin(mac, ip);
  server.begin();

  /*--------------- SD Inicialization ---------------*/
  Serial.print("Initializing SD card... ");
  if (!SD.begin(sdPin)) {
    Serial.println("initialization failed!");
    Serial.println();
    while (1);
  }
  Serial.println("initialization done.");
  Serial.println();

  /*--------------- MySQL Inicialization ---------------*/
  connectMySQL(FIRSTCONNECT);

  /*--------------- NTP Inicialization ---------------*/
  Udp.begin(localPort);
  Serial.println("Waiting for sync in NTP server...");
  setSyncProvider(getNtpTime);

  getActualDate();
}

void loop() {
  char insert[100] = "";

  // test if backup file is empty //
  myFile = SD.open(fileName, FILE_WRITE);
  if (myFile.size() != 0 && conn.connected()) {
    Serial.println("File contains data... Trying to read it...");
    readFile();
  }

  checkChangeDay();
  reading[count] = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;
  Serial.print("Actual Temperature [");
  if (count < 9) {
    Serial.print("0");
  }
  Serial.print(count + 1);
  Serial.print("]: ");
  Serial.print((float)reading[count]);
  Serial.println("°C");
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
          // Código HTML //
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("<title>Sensor de Temperatura </title>");
          client.println("<div align=center>");
          client.println("<a href=https://www.climatempo.com.br target='_blank'/><img src=https://www.luisllamas.es/wp-content/uploads/2015/10/arduino-sensor-temperatura-interno.png></a><br />");
          client.println("<div>");
          client.println("<h3 style='display:inline;'>LED Status: </h3>");
          client.print("<h3 style='display:inline; ");
          if (led_status == 1) {
            client.print("color:green;'> on");
          } else {
            client.print("color:red;'> off");
          }
          client.println("</h3>");
          client.println("</div>");
          client.println("<br/>");
          client.println("<form action='' method='post'>");
          client.println("<button name='operation' type='submit' value='on'>Turn ON LED</button>");
          client.println("<button name='operation' type='submit' value='off'>Turn OFF LED</button>");
          client.println("</form>");
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
          while (client.available()) {
            ch = client.read();
            led_operation.concat(ch);
          }
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
    if (led_operation == "operation=on") {
      digitalWrite(ledPin, HIGH);
      led_status = 1;
    } else if (led_operation == "operation=off") {
      digitalWrite(ledPin, LOW);
      led_status = 0;
    }
    led_operation = "";
  }
  count++;
  if (count == READINGS) {
    // Save a log after Y readings
    for (int i = 0; i < READINGS; i++) {
      average += reading[i] / READINGS;
    }
    Serial.println();
    Serial.print("Average Temperature: ");
    Serial.print(average);
    Serial.println("°C");
    Serial.println();
    getActualTime();
    String actualDate = String(year()) + "-" + actualMonth + "-" + actualDay;
    String actualTime = actualHour + ":" + actualMinute + ":" + actualSecond;

    // Check the connection with the database //
    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    cur_mem->execute(DATABASE);
    delete cur_mem;

    if (!conn.connected()) {
      Serial.println("Network's connection failed... Saving on temporary file on SD Card");
      Serial.println();
      // save average to log file - format '2018-07-28 16:13:45 24.54' //
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
      // save on db //
      sentence = "INSERT INTO temperatura (dataCaptura, horario, temperatura) VALUES ('" + actualDate + "', '" + actualTime + "', " + String(average) + ");";
      sentence.toCharArray(insert, sentence.length() + 1);
      MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
      cur_mem->execute(insert);
      delete cur_mem;
      Serial.println("Average temperature saved on MySQL's database.");
      Serial.println();
    }
    count = 0;
    average = 0;
  }
  delay(500);    // wait for x milliseconds before taking the reading again
}


// function to get a formatted date to string //
void getActualDate() {
  if (month() < 10) {
    actualMonth = 0 + String(month());
  } else {
    actualMonth = String(month());
  }
  if (day() < 10) {
    actualDay = 0 + String(day());
  } else {
    actualDay = String(day());
  }
}

// function to check changes on day and update, if necessary //
void checkChangeDay() {
  String checkNewDay;
  if (day() < 10) {
    checkNewDay = 0 + String(day());
  } else {
    checkNewDay = String(day());
  }
  if (checkNewDay != actualDay) {
    getActualDate();
  }
}

// function to get a formatted time to string //
void getActualTime() {
  if (hour() < 10) {
    actualHour = 0 + String(hour());
  } else {
    actualHour = String(hour());
  }
  if (minute() < 10) {
    actualMinute = 0 + String(minute());
  } else {
    actualMinute = String(minute());
  }
  if (second() < 10) {
    actualSecond = 0 + String(second());
  } else {
    actualSecond = String(second());
  }
}

/*-------- NTP Code ----------*/
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

/*-------- MySQL Code ----------*/
boolean connectMySQL(int type) {
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
  if (!conn.connected()) {
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
  int number = 3;
  String temp;
  char ch;
  char sql_query[100] = "";
  String data[number];
  int count = 0;
  Serial.println("Leitura do arquivo:");
  Serial.println();
  File myFile = SD.open(fileName);
  if (myFile) {
    while (myFile.available()) {
      ch = myFile.read();
      while (ch != ' ' && ch != '\n') {
        data[count] += ch;
        ch = myFile.read();
      }
      // prints to debug //
      /*Serial.print("Data[");
        Serial.print(count);
        Serial.print("]= ");
        Serial.println(data[count]);*/
      count++;
      if (count == number) {
        temp = "INSERT INTO temperatura (dataCaptura, horario, temperatura) VALUES ('" + data[0] + "', '" + data[1] + "', " + data[2] + ");";
        temp.toCharArray(sql_query, temp.length() + 1);
        if (connectMySQL(RECONNECT)) {
          MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
          cur_mem->execute(sql_query);
          delete cur_mem;

          for (int i = 0; i < count; i++) {
            data[i] = "";
          }
          count = 0;
          Serial.println();
          delay(1000);

        }
      }
    }
  } else {
    Serial.println("error opening the log file.");
  }
  myFile.close();
  SD.remove(fileName);
}

