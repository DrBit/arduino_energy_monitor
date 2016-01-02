/*
   This sketch is based on examples found at data.sparkfun.com , thingspeak.com and on a nice post at nicegear.co.nz By Hadley Rich
   https://nicegear.co.nz/blog/electricity-meter-usage-over-mqtt/
   
   Created:     Doctor Bit
   Date:        28 November 2015
   Last update: 14 December 2015
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
    If we are sensing more than 3 sensors (and up to 6) we can use Arduino Mega. 
    External Interrupts on Arduino Mega: 
    2 (interrupt 0)
    3 (interrupt 1)
    18 (interrupt 5)
    19 (interrupt 4)
    20 (interrupt 3) 
    21 (interrupt 2)
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

// VARS GENERAL ///////////////////
const unsigned long ms_per_hour = 3600000UL;

// VARS ELECTRICITY ///////////////
const float elec_w_per_pulse_ch1 = 1;   // Wats per pulse on ch1
volatile unsigned int elec_chan1_count = 0;
volatile unsigned long elec_chan1_first_pulse = 0;
volatile unsigned long elec_chan1_last_pulse = 0;
static unsigned int elec_chan1_watts;
unsigned long elec_chan1_delta; // Time since last pulse

// Electricity interrupts ----------------------------------------------------------------------
void elec_chan1_isr() {    // chan1_pulse
  elec_chan1_count++;
  elec_chan1_last_pulse = millis();
  if (elec_chan1_count == 1) { // was reset
    elec_chan1_first_pulse = elec_chan1_last_pulse;
  }
}

// VARS GAS ///////////////
const float gas_mm3_per_pulse_ch1 = 1;   // Cubic Meters per pulse on ch1
volatile unsigned int gas_chan1_count = 0;
static unsigned int gas_chan1_mm3;

// Gas interrupts ----------------------------------------------------------------------
void gas_chan1_isr() {    // chan1_pulse
  gas_chan1_count++;
}

// VARS WATER ///////////////
const float wat_mm3_per_pulse_ch1 = 1;   // Cubic Meters per pulse on ch1
volatile unsigned int wat_chan1_count = 0;
static unsigned int wat_chan1_mm3;

// WATER interrupts ----------------------------------------------------------------------
void wat_chan1_isr() {    // chan1_pulse
  wat_chan1_count++;
}

// PIN INTERRUPTS
const int elec_chan1_pin = 2;
const int gas_chan1_pin = 3;
const int wat_chan1_pin = 21; // MEGA ONLY

//----------------------------------------------------------------------
void setup()
{
  // Setting up interrupts  //////////////////
  pinMode(elec_chan1_pin, INPUT);
  pinMode(gas_chan1_pin, INPUT);
  pinMode(wat_chan1_pin, INPUT);
  attachInterrupt(0, elec_chan1_isr, RISING); // Interrupt 0 
  attachInterrupt(1, gas_chan1_isr, RISING);  // Interrupt 1
  attachInterrupt(2, gas_chan1_isr, RISING);  // Interrupt 2 (MEGA ONLY)
  ///////////////////////////////////////////////

  Serial.begin(9600);   //Initiallize the serial port.

  #ifdef SPARKFUN_LOG
    Serial.println("SparkFun enabled");
  #endif
  #ifdef THINGSPEAK_LOG
    ThingSpeak.begin(client);
    Serial.println("ThingSpeak enabled\n");
  #endif

  Serial.println("Network configuration:\n");
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
      main_timer = millis();  // Update main_timer with the current time in miliseconds.
      power_calculations();   // Calculate power consumption
      gas_calculations();     // Calculate gas consumption
      water_calculations();   // Calculate water consumption
      
      #ifdef SPARKFUN_LOG 
      sendToSparkfunDataServer(); // Send data to sparkfun data server.
      #endif
      
      #ifdef THINGSPEAK_LOG 
      sendToThingspeakServer();   // Send data to thingspeak data server.
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
            client.print(elec_chan1_watts);    
            client.print("&gas=");
            client.println(gas_chan1_mm3);
            client.print("&water=");
            client.println(wat_chan1_mm3);
         
            delay(1000); //Give some time to Sparkfun server to send the response to ENC28J60 ethernet module.
            sparkfun_timer = millis();
            while((client.available() == 0)&&(millis() < sparkfun_timer + TIMEOUT)); //Wait here until server respond or sparkfun_timer expires. 
            
            if (millis() > sparkfun_timer + TIMEOUT) {    // If we are here because of a time out
              Serial.println("Sparkfun server timeout");
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
  Serial.println("Sending data to Thingspeak...\n");

  // Write to ThingSpeak. There are up to 8 fields in a channel, and we're writing to all of them.  
  // First, you set each of the fields you want to send
  ThingSpeak.setField(1,(int)elec_chan1_watts);
  ThingSpeak.setField(2,(int)gas_chan1_mm3);
  ThingSpeak.setField(3,(int)wat_chan1_mm3);

  // Then you write the fields that you've set all at once.
  ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  // ThingSpeak will only accept updates every 15 seconds.
}
#endif

//----------------------------------------------------------------------
void power_calculations() {
  Serial.println("Calculating power consuption: ");

  elec_chan1_delta = elec_chan1_last_pulse - elec_chan1_first_pulse;  // calculate time that has passed
  elec_chan1_watts = (elec_chan1_count - 1) * elec_w_per_pulse_ch1 * ms_per_hour / elec_chan1_delta; //calculate wh
  elec_chan1_count = 0; // reset pulse counter
  Serial.print("Chan1 ");  // print data
  Serial.print(elec_chan1_watts);
  Serial.println(" Wh");
}

//----------------------------------------------------------------------
void gas_calculations() {
  Serial.println("Calculating gas consuption: ");

  gas_chan1_mm3 = gas_chan1_count * gas_mm3_per_pulse_ch1;
  gas_chan1_count = 0;

  Serial.print("Chan1 ");
  Serial.print(gas_chan1_mm3);
  Serial.println(" mm3\n");
}

//----------------------------------------------------------------------
void water_calculations() {
  Serial.println("Calculating water consuption: ");

  wat_chan1_mm3 = wat_chan1_count * wat_mm3_per_pulse_ch1;
  wat_chan1_count = 0;

  Serial.print("Chan1 ");
  Serial.print(wat_chan1_mm3);
  Serial.println(" mm3\n");
}