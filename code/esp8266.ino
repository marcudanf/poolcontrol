//needed for wifi comunication
#include <ESP8266WiFi.h>
//needed for ota programming
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//needed for keeping the log
#include <EEPROM.h>
//used for the temperature sensor
#include <OneWire.h>
#include <DallasTemperature.h>
//used for the rtc module
#include <Wire.h>
#include "RTClib.h"

//defines
#define rtc_pin 12  
#define favicon "<script>var favIcon = 'iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAABGdBTUEAALGPC/xhBQAAACBjSFJNAAB6JgAAgIQAAPoAAACA6AAAdTAAAOpgAAA6mAAAF3CculE8AAABd1BMVEUAAAALZqwMZqwMZqwMZqwtfLhinMq40ucKZqwMZqwMZqy20eb///////8MZqz///8LZqyWvdsKZawMZqwue7gAXKcGYqoHYqoLZqwLZqwMZqwLZawJZasKZawFY6sMZqwJZawLZqwDYqsJZawKZqwGZawCYqsJZKwKZawGZKwEYqoIZKsKZasKZawGY6sMZqw3gbtfmsgTaq4/hb3///8nd7U9hb1inMmixN8LZasGYqofcrMwe7g3f7pNjsGBr9M8hb0AW6YDYKkCYKkAXacpebWQuNhXlMQmdrWgw95dmcdupM5sosxlncppoMtooMtgm8mYvttEiL8IY6oIZKsdcbKMtthGi8A5grtIjMA9hL1Fib9Ah75Bh747hLyItNY3gboEYqoJZKsjdLRsos0AXKcFYqoFYakEYalWlcU9hLx3qdAEYaoLZawAX6hjnck+hb0NZ6xzps93qNB1p89/rtMbb7EWbK8XbK8LZqwKZawNZqwAAACN9f6uAAAAL3RSTlMAN5TP6daxOAqV/P2WC7i6lZo0/f42lpnS1OvTl5o1/jeaC7q8DAqXmQw5l9HROjkNu9wAAAABYktHRACIBR1IAAAACXBIWXMAAC4jAAAuIwF4pT92AAAA3ElEQVQY02NgAAJGJmYWFlY2dgYI4ODk0gcCA0NuHl4wn08fDIyMDU34QSIC+lBgamZuIcjAICRsaQUVsbaxFRFlELOzd3B0cnZydnZ2cXVzF2eQ0Pfw9PI29/H18w8IDAqWZJAKCQ0Lj4iMio6JjYtPSJRikEpKTklNc0rPyMhwzMxKk2KQTkrOzskFg7z8gjRJBpncwqLs4mwQKCktS5JlkJO3TCqHgAr9JHkFBgYZ/coqiJaqSn1FoEuVlKthbtVXUQV5Rk1dGCxULa+hCfWvlrYOC4uuth6IDQDSCzh5MTipcgAAAABJRU5ErkJggg==';var docHead = document.getElementsByTagName('head')[0];var newLink = document.createElement('link');newLink.rel = 'shortcut icon';newLink.href = 'data:image/png;base64,'+favIcon;docHead.appendChild(newLink);</script>"
#define header "HTTP/1.1 200 OK\nContent-Type: text/html\n\n<!DOCTYPE HTML>"
#define relay 14
#define minut *60000
#define HOME 0
#define OTA 1
#define LOG 2
#define TIME 3
#define SETTINGS 4

//rtc objects
RTC_DS3231 rtc;
DateTime now;

//temperature sensor objects
OneWire oneWire(2);
DallasTemperature tempSensor(&oneWire);

//data for the settings page
byte time_on;
byte time_off;
byte from_h;
byte from_m;
byte to_h;
byte to_m;

//wifi ssid and pass
char ssid[30];// = "1";
char password[30];// = "MarcuCristinaDaniel";
//ap ssid and pass
const char* ap_ssid = "PoolControl";
const char* ap_password = "";

//the server is on port 80(default)
WiFiServer server(80);

WiFiClient client;
String request;

//other variables
byte temp;
byte webtemp=35;
bool state=0;
bool manual=0;
bool priority=0;
unsigned long long last_change=0,last_checked=0;
int mod=0;
byte v[512]={0};
bool erasedToday=1;
int lastMin=0;

//function used to toggle the relay
void toggle(){
  state=!state;
  digitalWrite(relay,state);
  manual=1;
  mod=0;
}

