#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include "memorysaver.h"
#include <SSLClient.h>
#include <Ethernet.h>
#include <AsyncTelegram2.h>
#include <tg_certificate.h>

#define SPI_SETTINGS SPISettings(4000000, MSBFIRST, SPI_MODE0)

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip(192, 168, 11, 30);
IPAddress myDns(192, 168, 11, 1);

int triggerPort = 22;             // GPIO33
int echoPort = 28;                // GPIO32
float distanceTrigger = 1.0;
int checkDistanceTime = 5000;      // milliseconds

const char* telegram_token =  "YOUR_TOKEN_HERE";
int32_t chat_id = "YOUR_ID_HERE"; // You can discover your own chat id, with "Json Dump Bot"

EthernetClient base_client;
SSLClient client(base_client, TAs, (size_t)TAs_NUM, A0, 1, SSLClient::SSL_ERROR );

AsyncTelegram2 myBot(client);
ReplyKeyboard myReplyKbd;   // reply keyboard object helper
InlineKeyboard myInlineKbd;
bool isKeyboardActive;      // store if the reply keyboard is shown
bool alarmStatusEE;
#define CAPTURE_CALLBACK  "Capture"   // callback data sent when "LIGHT ON" button is pressed
#define AUTO_ON_CALLBACK  "AutoON"   // callback data sent when "LIGHT ON" button is pressed
#define AUTO_OFF_CALLBACK "AutoOFF"  // callback data sent when "LIGHT OFF" button is pressed

// set pin 7 as the slave select for the digital pot:
const int CS = 13;
bool is_header = false;
int mode = 0;

ArduCAM myCAM( OV2640, CS );

void i2c_init();
void spi_init();
void camera_init();

void test_capture(){
  myCAM.clear_fifo_flag();
  myCAM.start_capture();
}

void camCapture(ArduCAM myCAM) {
  const size_t bufferSize = 8192 * 2;
  uint8_t buffer[bufferSize] = {0xFF};
  uint8_t temp = 0, temp_last = 0;
  int i = 0;

  uint32_t len  = myCAM.read_fifo_length();
  Serial.println(len);
  if (len >= MAX_FIFO_SIZE) //8M
  {
    Serial.println(F("Over size."));
  }
  if (len == 0 ) //0 kb
  {
    Serial.println(F("Size is 0."));
  }
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();

  i = 0;
  while ( len-- )
  {
    temp_last = temp;
    temp =  SPI1.transfer(0x00);
    //Read JPEG data from FIFO
    if ( (temp == 0xD9) && (temp_last == 0xFF) ) //If find the end ,break while,
    {
      buffer[i++] = temp;  //save the last  0XD9
      //Write the remain bytes in the buffer
     // myBot.sendPhoto(chat_id, &buffer[0], i);
      is_header = false;
      i = 0;
      myCAM.CS_HIGH();
      break;
    }
    if (is_header == true)
    {
      //Write image data to buffer if not full
      if (i < bufferSize)
        buffer[i++] = temp;
      else
      {
        //Write bufferSize bytes image data to file
        //client.write(&buffer[0], bufferSize);
       // myBot.sendPhoto(chat_id,&buffer[0], bufferSize);
        i = 0;
        buffer[i++] = temp;
      }
    }
    else if ((temp == 0xD8) & (temp_last == 0xFF))
    {
      is_header = true;
      buffer[i++] = temp_last;
      buffer[i++] = temp;
    }
  }
  myBot.sendPhoto(chat_id,&buffer[0], bufferSize);
}

void serverCapture(){
  test_capture();
  Serial.println("CAM Capturing");

  int total_time = 0;

  total_time = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  total_time = millis() - total_time;
  Serial.print("capture total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  
  total_time = 0;
  
  Serial.println("CAM Capture Done!");
  total_time = millis();
  camCapture(myCAM);
  total_time = millis() - total_time;
  Serial.print("send total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM send Done!");
}

void setup() {
  // put your setup code here, to run once:  
  Serial.begin(115200);
  while (!Serial) ;
    
  i2c_init();
  spi_init();
  camera_init();    
    
  delay(1000);
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);

  pinMode( triggerPort, OUTPUT );     // sets the pin as OUTPUT for the ultrasonic sensor
  pinMode( echoPort, INPUT );         // sets the pin as INPUT for the ultrasonic sensor

  Ethernet.init(17);
  //Ethernet.begin(mac, ip);
  Ethernet.begin(mac, ip, myDns);

  // Set the Telegram bot properies
  myBot.setUpdateTime(3000);
  myBot.setTelegramToken(telegram_token);

  // Check if all things are ok
  Serial.print("\nTest Telegram connection... ");
  myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

   // Add reply keyboard
  isKeyboardActive = false;
  // add a button that send a message with "Simple button" text
  myReplyKbd.addButton("Capture");
  myReplyKbd.addRow();
  myReplyKbd.addButton("AutoMode-ON");
  myReplyKbd.addButton("AutoMode-OFF");
  myReplyKbd.addRow();
  myReplyKbd.addButton("/hide_keyboard");

  // Add sample inline keyboard
  myInlineKbd.addButton("Capture", CAPTURE_CALLBACK, KeyboardButtonQuery);
  myInlineKbd.addRow();
  myInlineKbd.addButton("AutoMode-ON", AUTO_ON_CALLBACK, KeyboardButtonQuery);
  myInlineKbd.addButton("AutoMode-OFF", AUTO_OFF_CALLBACK, KeyboardButtonQuery);
  myBot.addInlineKeyboard(&myInlineKbd);

  char welcome_msg[128];
  snprintf(welcome_msg, 128, "BOT @%s online\n/help all commands avalaible.", myBot.getBotName());
  
  myBot.sendTo(chat_id, welcome_msg);
  alarmStatusEE = false;
}

