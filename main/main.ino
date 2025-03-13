#include <WiFiManager.h>
#include <MD_MAX72xx.h>
#include <SoftwareSerial.h>
#include "san_timer.h"
#include <pgmspace.h>
#include<EEPROM.h>

#include <DS3231.h> 
#include <NTPClient.h> 
#include <WiFiUdp.h> 
#include "config.h"

#include "DHT.h"

const char* ssid = "nishantwifi";
const char* password = "nishantm123";

//#define SAN_DBG_ENABLE
#include "san_utils.h"

/** ALARM Stuff **/

WiFiServer server(80); 

String header; 
unsigned long currentTime = millis(); 
unsigned long previousTime = 0; 
const long timeoutTime = 2000; 
String alarmState = "off"; 
DateTime alarm;
#define ALARM_LENGTH_MINUTES 1 

unsigned long currentProxymillis= millis(); 
unsigned long previousProxymillis= 0;
#define  proxyToggleTimeout 1000



void update_ntp_time() ;
int get_hour();
int get_minute();
int get_second();
int get_weekday();
int get_day();
int get_month();
int get_year();
void alarmCheck();

void printText(const char *pMsg);
void enable_remote();


/************************************************************** PROGRAM MEMORY ****************************************************************/

const char day_0[] PROGMEM = "Sun";
const char day_1[] PROGMEM = "Mon";
const char day_2[] PROGMEM = "Tue";
const char day_3[] PROGMEM = "Wed";
const char day_4[] PROGMEM = "Thu";
const char day_5[] PROGMEM = "Fri";
const char day_6[] PROGMEM = "Sat";

const char month_0[] PROGMEM = "Jan";
const char month_1[] PROGMEM = "Feb";
const char month_2[] PROGMEM = "Mar";
const char month_3[] PROGMEM = "Apr";
const char month_4[] PROGMEM = "May";
const char month_5[] PROGMEM = "Jun";
const char month_6[] PROGMEM = "Jul";
const char month_7[] PROGMEM = "Aug";
const char month_8[] PROGMEM = "Sep";
const char month_9[] PROGMEM = "Oct";
const char month_10[] PROGMEM = "Nov";
const char month_11[] PROGMEM = "Dec";



const char *const months_table[] PROGMEM = {month_0,month_1,month_2,month_3,month_4,month_5,month_6,month_7,month_8,month_9,month_10,month_11};

const char *const days_table[] PROGMEM = { day_0,  day_1,  day_2,  day_3,  day_4,  day_5,  day_6 };

/************************************************************** EEPROM  MEMORY ****************************************************************/

/******************************** MACROS ************************************************/
 
#define	EEPROM_SCROLL_SPEED		1

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES	5
#define CHAR_SPACING  1 // pixels between characters


#define BOOT_MSG 		"   Welcome to PESU Alarm Clock    "


#define DATA_PIN          12
#define CS_PIN            13
#define CLK_PIN           14  /** MUST be this PIN on ESP **/

#define BUZZER_PIN 		  5
#define DHTPIN            4
#define PROX_PIN          3

#define DHTTYPE DHT11 // DHT 11


#define BRIGHTNESS_LEVEL 		3

#define	DEFAULT_SCROLL_SPEED 	20
#define	MAX_SCROLL_SPEED		55
#define	BLINK_INTERVAL			500
#define	DATE_DISPLAY_INTERVAL	30000
#define	WIFI_UPDATE_INTERVAL	60000
#define REMOTE_IDLE_INTERVAL   	60000

#define RUNNING 0

/****************************************************************************************/

void *display_date(void *arg);
void *wifi_update(void *arg);
void *blink_dots(void *arg);
void irremote();
void *resetState(void *arg);

void update_ntp_time();
void ntp_setup();

/********************************* GLOBALS **********************************************/

unsigned char g_scroll_speed = DEFAULT_SCROLL_SPEED;
int currentState = RUNNING;


MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

bool refresh = true;


san_timer_lib timer_lib; // Main Timer Class