//function used to do a 'factory reset' of the esp (keeping the log though)
void full_reset(){
  EEPROM.begin(512);
  for(int i=200;i<512;i++)
  EEPROM.write(i,0);
  EEPROM.end();
  Serial.println("restarting");
  ESP.restart();
  Serial.println("failed");
}

//function to read the log from eeprom
void eepromRead(){
  EEPROM.begin(512);
  int addr=0;
  byte Val;
  for(int i=0;i<96+now.hour()*2+now.minute()/30;i++){
    Val=EEPROM.read(i);
    v[i]=Val;
  }
  EEPROM.end();
}

//function to delete th last day form eeprom
void eepromShift(){
  for(int i=48;i<144;i++){
    EEPROM.begin(512);
    byte Val=EEPROM.read(i);
    EEPROM.write(i-48,Val);
    EEPROM.end();
  }
  EEPROM.begin(512);
  for(int i=96;i<144;i++)
    EEPROM.write(i,0);
  EEPROM.end();
}

//function to write a new value to eeprom
void eepromWrite(byte Val){
  EEPROM.begin(512);
  EEPROM.write(96+now.hour()*2+now.minute()/30,Val);
  EEPROM.end();
}

//used for the log
char Month[13][13]={"ERR","Jan.","Feb.","March","Apr.","May","June","July","Aug.","Sept.","Oct.","Nov.","Dec."};
int days_in_a_month[12]={31,28,31,30,31,30,31,31,30,31,30,31};

//checker is 1 when the system is scheduled to turn on
byte checker;

//function used to turn on/off the pump depending on the situation
void treat(unsigned long long);

//function used for printing the page to the client
void printPage(int);

//function used to treat the request
void treatRequest();

