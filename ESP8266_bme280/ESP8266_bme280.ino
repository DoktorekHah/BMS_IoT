#include <ESP8266WiFi.h>        // ESP8266 Core WiFi Library        
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <SNMPTrap.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "Configuration.h"

const char *ssid = STASSID;
const char *password = STAPSK;

WiFiUDP udp;

// Starts an SMMPAgent instance with the read-only community string 'public', and read-write community string 'private
SNMPAgent snmp = SNMPAgent(SNMP_COMMUNITY_STRING, SNMP_COMMUNITY_STRING_RW);  

// Numbers used to response to Get requests
int changingNumber    =        VALUE_ZERO;
int settableNumber    =        VALUE_ZERO;
int humidity          =        VALUE_ZERO;
int temperature       =        VALUE_ZERO;
uint32_t tensOfMillisCounter = VALUE_ZERO;

// arbitrary data will be stored here to act as an OPAQUE data-type
uint8_t* stuff = 0;

// If we want to change the functionaality of an OID callback later, store them here.
ValueCallback*      changingNumberOID;
ValueCallback*      settableNumberOID;
ValueCallback*      temperatureOID;
ValueCallback*      humidityOID;
ValueCallback*      EndOID;
TimestampCallback*  timestampCallbackOID;

std::string staticStringTemp = "Temperature";
std::string staticStringHum  = "Humidity";
std::string staticStringEnd  = " ";

// Setup an SNMPTrap for later use
SNMPTrap* settableNumberTrap = new SNMPTrap(SNMP_COMMUNITY_STRING, SNMP_VERSION_2C);
char* changingString;

Adafruit_BME280 bme; // I2C

void setup(){
    Serial.begin(115200);
    WiFi.begin(ssid, password);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print("Connecting to server ");
        Serial.print(ssid);
        delay(DELAY);
        Serial.print(".");
    } 
    
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Configuration for sensor bme280
    bool status;
    status = bme.begin(BME280_ADDRESS);  
    if (!status) {
      Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
    }
    
    // give snmp a pointer to the UDP object
    snmp.setUDP(&udp);
    snmp.begin();

    // setup our OPAQUE data-type
    stuff = (uint8_t*)malloc(4);
    stuff[0] = 1;
    stuff[1] = 2;
    stuff[2] = 24;
    stuff[3] = 67;
    
    // add 'callback' for an OID - pointer to an integer
    changingNumberOID = snmp.addIntegerHandler(".1.3.6.1.4.1.5.0", &changingNumber);
    
    // Using your favourite snmp tool:
    // snmpget -v 1 -c public <IP> 1.3.6.1.4.1.5.0
    
    // you can accept SET commands with a pointer to an integer 
    settableNumberOID = snmp.addIntegerHandler(".1.3.6.1.4.1.5.1", &settableNumber, true);
    // snmpset -v 1 -c public <IP> 1.3.6.1.4.1.5.0 i 99

    temperatureOID = snmp.addIntegerHandler(".1.3.6.1.4.1.5.20", &temperature);
    snmp.addReadOnlyStaticStringHandler(".1.3.6.1.4.1.5.15", staticStringTemp);
    humidityOID = snmp.addIntegerHandler(".1.3.6.1.4.1.5.21", &humidity);
    snmp.addReadOnlyStaticStringHandler(".1.3.6.1.4.1.5.16", staticStringHum);

    snmp.addReadOnlyStaticStringHandler(".1.3.6.1.4.1.5.22", staticStringEnd);

    // More examples:
    snmp.addIntegerHandler(".1.3.6.1.4.1.4.0", &changingNumber);
    snmp.addOpaqueHandler(".1.3.6.1.4.1.5.9", stuff, 4, true);
    
    // Setup read/write string
    changingString = (char*)malloc(25 * sizeof(char));
    snprintf(changingString, 25, "This is changeable");
    snmp.addReadWriteStringHandler(".1.3.6.1.4.1.5.12", &changingString, 25, true);


    // Setup SNMP TRAP
    // The SNMP Trap spec requires an uptime counter to be sent along with the trap.
    timestampCallbackOID = (TimestampCallback*)snmp.addTimestampHandler(".1.3.6.1.2.1.1.3.0", &tensOfMillisCounter);

    settableNumberTrap->setUDP(&udp); // give a pointer to our UDP object
    settableNumberTrap->setTrapOID(new OIDType(".1.3.6.1.2.1.33.2")); // OID of the trap
    settableNumberTrap->setSpecificTrap(1); 

    // Set the uptime counter to use in the trap (required)
    settableNumberTrap->setUptimeCallback(timestampCallbackOID);
    

    // Set some previously set OID Callbacks to send these values with the trap (optional)
    settableNumberTrap->addOIDPointer(changingNumberOID);
    settableNumberTrap->addOIDPointer(settableNumberOID);
    settableNumberTrap->addOIDPointer(temperatureOID);
    settableNumberTrap->addOIDPointer(humidityOID);
    settableNumberTrap->addOIDPointer(EndOID);
    
    settableNumberTrap->setIP(WiFi.localIP()); // Set our Source IP

    // Ensure to sortHandlers after adding/removing and OID callbacks - this makes snmpwalk work
    snmp.sortHandlers();
} 

void loop(){
    snmp.loop(); // must be called as often as possible
    if(settableNumberOID->setOccurred){
        
        Serial.printf("Number has been set to value: %i\n", settableNumber);
        if(settableNumber%2 == 0){
            // Sending an SNMPv2 INFORM (trap will be kept and re-sent until it is acknowledged by the IP address it was sent to)
            settableNumberTrap->setVersion(SNMP_VERSION_2C);
            settableNumberTrap->setInform(true); // set this to false and send using `settableNumberTrap->sendTo` to send it without the INFORM request
        } else {
            // Sending regular SNMPv1 trap
            settableNumberTrap->setVersion(SNMP_VERSION_1);
            settableNumberTrap->setInform(false);
        }
        // Serial.println("Lets remove the changingNumber reference");
        // snmp.sortHandlers();
        // if(snmp.removeHandler(settableNumberOID)){
        //     Serial.println("Remove succesful");
        // }
        settableNumberOID->resetSetOccurred();

        // Send the trap to the specified IP address
        // If INFORM is set, snmp.loop(); needs to be called in order for the acknowledge mechanism to work.
        IPAddress destinationIP = IPAddress(192, 168, 1, 243);
        if(snmp.sendTrapTo(settableNumberTrap, destinationIP, true, 2, 5000) != INVALID_SNMP_REQUEST_ID){ 
            Serial.println("Sent SNMP Trap");
        } else {
            Serial.println("Couldn't send SNMP Trap");
        }
    }
    changingNumber++;
    tensOfMillisCounter = millis()/10;

    // Read data from sensor bme280
    humidity = bme.readHumidity();
    temperature = bme.readTemperature();

#ifdef SHOW_VALUE_FROM_BME280
  valueBME280();
#endif
}

void valueBME280() {
  Serial.print("Temperature = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");
  
  Serial.print("Pressure = ");
  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  //Serial.print("Approx. Altitude = ");
  //Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
  //Serial.println(" m");

  Serial.print("Humidity = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

  Serial.println();
}