san_timer display_date_timer(DATE_DISPLAY_INTERVAL,0,display_date);
san_timer blink_dots_timer(BLINK_INTERVAL,0,blink_dots);
san_timer wifi_update_timer(WIFI_UPDATE_INTERVAL,0,wifi_update);
san_timer resetState_timer(REMOTE_IDLE_INTERVAL,0,resetState,false,false);


unsigned char get_scroll_speed_EEPROM()
{
	return EEPROM.read( EEPROM_SCROLL_SPEED);
}

void set_scroll_speed_EEPROM( unsigned char val )
{
  if( ( val < DEFAULT_SCROLL_SPEED ) || ( val >= MAX_SCROLL_SPEED ) )
  	set_scroll_speed_EEPROM(DEFAULT_SCROLL_SPEED);
 else
	EEPROM.write( EEPROM_SCROLL_SPEED, val );
}

/****************************************************************************************/
DHT dht(DHTPIN, DHTTYPE);


void setup()
{
  bool res;
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, BRIGHTNESS_LEVEL);
  mx.clear();
  scrollText(BOOT_MSG);
  mx.clear();
  scrollText("Connecting to WIFI...");

  Serial.begin(9600);

  WiFiManager wm;
  res = wm.autoConnect(ssid,password);
  mx.clear();

  SAN_DBG("g_chime_state = "); SAN_DBGLN(g_chime_state);
  //g_scroll_speed = get_scroll_speed_EEPROM();

  if( ( g_scroll_speed < DEFAULT_SCROLL_SPEED ) || ( g_scroll_speed >= MAX_SCROLL_SPEED ) )
    set_scroll_speed_EEPROM(DEFAULT_SCROLL_SPEED);

  SAN_DBG("g_scroll_speed = "); SAN_DBGLN(g_scroll_speed);

  timer_lib.add(&display_date_timer);
  timer_lib.add(&blink_dots_timer);
  timer_lib.add(&wifi_update_timer);
  timer_lib.add(&resetState_timer);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN,LOW);
  pinMode(PROX_PIN,INPUT);

  ntp_setup();
  wifi_update(NULL);
  mx.clear();
  dht.begin();

  server.begin(); // webserver

  String localIP;
  localIP.concat("    IP Address : ");
  localIP = WiFi.localIP().toString();
  localIP.concat("                 ");
  scrollText(localIP.c_str());
  mx.clear();


}

void resetDisplay()
{
	refresh=true;
	mx.clear();
}