void setup(){
  //begin serial comunication
  pinMode(rtc_pin,OUTPUT);
  digitalWrite(rtc_pin,HIGH);
  Serial.begin(9600);
  EEPROM.begin(512);
  byte booted_before=EEPROM.read(200);
  EEPROM.end();
  Serial.println("started");
  if(booted_before!=1){
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid,ap_password);
    WiFi.disconnect();
    //start the server
    server.begin();
    Serial.println("waiting for a client");
    check1:
    client=server.available();
    if(!client){
      client.flush();
      goto check1;
    }
    Serial.println("got a client");
    while(!client.available())
      delay(1);
    Serial.println(client.readStringUntil('\r'));
    String webpage;
    Serial.println("enter ap name and pass");
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'> ";
    webpage+="<title>PoolControl</title>";
    webpage+="<style>";
    webpage+="html,body{margin:0;width:100%;}";
    webpage+="h1{font-size:40px;font-weight:bold;}";
    webpage+=".separate{width:100%;height:3px;background-color:black;}";
    webpage+="input{font-size:32px;width:60%;}";
    webpage+="h2{font-size:30px;}";
    webpage+=".submit{width:40%;}";
    webpage+="</style>";
    client.print(webpage);
    webpage="</head><body><center>";
    webpage+="<h1>Welcome to<br/>PoolControl</h1>";
    webpage+="<div class='separate'></div>";
    webpage+="<form method='get'>";
    webpage+="<h2>Enter network SSID</h2>";
    webpage+="<input type='text' name='ssid' placeholder='ssid...'/><br/><br/>";
    webpage+="<div class='separate'></div>";
    webpage+="<h2>Enter network password</h2>";
    webpage+="<input type='text' name='pass' placeholder='pass..'/><br/><br/>";
    webpage+="<div class='separate'></div><br/>";
    webpage+="<input class='submit' type='submit' value='Submit'/>";
    webpage+="</form>";
    webpage+="</center>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
    delay(2000);
    check2:
    client=server.available();
    if(!client){
      client.flush();
      goto check2;
    }
    Serial.println("got a client, waiting");
    long long started_at=millis();
    while(!client.available()){
      if(millis()-started_at>3000)
      goto check2;
      delay(1);
    }
    Serial.println("client connected");
    String request=client.readStringUntil('\r');
    Serial.println(request);
    if(request.indexOf("favicon.ico")!=-1){
      client.flush();
      client.stop();
      goto check2;
    }
    Serial.println("got ssid and pass");
    Serial.println("request= "+request);
    String temp_ssid=request.substring(request.indexOf("ssid=")+5,request.indexOf("&pass="));
    String temp_pass=request.substring(request.indexOf("pass=")+5,request.indexOf(" HTTP"));
    byte ssid_len=temp_ssid.length();
    byte pass_len=temp_pass.length();
    for(int i=0;i<ssid_len;i++)
    ssid[i]=temp_ssid[i];
    for(int i=0;i<pass_len;i++)
    password[i]=temp_pass[i];
    Serial.println(ssid);
    Serial.println(password);
    Serial.println("connecting...");
    WiFi.begin(ssid,password);
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<meta http-equiv='refresh' content='12;url=/'>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<title>PoolControl</title>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    webpage+="<h1>";
    webpage+="Waiting for<br/>WiFi to connect";
    webpage+="</h1>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    started_at=millis();
    while(WiFi.status()!=WL_CONNECTED){
      delay(500);
      if(millis()-started_at>10000)
      break;
    }
    Serial.println(int(millis()-started_at));
    check3:
    client=server.available();
    if(!client){
      client.flush();
      goto check3;
    }
    started_at=millis();
    while(!client.available()){
      delay(1);
      if(millis()-started_at>4000)
      goto check3;
    }
    if(client.readStringUntil('\r').indexOf("favicon.ico")!=-1){
      client.flush();
      client.stop();
      goto check3;
    }
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<title>PoolControl</title>";
    webpage+="<style>";
    webpage+="a{text-align:center;text-decoration:none;color:black;}";
    webpage+=".a{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;}";
    webpage+="</style>";
    client.print(webpage);
    webpage="";
    if(WiFi.status()==WL_CONNECTED){
    Serial.println("connected to "+(String)ssid+" pass= "+(String)password+"\nIP");
    Serial.println(WiFi.localIP());
    } 
    if(WiFi.status()==WL_CONNECTED){
      webpage+="</head><body><center><h2>Connected, IP:<br/>";
      client.print(webpage);
      client.print(WiFi.localIP());
      webpage="<br/>Use button below to connect<br/>(after connecting to wifi)</h2><br/><br/>";
      webpage+="<a class='a B' href='http://";
      client.print(webpage);
      client.print(WiFi.localIP());
      webpage="'>GO<a/><br/><h2>Or use direct connection</h2><a class='a B' href='/'></a></center></body></html>";
    }else{
      webpage+="</head><body><center><h1>Could not connect,<br/>Use direct connection</h1><a class='a B' href='/'>GO</a></center></body></html>";
    }
    client.print(webpage);
    client.flush();
    client.stop();
    booted_before=1;
    EEPROM.begin(512);
    EEPROM.write(200,booted_before);
    EEPROM.write(201,ssid_len);
    for(int i=0;i<ssid_len;i++)
    EEPROM.write(202+i,ssid[i]);
    EEPROM.write(202+ssid_len,pass_len);
    for(int i=0;i<pass_len;i++)
    EEPROM.write(203+ssid_len+i,password[i]);
    EEPROM.end();
    //initialise the rtc module
    rtc.begin();
    //write default values to eeprom
    EEPROM.begin(512);
    webtemp=35;
    EEPROM.write(500,webtemp);
    time_on=45;
    EEPROM.write(501,time_on);
    time_off=15 ;
    EEPROM.write(502,time_off);
    EEPROM.end();
    //relay pin
    pinMode(relay,OUTPUT);
    digitalWrite(relay,0);
    //begin the temperature sensor
    tempSensor.begin();
    //restart the esp when ota programming is done
    ArduinoOTA.onEnd([](){
      ESP.restart();
    });
    ArduinoOTA.begin();
  }else{
    Serial.println("normal boot");
    //initialise the rtc module
    rtc.begin();
    EEPROM.begin(512);
    byte ssid_len=EEPROM.read(201);
    for(int i=0;i<ssid_len;i++)
      ssid[i]=(char)EEPROM.read(202+i);
    byte pass_len=EEPROM.read(202+ssid_len);
    for(int i=0;i<pass_len;i++)
      password[i]=(char)EEPROM.read(203+ssid_len+i);
    EEPROM.end();
    //begin wifi comunication
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid,password);    
    WiFi.softAP(ap_ssid,ap_password);
    //read webpage variables from eeprom
    EEPROM.begin(512);
    webtemp=EEPROM.read(500);
    time_on=EEPROM.read(501);
    time_off=EEPROM.read(502);
    from_h=EEPROM.read(503);
    from_m=EEPROM.read(504);
    to_h=EEPROM.read(505);
    to_m=EEPROM.read(506);
    EEPROM.end();
    //write default values if unexisting
    EEPROM.begin(512);
    if(!webtemp){
      webtemp=35;
      EEPROM.write(500,webtemp);
    }
    if(!time_on){
      time_on=45;
      EEPROM.write(501,time_on);
    }
    if(!time_off){
      time_off=15 ;
      EEPROM.write(502,time_off);
    }
    EEPROM.end();
    //start the server
    server.begin();
    //relay pin
    pinMode(relay,OUTPUT);
    digitalWrite(relay,0);
    //initialize the temperature sensor
    tempSensor.begin();
    //restart the esp when ota programming is done
    ArduinoOTA.onEnd([](){
      ESP.restart();
    });
    ArduinoOTA.begin();
    Serial.println("connecting to "+String(ssid)+" pass "+String(password));
    unsigned long long started_at=millis();
    while(WiFi.status()!=WL_CONNECTED){
      delay(500);
      if(millis()-started_at>10000)
      break;
    }
    if(WiFi.status()==WL_CONNECTED){
      Serial.println(WiFi.localIP());
    }
  }
  digitalWrite(rtc_pin,LOW);
  delay(500);
  digitalWrite(rtc_pin,HIGH);
  delay(500);
  Serial.println("done setup");
}

