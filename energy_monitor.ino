/*
   Telnet server and SparkFun data client (data.sparkfun.com). 
   
   Created:     Vassilis Serasidis
   Date:        30 Jul 2014
   Last update: 22 Aug 2014
   Version:     1.01
   Home:        http://www.serasidis.gr
   email:       avrsite@yahoo.gr , info@serasidis.gr
   
   Tested with Arduino IDE 1.5.7 and UIPEthernet v1.57
  
 -= SparkFun data client =-
 
    * Create your own data stream (https://data.sparkfun.com)
    * You can mark your data as public or private during the data stream creation.
    * Replace PUBLIC_KEY and PRIVATE_KEY with these sparkfun give you
    * Replace the data names "humidity", "maxTemp" etc on sendToSparkfunDataServer() with those
      gave you sparkfun data stream creation.
    * You can view online or download your data as CSV or JSON file via 
      https://data.sparkfun.com/streams/ + YOUR_PUBLIC_KEY
      
      https://data.sparkfun.com/streams/WGGWNZLKGOFAzyLwLOzQ
      
      GET /input/WGGWNZLKGOFAzyLwLOzQ?private_key=XRRmzj9YR2iXzjnKn6zR&humidity=25.81&maxTemp=26.94&nowTemp=14.48

      1000 imp = 1Kwh
 */

#include <SPI.h>
#include <Ethernet.h>
#include <stdio.h>
#include "keys.h"

// Enable or disable logging to diferent servres by comenting or uncomenting the next lines
#define SPARKFUN_LOG
#define THINGSPEAK_LOG 

#ifdef SPARKFUN_LOG
  char sparkfunDataServer[] = "data.sparkfun.com";
  //#define PUBLIC_KEY  "yourpublickey" //Your SparkFun public_key
  //#define PRIVATE_KEY "yourprivatekey" //Your SparkFun private_key
  //#define DELETE_KEY "not used"  //Your SparkFun delte_key
  #define SPARKFUN_UPDATE_TIME 60000         //Update SparkFun data server every 60000 ms (1 minute).
  //#define SPARKFUN_UPDATE_TIME 300000         //Update SparkFun data server every 300000 ms (5 minutes).
#endif

#ifdef THINGSPEAK_LOG
  #include "ThingSpeak.h"
  //unsigned long myChannelNumber = your channel number;
  //const char * myWriteAPIKey = "your API key";
#endif



#define TIMEOUT 1000 //1 second timout

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xAE, 0xCD };
IPAddress ip(192, 168, 1, 250); //Your local IP if DHCP fails.
IPAddress dnsServerIP(192, 168, 2, 254);
IPAddress gateway(192, 168, 2, 254);
IPAddress subnet(255, 255, 255, 0);

// Initialize the Ethernet client.
EthernetClient client;

unsigned long timer1 = 0;
unsigned long timer2 = 0;

int failedResponse = 0;
String power;

// VARS ///////////////
const int chan1_pin = 2;
const int chan2_pin = 3;
const float w_per_pulse = 1;
const unsigned long ms_per_hour = 3600000UL;

volatile unsigned int chan1_count = 0;
volatile unsigned int chan2_count = 0;
unsigned long report_time = 0;
volatile unsigned long chan1_first_pulse = 0;
volatile unsigned long chan1_last_pulse = 0;
volatile unsigned long chan2_first_pulse = 0;
volatile unsigned long chan2_last_pulse = 0;

static unsigned int chan1_watts;
unsigned long chan1_delta; // Time since last pulse
static unsigned int chan2_watts;
unsigned long chan2_delta; // Time since last pulse

////////////// interrupts//////////////////////////
void chan1_isr() {    // chan1_pulse
  chan1_count++;
  chan1_last_pulse = millis();
  if (chan1_count == 1) { // was reset
    chan1_first_pulse = chan1_last_pulse;
  }
}
void chan2_isr() {    // chan2_pulse
  chan2_count++;
  chan2_last_pulse = millis();
  if (chan2_count == 1) { // was reset
    chan2_first_pulse = chan2_last_pulse;
  }
}
////////////////////////////////////////


