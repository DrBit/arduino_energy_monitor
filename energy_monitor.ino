/*
   This sketch is based on examples found at data.sparkfun.com , thingspeak.com and on a nice post at nicegear.co.nz By Hadley Rich
   https://nicegear.co.nz/blog/electricity-meter-usage-over-mqtt/
   
   Created:     Doctor Bit
   Date:        28 November 2015
   Last update: 06 December 2015
   Home:        http://www.drbit.nl
   
   Tested with Arduino IDE 1.6.3
  
 -= SparkFun data client =-
 
    * Create your own data stream (https://data.sparkfun.com)
    * You can mark your data as public or private during the data stream creation.
    * Replace PUBLIC_KEY and PRIVATE_KEY with these sparkfun give you
    * Replace the data name "power" on sendToSparkfunDataServer() with those
      gave you sparkfun data stream creation.
    * You can view online or download your data as CSV or JSON file via 
      https://data.sparkfun.com/streams/ + YOUR_PUBLIC_KEY
      
      https://data.sparkfun.com/streams/WGGWNZLKGOFAzyLwLOzQ
      
      GET /input/WGGWNZLKGOFAzyLwLOzQ?private_key=XRRmzj9YR2iXzjnKn6zR&power=255

      1000 imp = 1Kwh

    Sensors should be attached at Arduino Uno pins 2 and 3 (Check other Interrupt pins for other arduino boards)
 */

#include <SPI.h>
#include <Ethernet.h>
#include <stdio.h>
// delete this next line or you will get an error compiling.
#include "keys.h"   //My file containing all my private keys (not included obviously)

// Enable or disable logging to different servers by commenting (or not) the next lines
#define SPARKFUN_LOG
#define THINGSPEAK_LOG 

#ifdef SPARKFUN_LOG
  ///////  *** ADD your KEYS here and uncomment ***  ///////////////////////
  //#define PUBLIC_KEY  "yourpublickey"   //Your SparkFun public_key
  //#define PRIVATE_KEY "yourprivatekey"  //Your SparkFun private_key
  //#define DELETE_KEY "not used"         //Your SparkFun delte_key
  char sparkfunDataServer[] = "data.sparkfun.com";
#endif

#ifdef THINGSPEAK_LOG
  //////  *** ADD your KEYS here and uncomment ***  ///////////////////////
  //unsigned long myChannelNumber = your channel number;
  //const char * myWriteAPIKey = "your API key";
  #include "ThingSpeak.h"
  unsigned long sparkfun_timer = 0;
#endif

#define UPDATE_TIME 60000         //Update posting data to server every 60000 ms (1 minute).        
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

unsigned long main_timer = 0;

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

//----------------------------------------------------------------------
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

//----------------------------------------------------------------------
void setup()
{
  pinMode(chan1_pin, INPUT);
  pinMode(chan2_pin, INPUT);
  attachInterrupt(0, chan1_isr, RISING);
  attachInterrupt(1, chan2_isr, RISING);

  Serial.begin(9600);   //Initiallize the serial port.

  #ifdef SPARKFUN_LOG
    Serial.println("-= Logging to SparkFun enabled =-\n");
  #endif
  #ifdef THINGSPEAK_LOG
    ThingSpeak.begin(client);
    Serial.println("-= Logging to ThingSpeak enabled =-\n");
  #endif

  if (Ethernet.begin(mac) == 0)  // start the Ethernet connection
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
void loop()
{
  if(millis() > main_timer + UPDATE_TIME)
  {
      main_timer = millis();      //Update main_timer with the current time in miliseconds.
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
    if (client.connect(sparkfunDataServer, 80))   //Establish a TCP connection to sparkfun server.
    {
        if(client.connected())    //if the client is connected to the server...
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
            sparkfun_timer = millis();
            while((client.available() == 0)&&(millis() < sparkfun_timer + TIMEOUT)); //Wait here until server respond or sparkfun_timer expires. 
            
            if (millis() > sparkfun_timer + TIMEOUT) {    // If we are here because of a time out
              serial.println("Sparkfun server timeout");
            }
            
            // if there are incoming bytes available from the server, read them and print them:
            while(client.available() > 0)
            {
                char inData = client.read();
                Serial.print(inData);
            }         
            client.stop(); //Disconnect the client from server.  
         }
     } 
}
#endif

//----------------------------------------------------------------------
#ifdef THINGSPEAK_LOG
void sendToThingspeakServer()
{
  Serial.println("Sending data to Thingspeak...");
  int data_filed1 = chan1_watts;
  int data_filed2 = chan2_watts;
  ThingSpeak.writeField(myChannelNumber, 1, data_filed1, myWriteAPIKey);
  ThingSpeak.writeField(myChannelNumber, 2, data_filed2, myWriteAPIKey);
  // ThingSpeak will only accept updates every 15 seconds.
}
#endif

//----------------------------------------------------------------------
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