void loop(){
  //get currnent time
  now = rtc.now();
  //invalid hour, restart
  /*if(now.hour()==165)
  ESP.restart();*/
  //add another value to eepprom form 30 minutes to 30 minutes
  if((now.minute()%30)==0&&lastMin!=now.minute()){
    lastMin=now.minute();
    tempSensor.requestTemperatures();
    temp=tempSensor.getTempCByIndex(0);
    if((temp>webtemp)&&!manual||state)
      eepromWrite(temp+100);//add 100 if the relay is (or should be) on (for the log)
    else
      eepromWrite(temp);
  }
  //erase last day form eeprom
  if(now.minute()==1&&(now.hour()==0||now.hour()==24)&&!erasedToday){
    eepromShift();
    erasedToday=1;
  }
  if(now.minute()==0&&(now.hour()==0||now.hour()==24)){
    erasedToday=0;
  }
  //get current millis
  unsigned long long currnet_millis=millis();
  //check for temperature changes once every 5 sec(for better speed)
  if(currnet_millis-last_checked>5000&&!manual){
    retry:
    treat(currnet_millis);
    //invalid temp
    if(temp<-20)
      goto retry;
    last_checked=currnet_millis;
  }
  //check for clients
  client=server.available();
  //no client, return
  if(!client){
    yield();
    client.flush();
    return;
  }
  //got a client, wait for it to connect
  unsigned long long started_at=millis();
  while(!client.available()){
    delay(1);
    if(millis()-started_at>300)
    break;
  }
  //get the request
  request=client.readStringUntil('\r');
  Serial.println(request);
  client.flush();
  if(request.indexOf("favicon")!=-1){
    client.flush();
    client.stop();
    return;
  }
  if(request.indexOf("/toggle")!=-1){
    //toggle the relay
    toggle();
    //print the page
    printPage(HOME);
    return;
  }else if(request.indexOf("/restart")!=-1){
    //print the page
    printPage(HOME);
    //restart the esp
    ESP.restart();
    return;
  }else if(request.indexOf("/?value=")!=-1){
    //treat the request
    treatRequest();
    //print the page
    printPage(HOME);
    return;
  }else if(request.indexOf("/OTA")!=-1){
    //OTA programming
    printPage(OTA);
    //turn relay off if in ota mode
    state=0;
    digitalWrite(relay,state);
    //wait for ota to end
    while(1){
    yield();
    ArduinoOTA.handle();
    }
    return;
  }else if(request.indexOf("/log")!=-1){
    //log page
    //read the log
    eepromRead();
    //print the page
    printPage(LOG);
    return;
  }else if(request.indexOf("/time")!=-1){
    //time settings page
    //adjust the time if requested
    treatRequest();
    delay(1000);  
    //print the page
    printPage(TIME);
    return;
  }else if(request.indexOf("/factory_reset")!=-1){
    printPage(HOME);
    treatRequest();
    return;
  }else if(request.indexOf("/settings")!=-1){
    //other settings page
    //treat the request
    treatRequest();
    //print the page
    printPage(SETTINGS);
    return;
  }else{
    //print the default page
    printPage(HOME);
  }
}