// the loop function runs over and over again forever
void loop() {
  //server.handleClient();
  TBMessage msg;
  if (myBot.getNewMessage(msg)) {
    
    if (msg.chatId != chat_id){
      myBot.sendMessage(msg, "Unauthorized user");
    }else{
      String tgReply;
      MessageType msgType = msg.messageType;
      switch (msgType) {
        case MessageText :
          // received a text message
          tgReply = msg.text;
          Serial.print("\nText message received: ");
          Serial.println(tgReply);

          // check if is show keyboard command
          if (tgReply.equalsIgnoreCase("/reply_keyboard")) {
            // the user is asking to show the reply keyboard --> show it
            myBot.sendMessage(msg, "This is reply keyboard:", myReplyKbd);
            isKeyboardActive = true;
          }
          else if (tgReply.equalsIgnoreCase("/inline_keyboard")) {
            myBot.sendMessage(msg, "This is inline keyboard:", myInlineKbd);
          }
          // check if the reply keyboard is active
          else if (isKeyboardActive) {
            // is active -> manage the text messages sent by pressing the reply keyboard buttons
            if (tgReply.equalsIgnoreCase("/hide_keyboard")) {
              // sent the "hide keyboard" message --> hide the reply keyboard
              myBot.removeReplyKeyboard(msg, "Reply keyboard removed");
              isKeyboardActive = false;
            } else if (tgReply.equalsIgnoreCase("Capture")) {
              serverCapture();
              myBot.sendMessage(msg, "Image captured and sent");
            } else if (tgReply.equalsIgnoreCase("AutoMode-ON")) {
              alarmStatusEE = true;
              myBot.sendMessage(msg, "AutoMode is ON");
            } else if (tgReply.equalsIgnoreCase("AutoMode-OFF")) {  
              alarmStatusEE = false;
              myBot.sendMessage(msg, "AutoMode is OFF");
            } else {
              // print every others messages received
              myBot.sendMessage(msg, msg.text);
            }
          }
          // the user write anything else and the reply keyboard is not active --> show a hint message
          else {
            myBot.sendMessage(msg, "Try /reply_keyboard or /inline_keyboard");
          }
          break;

        case MessageQuery:
          // received a callback query message
          tgReply = msg.callbackQueryData;
          Serial.print("\nCallback query message received: ");
          Serial.println(tgReply);

          if (tgReply.equalsIgnoreCase(AUTO_ON_CALLBACK)) {
            alarmStatusEE = true;
            // terminate the callback with an alert message
            myBot.endQuery(msg, "Auto is on", true);
          }
          else if (tgReply.equalsIgnoreCase(AUTO_OFF_CALLBACK)) {
            alarmStatusEE = false;
            // terminate the callback with a popup message
            myBot.endQuery(msg, "Auto is off");
          }
          else if (tgReply.equalsIgnoreCase(CAPTURE_CALLBACK)){
            serverCapture();
            myBot.endQuery(msg, "Image sent");
          }

          break;
        default:
          break;
      }
    }
  }

  static unsigned long lastRefreshTime = 0;
	
  if(alarmStatusEE){
      if(millis() - lastRefreshTime >= checkDistanceTime)
      {
        lastRefreshTime += checkDistanceTime;
        Serial.println(lastRefreshTime);

        digitalWrite(triggerPort, LOW);			// set to LOW trigger's output
        delayMicroseconds(2);
        digitalWrite(triggerPort, HIGH);		// send a 10us pulse to the trigger
        delayMicroseconds(10);
        digitalWrite(triggerPort, LOW);
        
        long duration = pulseIn(echoPort, HIGH);
        
        long r = 3.4 * duration / 2;			// here we calculate the distance using duration

        float distance = duration * 340/2 * 0.0001;

        float distanceMeters = distance / 100;

        if( (duration > 38000) or (duration == 0) ) Serial.println("out of reach");		// if duration in greather than 38ms, the obstacle is out of reach. If the duration equals 0 the sensor is in error
        else { 
          Serial.print("duration: ");
          Serial.print(duration);
          Serial.print(" , ");
          Serial.print("distance: ");Serial.print(distance); Serial.print("cm, ");
          Serial.print(distance/100); Serial.println("m");

          if(distanceMeters <= distanceTrigger)
          {
            myBot.sendMessage(msg, "Intruder detected!");
            serverCapture();
          }
        }
      } 
  }
  //delay(1000);
}

void i2c_init()
{
  Wire.setSDA(8);
  Wire.setSCL(9);
  Wire.begin();
}
void spi_init()
{
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  SPI1.setCS(13);
  SPI1.setRX(12);
  SPI1.setSCK(10);
  SPI1.setTX(11);
  SPI1.begin();
}
void camera_init()
{
  uint8_t vid, pid;
  uint8_t temp;

  //Reset the CPLD
  myCAM.write_reg(0x07, 0x80);
  delay(100);
  myCAM.write_reg(0x07, 0x00);
  delay(100);
  while(1){
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55){
      Serial.println(F("SPI interface Error! END"));
      delay(1000);continue;
    }else{
      Serial.println(F("SPI interface OK. END"));break;
    }
  }
  
  while(1){
    //Check if the camera module type is OV2640
    myCAM.wrSensorReg8_8(0xff, 0x01);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))){
      Serial.println(F("ACK CMD Can't find OV2640 module! END"));
      delay(1000);continue;
    }
    else{
      Serial.println(F("ACK CMD OV2640 detected. END"));break;
    } 
  }
  
  //Change to JPEG capture mode and initialize the OV2640 module
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  myCAM.OV2640_set_JPEG_size(OV2640_320x240);
  delay(1000);
  myCAM.clear_fifo_flag();
}