void printText(const char *pMsg)
{
  uint8_t   state = 0;
  uint8_t   curLen;
  uint16_t  showLen;
  uint8_t   cBuf[8];
  uint8_t   modStart=0;
  uint8_t   modEnd = MAX_DEVICES - 1; 
  int16_t   col = ((modEnd + 1) * COL_SIZE) - 1;

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  do     // finite state machine to print the characters in the space available
  {
    switch(state)
    {
      case 0: // Load the next character from the font table
        // if we reached end of message, reset the message pointer
        if (*pMsg == '\0')
        {
          showLen = col - (modEnd * COL_SIZE);  // padding characters
          state = 2;
          break;
        }

        // retrieve the next character form the font file
        showLen = mx.getChar(*pMsg++, sizeof(cBuf)/sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state++;
        // !! deliberately fall through to next state to start displaying

      case 1: // display the next part of the character
        mx.setColumn(col--, cBuf[curLen++]);

        // done with font character, now display the space between chars
        if (curLen == showLen)
        {
          showLen = CHAR_SPACING;
          state = 2;
        }
        break;

      case 2: // initialize state for displaying empty columns
        curLen = 0;
        state++;
        // fall through

      case 3:  // display inter-character spacing or end of message padding (blank columns)
        mx.setColumn(col--, 0);
        curLen++;
        if (curLen == showLen)
          state = 0;
        break;

      default:
        col = -1;   // this definitely ends the do loop
    }
  } while (col >= (modStart * COL_SIZE));

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void start_resetState_timer()
{
	resetState_timer.start();
}

void stop_resetState_timer()
{
	resetState_timer.stop();
}


void *resetState(void *arg)
{
	SAN_DBGLN("resetState");
	currentState = RUNNING;
	resetDisplay();
	enableTimers();
	display_date_timer.reset();
    return NULL;
}


void resetTimers()
{
	display_date_timer.reset();
	resetState_timer.reset();
}

void disableTimers()
{
	display_date_timer.disable();
	blink_dots_timer.disable();
	wifi_update_timer.disable();
}

void enableTimers()
{
	display_date_timer.enable();
	blink_dots_timer.enable();
	wifi_update_timer.enable();
}


void buzzer(int dur, int repeat)
{
	for(int i=0;i<repeat;i++)
	{
    	digitalWrite(BUZZER_PIN,LOW);
		delay(dur);
    	digitalWrite(BUZZER_PIN,HIGH);
		delay(dur/2);
	}
}

void scrollText(const char *p)
{
  uint8_t charWidth;
  uint8_t cBuf[50];  // this should be ok for all built-in fonts

  while (*p != '\0')
  {
    charWidth = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);

    for (uint8_t i=0; i<=charWidth; i++)	// allow space between characters
    {
      mx.transform(MD_MAX72XX::TSL);
      if (i < charWidth)
        mx.setColumn(0, cBuf[i]);
      delay(g_scroll_speed);
    }
  }
  mx.clear();
}

void    display_hour_tenth_char( int pos, uint16_t c)
{
    mx.setChar(pos,155);
    mx.setChar(pos,c);
}


void display_dots( int pos, uint16_t c)
{

    mx.setChar(pos,c);
}


void display_char( int pos, uint16_t c)
{
		uint8_t size=10;
		uint8_t buf[10];
		uint16_t prev_c;

		if( c > 0 )
			prev_c = c - 1;
		else
			prev_c = 0;

		size = mx.getChar(prev_c, size, buf);

		/** erase previous pixels of size **/

		if(size == 2 )
    	mx.setChar(pos,32);
		if(size == 3 )
    	mx.setChar(pos,152);
		if(size == 4 )
    	mx.setChar(pos,154);
		if(size == 5 )
    	mx.setChar(pos,151);

    	mx.setChar(pos,c);
}

void display_hour(int hour, bool refresh)
{
	char h_digit_1;
	char h_digit_2;
	static int pre_hour=-1;

	if( hour > 12 )
		hour -= 12;
	if( hour == 0 )
		hour = 12;


	if( ( hour == pre_hour ) && ( refresh == false ) ) return;
	pre_hour=hour;


	h_digit_1 = '0' + 1;

	if( hour > 9 )
	{
		h_digit_2 = hour % 10;
		h_digit_2 = '0' + h_digit_2;
    display_char(4*COL_SIZE+3, h_digit_2);
    display_hour_tenth_char(5*COL_SIZE-1, h_digit_1);
	} 
	else
	{
		h_digit_2 = '0' + hour;
    display_char(4*COL_SIZE+3, h_digit_2);
    mx.setChar(5*COL_SIZE-1,155);
	}
}


void display_min(int min, bool refresh)
{
	char m_digit_1;
	char m_digit_2;
	static int pre_min =-1;

	if( ( min  == pre_min  )  && ( refresh == false ) )return;
	pre_min = min;

	if( min > 9 )
	{
		m_digit_2 = min % 10;
		m_digit_2 = '0' + m_digit_2;

		m_digit_1 = min / 10;
		m_digit_1 = '0' + m_digit_1;
    display_char(2*COL_SIZE+4, m_digit_2);
    display_char(3*COL_SIZE+2, m_digit_1);
	} 
	else
	{
		m_digit_2 = '0' + min;
		m_digit_1 = '0';
    display_char(2*COL_SIZE+4, m_digit_2);
    display_char(3*COL_SIZE+2, m_digit_1);
	}
}

void display_sec(int sec, bool refresh)
{
  static char prev_m_digit_1 = -1;
  static char m_digit_1;
  char m_digit_2;
    static int pre_sec =-1;


	if( ( sec  == pre_sec  ) && ( refresh == false ) )  { return;}
	pre_sec =sec;

  if( sec > 9 )
  {
    m_digit_2 = sec % 10;
    m_digit_2 = '0' + m_digit_2;

    m_digit_1 = sec / 10;
    m_digit_1 = '0' + m_digit_1;
    display_char(1*COL_SIZE-3, m_digit_2);
		if( (prev_m_digit_1 != m_digit_1 ) || ( refresh == true ) )
		{
    	display_char(2*COL_SIZE-5, m_digit_1);
			prev_m_digit_1 = m_digit_1 ;
		}
  } 
  else
  {
    m_digit_2 = '0' + sec;
    m_digit_1 = '0';
    display_char(1*COL_SIZE-3, m_digit_2);
		if( (prev_m_digit_1 != m_digit_1 ) || ( refresh == true ) )
		{
    	display_char(2*COL_SIZE-5, m_digit_1);
			prev_m_digit_1 = m_digit_1;
		}
  }
	prev_m_digit_1 = m_digit_1;
}

#define NOCOLON 150
#define BIG_COLON 156
#define COLON 149

void *blink_dots(void *arg)
{
		static int blink = 1;
        int colon;

        if( alarmState == "off" )
            colon = COLON;
        else
            colon = BIG_COLON;

		if( currentState != RUNNING ) return NULL;

		blink = (-1)*blink;

		if( blink == 1 )
		{
    		display_dots(4*COL_SIZE-3, NOCOLON);
    		display_dots(2*COL_SIZE-2, NOCOLON);
		} 
		else 
		{
    		display_dots(4*COL_SIZE-3, colon);
    		display_dots(2*COL_SIZE-2, colon);
		}
        return NULL;
}

void *wifi_update(void *arg)
{
  update_ntp_time();
  //timeClient.update(); 
  return NULL;
}


void showTime(bool refresh)
{
		display_hour(get_hour(),refresh);
		display_min(get_minute(),refresh);
    	display_sec(get_second(),refresh);
		refresh=false;
  		chime( get_hour(), get_minute(), get_second() );
		mx.update();
}

void *display_date(void *arg)
{
	char day_buf[5];
	char mon_buf[5];

	if( currentState != RUNNING ) return NULL;
	refresh=true;

	String date = String("     Today's date -> ");
	strcpy_P(day_buf, (char *)days_table[get_weekday()]);
	strcpy_P(mon_buf, (char *)months_table[get_month()]);

	
	date.concat(day_buf);
	date.concat(" / ");
	date.concat(get_day());
	date.concat(" / ");
	date.concat( mon_buf );

	date.concat(" / ");
	date.concat(get_year());
	date.concat("             ");
	scrollText(date.c_str());
	mx.clear();
	chkReminder();
    displayTemp();
    return NULL;
}

// void chkReminder()
// {
// 	String tmpDate = String("");;
// 	char tmpbuf[50];

// 	tmpDate.concat(get_day());
// 	tmpDate.concat(get_month() + 1);

// 	for(int i=0; i<g_num_rem; i++)
// 	{
// 		strcpy_P(tmpbuf,(char *)rem_date_table[i]);
// 		if( !strcmp(tmpDate.c_str(), tmpbuf) )
// 		{
// 			strcpy_P(tmpbuf,(char *)rem_msg_table[i]);
// 			scrollText( tmpbuf );

// 		}

// 	}

// }


void displayTemp()
{
  // Reading temperature or humidity takes about 250 milliseconds!
// Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)

float h = dht.readHumidity();
float t = dht.readTemperature();

// Check if any reads failed and exit early (to try again).
if (isnan(h) || isnan(t) ) {
//Serial.println(F("Failed to read from DHT sensor!"));
return;
}

String weatherData;

weatherData.concat("      Humidity = ");
weatherData.concat(h);
weatherData.concat(" Temp = ");
weatherData.concat(t);
weatherData.concat(" C           ");
scrollText(weatherData.c_str());
mx.clear();
}

void proxi_chk()
{
    if( (digitalRead(PROX_PIN)==LOW) )
    {
        currentProxymillis = millis();
        if( currentProxymillis - previousProxymillis >= proxyToggleTimeout )
        {
            if( alarmState == "off" )
                alarmState = "on";
            else
                alarmState = "off";
            previousProxymillis = currentProxymillis;
        }
    }
    if( alarmState == "off" )
    {
        digitalWrite(BUZZER_PIN,LOW);
    }
}

void playAlarm()
{
  if(   ( alarmState == "on" ) && 
        ( get_hour() == alarm.hour() ) && 
        ( alarm.minute() == get_minute() )  && 
        ( get_minute() <= ( alarm.minute()+ALARM_LENGTH_MINUTES) ) )
  {
    Serial.println("alarm");
    digitalWrite(BUZZER_PIN,HIGH);
  }
}

void loop()
{
  	timer_lib.run();
	showTime(refresh);
	refresh=false;
    proxi_chk();
    alarmCheck();
    playAlarm();
    delay(1);
}


void alarmCheck()
{
  time_t now;
  time(&now);
  DateTime today = DateTime(now);
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client is connected
      currentTime = millis();         
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          if (currentLine.length() == 0) { // If the current line is blank, you got two newline characters in a row. That's the end of the client HTTP request, so send a response:
            client.println("HTTP/1.1 200 OK"); // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            client.println("Content-type:text/html"); // and a content-type so the client knows what's coming, then a blank line:
            client.println("Connection: close");
            client.println();
            
            if (header.indexOf("GET /alarm/on") >= 0) { // If the user clicked the alarm's on button
              Serial.println("Alarm enabled");
              alarmState = "on";
            } 
            else if (header.indexOf("GET /alarm/off") >= 0) { // If the user clicked the alarm's off button
              Serial.println("Alarm disabled");
              alarmState = "off";
            }

            else if (header.indexOf("GET /time") >= 0) { // If the user submitted the time input form
              // Strip the data from the GET request
              int index = header.indexOf("GET /time");
              String timeData = header.substring(index + 15, index + 22);
              int hour = timeData.substring(0, 2).toInt();
              int minute = timeData.substring(5,7).toInt();
              
              Serial.println(timeData);
              // Update our alarm DateTime with the user selected time, using the current date.
              // Since we just compare the hours and minutes on each loop, I do not think the day or month matters.
              //DateTime temp = DateTime(timeClient.getEpochTime());
              DateTime temp = DateTime(now);
              alarm = DateTime(22, temp.month(), temp.day(), hour, minute, 0);
            }
            
            // Display the HTML web page
            // Head
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<link rel=\"stylesheet\" href=\"//stackpath.bootstrapcdn.com/bootstrap/4.4.1/css/bootstrap.min.css\">"); // Bootstrap
            client.println("</head>");
            
            // Body
            client.println("<body>");
            client.println("<h1 class=\"text-center mt-3\">Alarm Clock</h1>"); // Title

            // Current Time
            client.print("<h4 class=\"text-center\">"); 
            client.print(ctime(&now));
            client.println("</h4>");

            // Alarm
            client.println("<form action=\"/time\" method=\"GET\">");
            // Time Input
            client.print("<p class=\"text-center\"><input type=\"time\" id=\"time\" name=\"time\" value=\"");

            // Prepend a "0" to the hour and minute if needed, otherwise HTML gets cranky 
            String hour = String(alarm.hour());
            if(hour.length() == 1){
              hour = "0" + hour;
            }
            String minute = String(alarm.minute());
            if(minute.length() == 1){
              minute = "0" + minute;
            }

            // Pre-fill the value of the form with the current alarm time
            client.print(hour);
            client.print(":");
            client.print(minute);
            client.println("\" required>"); // Make the input required, so user cannot submit a null time
            
            // Submit Button
            client.println("<input type=\"submit\" class=\"btn btn-sm btn-success\" value=\"Submit\">");
            client.println("</form>");
            
            // Display current state, and ON/OFF buttons for Alarm  
            client.println("<h6 class=\"text-center\">Alarm State - " + alarmState + "</h6>");
            if (alarmState=="off") {
              client.println("<p class=\"text-center\"><a href=\"/alarm/on\"><button class=\"btn btn-sm btn-danger\">ON</button></a></p>");
            }
            else {
              client.println("<p class=\"text-center\"><a href=\"/alarm/off\"><button class=\"btn btn-success btn-sm\">OFF</button></a></p>");
            }
            client.println("</body></html>");
            client.println(); // The HTTP response ends with another blank line
            break; // Break out of the while loop
            
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    
    header = ""; // Clear the header variable
    client.stop(); // Close the connection
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}