void treat(unsigned long long current_millis){
  //get the temperature
  tempSensor.requestTemperatures();
  temp=tempSensor.getTempCByIndex(0);
  int current_time,from_time,to_time;
  //keep current time
  current_time=60*now.hour()+now.minute();
  from_time=60*from_h+from_m;
  to_time=60*to_h+to_m;
  //if the system is scheduled to turn on
  if(current_time>from_time&&current_time<to_time){
    state=1;
    checker=1;
    digitalWrite(relay,state);
    last_change=current_millis;
    mod=0;
    return;
  }else{
    if(checker){
      //turn the relay off if it was on from the schedule
      checker=0;
      state=0;
      digitalWrite(relay,state);
    }
    if(temp>webtemp){
      if(temp>webtemp+6){
        mod=0;
        state=1;
        last_change=current_millis;
        digitalWrite(relay,state);
        return;
      }else if(mod==0){
        mod=1;
        last_change=current_millis;
        state=1;
        digitalWrite(relay,state);
        return;
      }else if(mod==1&&current_millis-last_change>time_on minut){
        state=0;
        digitalWrite(relay,state);
        mod=2;
        last_change=current_millis;
        return;
      }else if(mod==2&&current_millis-last_change>time_off minut){
        mod=0;
        last_change=current_millis;
        return;
      }
      return;
    }else{
      if(mod==1&&current_millis-last_change>time_on minut){
        state=0;
        digitalWrite(relay,state);
        mod=2;
        last_change=current_millis;
        return;
      }
      if(mod==2&&current_millis-last_change>time_off minut){
        mod=0;
        last_change=current_millis;
        return;
      }
      return;
    }
    return;
  }
  return;
}