//----------------------------------------------------------------------
void setup()
{

  //setup code
  pinMode(chan1_pin, INPUT);
  pinMode(chan2_pin, INPUT);
  attachInterrupt(0, chan1_isr, RISING);
  attachInterrupt(1, chan2_isr, RISING);
  ///////////////////////////////////////


  //Initiallize the serial port.
  Serial.begin(9600);
  #ifdef SPARKFUN_LOG
    Serial.println("-= Init SparkFun data client =-\n");
  #endif
  #ifdef THINGSPEAK_LOG
    ThingSpeak.begin(client);
    Serial.println("-= Init ThingSpeak data client =-\n");
  #endif

  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0)
  {
    Serial.println("Failed to configure Ethernet using DHCP");
    // DHCP failed, so use a fixed IP address:
    Ethernet.begin(mac, ip, dnsServerIP, gateway, subnet);
  }
  Serial.print("LocalIP:\t\t");
  Serial.println(Ethernet.localIP());
  Serial.print("SubnetMask:\t\t");
  Serial.println(Ethernet.subnetMask());
  Serial.print("GatewayIP:\t\t");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("dnsServerIP:\t\t");
  Serial.println(Ethernet.dnsServerIP());
}
  
//----------------------------------------------------------------------
//
//----------------------------------------------------------------------
void loop()
{
  //Update sparkfun data server every 60 seconds.
  if(millis() > timer1 + SPARKFUN_UPDATE_TIME)
  {
      timer1 = millis(); //Update timer1 with the current time in miliseconds.
      power_calculations();   // Calculate power
      
      #ifdef SPARKFUN_LOG 
      sendToSparkfunDataServer(); //Send data to sparkfun data server.
      #endif
      
      #ifdef THINGSPEAK_LOG 
      sendToThingspeakServer();   //Send data to thingspeak data server.
      #endif
  }
}

//----------------------------------------------------------------------
#ifdef SPARKFUN_LOG
void sendToSparkfunDataServer()
{ 
    //Establish a TCP connection to sparkfun server.
    if (client.connect(sparkfunDataServer, 80))
    {
        //if the client is connected to the server...
        if(client.connected())
        {
            Serial.println("Sending data to SparkFun...");
            // send the HTTP PUT request:
            client.print("GET /input/");
            client.print(PUBLIC_KEY);
            client.print("?private_key=");
            client.print(PRIVATE_KEY);
            client.print("&power=");
            client.println(chan1_watts);    //send the number stored in 'humidity' string. Select only one.
         
            delay(1000); //Give some time to Sparkfun server to send the response to ENC28J60 ethernet module.
            timer2 = millis();
            while((client.available() == 0)&&(millis() < timer2 + TIMEOUT)); //Wait here until server respond or timer2 expires. 
            
            if (millis() > timer2 + TIMEOUT) {    // If we are here because of a time out
              serial.println("Sparkfun server timeout");
            }
            
            // if there are incoming bytes available from the server, read them and print them:
            while(client.available() > 0)
            {
                char inData = client.read();
                Serial.print(inData);
            }      
            //Serial.println("\n");   
            client.stop(); //Disconnect the client from server.  
         }
     } 
}
#endif

#ifdef THINGSPEAK_LOG
void sendToThingspeakServer()
{
  Serial.println("Sending data to Thingspeak...");
  int data_filed1 = chan1_watts;
  int data_filed2 = chan2_watts;
  ThingSpeak.writeField(myChannelNumber, 1, data_filed1, myWriteAPIKey);
  ThingSpeak.writeField(myChannelNumber, 2, data_filed2, myWriteAPIKey);
  //delay(20000); // ThingSpeak will only accept updates every 15 seconds.
}
#endif


void power_calculations() {
  Serial.println("Calculating power consuption: ");
  // If 60 seconds have passed restart counter
  if (millis() - chan1_last_pulse > 60000) {
    chan1_watts = 0;
  }
  if (millis() - chan2_last_pulse > 60000) {
    chan2_watts = 0;
  }

  if (millis() - report_time > 5000) {
    chan1_delta = chan1_last_pulse - chan1_first_pulse;
    if (chan1_delta && chan1_count) {
      chan1_watts = (chan1_count - 1) * w_per_pulse * ms_per_hour / chan1_delta;
      chan1_count = 0;
    }
    chan2_delta = chan2_last_pulse - chan2_first_pulse;
    if (chan2_delta && chan2_count) {
      chan2_watts = (chan2_count - 1) * w_per_pulse * ms_per_hour / chan2_delta;
      chan2_count = 0;
    }
    if (!(report_time == 0 && chan1_watts == 0 && chan2_watts == 0)) {
      Serial.print("Chan1 ");
      Serial.println(chan1_watts);
      Serial.print("Chan2 \n");
      Serial.println(chan2_watts);
      report_time = millis();
    }
  }
}