void printPage(int page){
  if(page==HOME){
    String webpage;
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<title>";
    webpage+="T=";
    webpage+=String(temp);
    webpage+=(char)176;
    webpage+="C";
    webpage+="</title>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<style>";
    webpage+="body,html,center{margin:0;width:100%;float:left;}";
    webpage+=".on{background-color:green;}";
    webpage+=".off{background-color:red;}";
    webpage+=".bt{margin:0;font-size:50px;height:30%;width:100%;padding-top:25%;padding-bottom:25%;margin-bottom:5%;}";
    webpage+="#in{width:70%;height:5%;font-size:32px;margin-bottom:10px;}";
    webpage+="a{text-align:center;text-decoration:none;color:black;}";
    webpage+=".b{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}";
    webpage+=".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;}";
    webpage+=".B{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}";
    webpage+=".separate{width:100%;height:3px;background-color:black;}";
    webpage+="</style>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    if(now.hour()==165){
    webpage+="<a href='/time'><h1 style='font-size:32px'>!Adjust time</h1></a><div class='separate'></div>";
    }
    webpage+="<h1 style=\"font-size:32px;\">";
    webpage+="Temperature = ";
    webpage+=String(temp);
    webpage+=(char)176;
    webpage+="C";
    webpage+="</h1><div class=\"separate\"></div>";
    if(manual)
      webpage+="<h1>Manual Mode</h1>";
    else
      webpage+="<h1>Automatic Mode</h1>";
    webpage+="<a href=\"/toggle\" style=\"width:100%;\"><div class=\"bt ";
    if(state)
      webpage+="on\">ON</div></a>";
    else
      webpage+="off\">OFF</div></a>";
    webpage+="<form method='get'>";
    webpage+="<input type=\"number\" id=\"in\" name='value' value=\"";
    webpage+=(int)webtemp;
    webpage+="\"/>";
    webpage+="<input type='submit' class='B a' value='Submit'/>";
    webpage+="</form>";
    webpage+="<a href=\"/log\" class=\"B a\">Log</a><br/>";
    webpage+="<a href=\"/time\" class=\"B a\">Time</a><br/>";
    webpage+="<a href=\"/settings\" class=\"B a\">Settings</a><br/>";
    webpage+="<a href=\"/restart\" class=\"B a\">Restart</a><br/>";
    webpage+="<a href=\"/\" class=\"B a\">Home</a><br/>";
    webpage+="<a href=\"/OTA\" class=\"B a\">OTA</a><br/>";
    webpage+="<a style='font-size:22px;line-height:40px;' href=\"/factory_reset\" class=\"B a\">Factory Reset</a><br/><br/>";
    webpage+="</center>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
  }else if(page==OTA){
    String webpage;
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<title>OTA MODE</title>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<style>";
    webpage+="a{width:45%;display:inline-block;height:50px;text-decoration:none;padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:55px;}";
    webpage+="</style>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    webpage+="<div style='font-size:45px;margin-top:45%;'>OTA MODE<br/>WAITING...</div><br/>";
    webpage+="<a href='/'>Home</a>";
    webpage+="</center>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
  }else if(page==LOG){
    String webpage;
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<title>Log</title>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<style>";
    webpage+="a{text-decoration:none;}span{float:right;margin-right:20px;}";
    webpage+="body,html,center{margin:0;width:100%;}";
    webpage+=".imp{background-color:#D3D3D3;}";
    webpage+=".par{bakground-color:#C0C0C0;}";
    webpage+=".separator{background-color:#2c2c2c;color:white;backgrond-color:#505256;width:100%;heigth:50px;font-size:32px;}";
    webpage+=".item{width:100%;height:40px;font-size:24px;color:black;line-height:40px;}";
    webpage+=".fix{font-size:28px;float:right;margin-left:13%;padding-right:10%;border-radius:10%;margin-top:155%;width:40%;height:10%;line-height:200%;background-color:black;color:white;position:fixed;}";
    webpage+="</style>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    webpage+="<a href='/' class='fix'>Home</a>";
    webpage+="<div class=\"separator\">Today:";
    webpage+=String(now.day());
    webpage+=" ";
    webpage+=String(Month[now.month()]);
    webpage+="</div>";
    int h=now.hour(),m=now.minute(),d=now.day(),M=now.month();
    m-=m%30;
    int element_no=0;
    for(int i=h*2+m/30+95;i>=96;i--){
      client.print(webpage);
      webpage="";
      yield();
      element_no++;
      if(element_no%2)
        webpage+="<div class=\"imp item\">Temperature=";
      else
        webpage+="<div class=\"par item\">Temperature=";
      yield();
      if(v[i]<100)
        webpage+=String(v[i]);
      else
        webpage+=String(v[i]-100);
      webpage+=(char)176;
      yield();
      if(v[i]<100)
        webpage+="C<span> OFF ";
      else
        webpage+="C<span> ON ";
      webpage+=String(h);
      yield();
      if(h<0)
        webpage+="0";
      webpage+=":";
      if(m==0)
        webpage+="0";
      webpage+=String(m);
      webpage+="</span></div>";
      m-=30;
      if(m<0){
        h--;
        m=30;
      }
    }
    d--;
    if(d==0){
      M--;
      d=days_in_a_month[M];
      if(now.year()%4==0&&d==28&&M==2)
        d++;
    }
    h=23;m=30;
    client.print(webpage);
    webpage="<div class=\"separator\">Yesterday:";
    webpage+=String(d);webpage+=" ";
    webpage+=String(Month[M]);
    webpage+="</div>";
    for(int i=95;i>47;i--){
      client.print(webpage);
      webpage="";
      yield();
      element_no++;
      if(element_no%2)
        webpage+="<div class=\"imp item\">Temperature=";
      else
        webpage+="<div class=\"par item\">Temperature=";
      if(v[i]<100)
        webpage+=String(v[i]);
      else
        webpage+=String(v[i]-100);
      webpage+=(char)176;
      if(v[i]<100)
        webpage+="C<span> OFF ";
      else
        webpage+="C<span> ON ";
      if(h<0)
        webpage+="0";
      webpage+=String(h);
      webpage+=":";
      if(m==0)
        webpage+="0";
      webpage+=String(m);
      webpage+="</span></div>";
      m-=30;
      if(m<0){
        h--;
        m=30;
      }
    }
    d--;
    if(d==0){
      M--;
      d=days_in_a_month[M];
      if(now.year()%4==0&&d==28&&M==2)
        d++;
    }
    h=23;m=30;
    client.print(webpage);
    webpage="<div class=\"separator\">2 days ago:";
    webpage+=String(d);
    webpage+=" ";
    webpage+=String(Month[M]);
    webpage+="</div>";
    for(int i=47;i>=0;i--){
      client.print(webpage);
      webpage="";
      yield();
      element_no++;
      if(element_no%2)
        webpage+="<div class=\"imp item\">Temperature=";
      else
        webpage+="<div class=\"par item\">Temperature=";
      if(v[i]<100)
        webpage+=String(v[i]);
      else
        webpage+=String(v[i]-100);
      webpage+=(char)176;
      if(v[i]<100)
        webpage+="C<span> OFF ";
      else
        webpage+="C<span> ON ";
      if(h<0)
        webpage+="0";
      webpage+=String(h);
      webpage+=":";
      if(m==0)
        webpage+="0";
      webpage+=String(m);
      webpage+="</span></div>";
      m-=30;
      if(m<0){
        h--;
        m=30;
      }
    }
    bool par=element_no%2;
    if(par){
      webpage+="<div style='width:100%;height:55px;' class='par'></div>";
    }else{
      webpage+="<div style='width:100%;height:58px;' class='imp'></div>";
    }
    webpage+="</center>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
  }else if(page==TIME){
    String webpage;
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<title>TIME</title>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+="<style>";
    webpage+="body,html,center{margin:0;width:100%;}";
    webpage+=".bt{margin:0;font-size:50px;height:30%;width:100%;padding-top:25%;padding-bottom:25%;margin-bottom:5%;}";
    webpage+="input{width:70%;height:5%;font-size:32px;margin-top:10px;border-style:solid;border-width:2px;border-color:black;padding-left:15px;}";
    webpage+="a{text-align:center;text-decoration:none;color:black;}";
    webpage+=".b{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}";
    webpage+=".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;}";
    webpage+=".B{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}";
    webpage+=".separate{width:100%;height:3px;background-color:black;margin-bottom:10px;}";
    webpage+="h1{margin-top:10px;}";
    webpage+="</style>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    webpage+="<h1>Set Time";
    webpage+="<div class=\"separate\"></div>";
    webpage+="Date:";
    webpage+=String(now.day());
    webpage+=".";
    webpage+=String(now.month());
    webpage+=".";
    webpage+=String(now.year());
    webpage+="<br/><div class=\"separate\">";
    webpage+="</div>Time:";
    webpage+=String(now.hour());
    webpage+=":";
    webpage+=String(now.minute());
    webpage+=":";
    webpage+=String(now.second());
    webpage+="<br/><form method='get'><input type='number' name='year' placeholder='year...'/><br/>";
    webpage+="<input type='number' name='month' placeholder='month...'/><br/>";
    webpage+="<input type='number' name='day' placeholder='day...'/><br/>";
    webpage+="<input type='number' name='hour' placeholder='hour...'/><br/>";
    webpage+="<input type='number' name='minute' placeholder='minute...'/><br/>";
    webpage+="<input type='number' name='seconds' placeholder='second...'/><br/>";
    webpage+="<input type='submit' class='B a' value='Submit'/>";
    webpage+="</form>";
    webpage+="<a href=\"/\" class=\"B a\">Home</a><br/>";
    webpage+="</center>";
    webpage+="</body>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
  }else if(page==SETTINGS){
    String webpage;
    webpage=header;
    webpage+="<html>";
    webpage+="<head>";
    webpage+="<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0'>";
    webpage+=favicon;
    //webpage+="<link rel='shortcut icon' href='data:image/x-icon;,' type='image/x-icon'>";
    webpage+="<title>Settings</title><style>";
    webpage+="html,body{width:100%;height:100%;margin:0;}";
    webpage+="a{color:black;text-decoration:none;}";
    webpage+=".separate{width:100%;height:3px;background-color:black;}";
    webpage+=".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;width:60%;height:70px;line-height:70px;}";
    webpage+=".input{height:50px;width:80%;border-top-style:groove;padding-left:10px;}";
    webpage+="input{font-size:32px;text-align:center;}";
    webpage+=".submit{margin-top:15px;height:50px;width:60%;margin-bottom:20px;}";
    webpage+=".small{width:30%;height:50px;border-top-style:groove;margin-left:5%;margin-right:5%;}";
    webpage+="form{font-size:50px;}";
    webpage+="h1{font-size:40px;}";
    webpage+="</style>";
    webpage+="</head>";
    client.print(webpage);
    webpage="<body>";
    webpage+="<center>";
    webpage+="<h1>Time On<br/>- in minutes -</h1>";
    webpage+="<form method='get'>";
    webpage+="<input class='input' type='number' name='timeon' value='";
    webpage+=String(time_on);
    webpage+="'/>";
    webpage+="<input class='submit' type='submit' value='Submit'/>";
    webpage+="</form>";
    webpage+="<div class='separate'></div>";
    webpage+="<h1>Time Off<br/>- in minutes -</h1>";
    webpage+="<form method='get'>";
    webpage+="<input class='input' type='number' name='timeoff' value='";
    webpage+=String(time_off);
    webpage+="'/>";
    webpage+="<input class='submit' type='submit' value='Submit'/>";
    webpage+="</form>";
    webpage+="<div class='separate'></div>";
    webpage+="<h1 style='margin-bottom:5px;'>Keep on<br/>- from -</h1>";
    webpage+="<form method='get'>";
    webpage+="<input class='small' type='number' name='from_h' value='";
    webpage+=String(from_h);
    webpage+="'/> : ";
    webpage+="<input class='small' type='number' name='from_m' value='";
    webpage+=String(from_m);
    webpage+="'/>";
    webpage+="<h1 style='margin-top:5px;margin-bottom:5px;'>- to -</h1>";
    webpage+="<input class='small' type='number' name='to_h' value='";
    webpage+=String(to_h);
    webpage+="'/> : ";
    webpage+="<input class='small' type='number' name='to_m' value='";
    webpage+=String(to_m);
    webpage+="'>";
    webpage+="<input type='submit' class='submit' value='Submit'/>";
    webpage+="</form>";
    webpage+="<div class='separate'></div>";
    webpage+="<a href='/'><div class='a'>Home</div></a>";
    webpage+="</html>";
    client.print(webpage);
    client.flush();
    client.stop();
  }
}

void treatRequest(){
  if(request.indexOf("/factory_reset")!=-1){
    full_reset();
  }
  if(request.indexOf("/time?year")!=-1){
    int a=request.substring(request.indexOf("?year=")+6,request.indexOf("&month")).toInt();
    int b=request.substring(request.indexOf("&month=")+7,request.indexOf("&day")).toInt();
    int c=request.substring(request.indexOf("&day=")+5,request.indexOf("&hour")).toInt();
    int d=request.substring(request.indexOf("&hour=")+6,request.indexOf("&minute")).toInt();
    int e=request.substring(request.indexOf("&minute=")+8,request.indexOf("&second")).toInt();
    int f=request.substring(request.indexOf("&seconds=")+9,request.indexOf(" HTTP")).toInt();
    Serial.println(a);
    Serial.println(b);
    Serial.println(c);
    Serial.println(d);
    Serial.println(e);
    Serial.println(f);
    rtc.adjust(DateTime(a,b,c,d,e,f));
    now=rtc.now();
  }else if(request.indexOf("?value=")!=-1){
    //got a new value for webtemp
    webtemp=(byte)request.substring(request.indexOf("?value=")+7,request.indexOf(" HTTP")).toInt();
    Serial.print("webtemp=");
    Serial.println(webtemp);
    //write the change to eeprom
    EEPROM.begin(512);
    EEPROM.write(500,webtemp);
    EEPROM.end();
    //toggle the relay
    if(webtemp>temp)
      state=0;
    else
      state=1;
    manual=0;
    digitalWrite(relay,state);
  }else if(request.indexOf("/settings")!=-1){
    if(request.indexOf("timeon=")!=-1){
      time_on=request.substring(request.indexOf("timeon=")+7,request.indexOf(" HTTP")).toInt();
      if(time_on<0)
        time_on=45;
      EEPROM.begin(512);
      EEPROM.write(501,time_on);
      EEPROM.end();
    }
    if(request.indexOf("timeoff=")!=-1){
      time_off=request.substring(request.indexOf("timeoff=")+8,request.indexOf(" HTTP")).toInt();
      EEPROM.begin(512);
      EEPROM.write(502,time_off);
      EEPROM.end();
    }
    if(request.indexOf("to_h")!=-1){
      EEPROM.begin(512);
      from_h=request.substring(request.indexOf("from_h=")+7,request.indexOf("&from_m")).toInt();
      from_m=request.substring(request.indexOf("from_m=")+7,request.indexOf("&to_h")).toInt();
      to_h=request.substring(request.indexOf("to_h=")+5,request.indexOf("&to_m")).toInt();
      to_m=request.substring(request.indexOf("to_m=")+5,request.indexOf(" HTTP")).toInt();
      EEPROM.write(503,from_h);
      EEPROM.write(504,from_m);
      EEPROM.write(505,to_h);
      EEPROM.write(506,to_m);
      EEPROM.end();
    }
  }
  return;
}
