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
#define relay 0
#define minut *60000
#define HOME 0
#define OTA 1
#define LOG 2
#define TIME 3
#define SETTINGS 4

//rtc objects
RTC_DS1307 rtc;
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
const char* ssid = "......";
const char* password = "......";
//ap ssid and pass
const char* ap_ssid = "PoolControl";
const char* ap_password = "";

//temperature
byte Val;

//the server is on port 80(default)
WiFiServer server(80);

WiFiClient client;
String request;

//other variables
float temp;
float webtemp=35;
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
char Month[13][13]={"NONE","Jan.","Feb.","March","Apr.","May","June","July","Aug.","Sept.","Oct.","Nov.","Dec."};
int days_in_a_month[12]={31,28,31,30,31,30,31,31,30,31,30,31};

//checker is 1 when the system is scheduled to turn on
byte checker;

//function used to turn on/off the pump depending on the situation
void treat(unsigned long long);

//function used for printing the page to the client
void printPage(int);

//function used to treat the request
void treatRequest();

void setup() {
	//begin the rtc module, restart if it fails
	if(!rtc.begin())
		ESP.restart();
	//begin wifi comunication
	WiFi.mode(WIFI_AP_STA);
	WiFi.begin(ssid,password);
	WiFi.softAP(ap_ssid,ap_password);
	//begin serial comunication
	Serial.begin(9600);
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
	//wait for wifi to connect
	while (WiFi.status() != WL_CONNECTED){
		delay(500);
		Serial.print('.');
	}
	//start the server
	server.begin();
	Serial.println(WiFi.localIP());
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
}

void loop(){
	//get currnent time
	now = rtc.now();
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
	while(!client.available()){
		delay(1);
	}
	//get the request
	request=client.readStringUntil('\r');
	Serial.println(request);
	client.flush();
	if(request.indexOf("/toggle")!=-1){
		//toggle the relay
		toggle();
		//print the page
		printPage(HOME);
		return;
	}else if(request.indexOf("/restart=")!=-1){
		//print the page
		printPage(HOME);
		//restart the esp
		ESP.restart();
		return;
	}else if(request.indexOf("/value=")!=-1){
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
		//print the page
		printPage(TIME);
		return;
	}else if(request.indexOf("/settime=")!=-1){
    //set the time
		//treat the request
		treatRequest();
		//print the page
		printPage(HOME);
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
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println(""); //  do not forget this one
		client.println("<!DOCTYPE HTML>");
		client.println("<html>");
		client.println("<head>");
		client.println("<title>");
		client.print("T=");client.print((int)temp);client.print((char)176);client.println("C");
		client.println("</title>");
		client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
		client.println("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js\"></script>");
		client.println("<style>");
		client.println("body,html,center{margin:0;width:100%;float:left;}");
		client.println(".on{background-color:green;}");
		client.println(".off{background-color:red;}");
		client.println(".bt{margin:0;font-size:50px;height:30%;width:100%;padding-top:25%;padding-bottom:25%;margin-bottom:5%;}");
		client.println("#in{width:70%;height:5%;font-size:32px;margin-bottom:10px;}");
		client.println("a{text-align:center;text-decoration:none;color:black;}");
		client.println(".b{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}");
		client.println(".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;}");
		client.println(".B{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}");
		client.println(".separate{width:100%;height:3px;background-color:black;}");
		client.println("</style>");
		client.println("<script>");
		client.print("function f(){var x=document.getElementById(\"in\").value;$(\".b\").hide();$(\".prependable\").prepend(\"<a href='/value=\"+x+\"' class='b a'>Set \"+x+\"");client.print((char)176);client.println("C</a>\");}");
		client.println("</script>");
		client.println("</head>");
		client.println("<body>"); 
		client.println("<center>");
		client.println("<h1 style=\"font-size:32px;\">");
		client.print("Temperature = ");client.print((int)temp);client.print((char)176);client.println("C");
		client.println("</h1><div class=\"separate\"></div>");
		if(manual)
			client.println("<h1>Manual Mode</h1>");
		else
			client.println("<h1>Automatic Mode</h1>");
		client.print("<a href=\"/toggle\" style=\"width:100%;\"><div class=\"bt ");
		if(state)
			client.println("on\">ON</div></a>");
		else
			client.println("off\">OFF</div></a>");
		client.print("<input type=\"number\" id=\"in\" value=\"");
		client.print((int)webtemp);
		client.println("\"/>");
		client.println("<button onclick=\"f()\" class=\"b\">Prepare</button><div class=\"prependable\"></div>");
		client.println("<a href=\"/\" class=\"B a\">Home</a><br/>");
		client.println("<a href=\"/OTA\" class=\"B a\">OTA</a><br/>");
		client.println("<a href=\"/log\" class=\"B a\">Log</a><br/>");
		client.println("<a href=\"/time\" class=\"B a\">Time</a><br/>");
		client.println("<a href=\"/settings\" class=\"B a\">Settings</a><br/>");
		client.println("</center>");
		client.println("</body>");
		client.println("</html>");
		client.flush();
		client.stop();
	}else if(page==OTA){
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println(""); //  do not forget this one
		client.println("<!DOCTYPE HTML>");
		client.println("<html>");
		client.println("<head>");
		client.println("<title>OTA MODE</title>");
		client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
		client.println("<style>a{width:45%;display:inline-block;height:50px;text-decoration:none;padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:55px;}</style>");
		client.println("</head>");
		client.println("<body>");
		client.println("<center><div style=\"font-size:45px;margin-top:45%;\">OTA MODE<br/>WAITING...</div><br/>");
		client.println("<a href=\"/\">Home</a></center>");
		client.println("</body>");
		client.println("</html>");
		client.flush();
		client.stop();
	}else if(page==LOG){
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println(""); //  do not forget this one
		client.println("<!DOCTYPE HTML>");
		client.println("<html>");
		client.println("<head>");
		client.println("<title>Log</title>");
		client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
		client.println("<style>");
		client.println("a{text-decoration:none;}span{float:right;margin-right:20px;}body,html,center{margin:0;width:100%;}.imp{background-color:#D3D3D3;}.par{bakground-color:#C0C0C0;}.separator{background-color:#2c2c2c;color:white;backgrond-color:#505256;width:100%;heigth:50px;font-size:32px;}.item{width:100%;height:40px;font-size:24px;color:black;line-height:40px;}.fix{font-size:28px;float:right;margin-left:15%;padding-right:10%;border-radius:10%;margin-top:160%;width:40%;height:10%;line-height:200%;background-color:black;color:white;position:fixed;}");
		client.println("</style>");
		client.println("</head>");
		client.println("<body><center><a href='/' class='fix'>Home</a>");
		client.print("<div class=\"separator\">Today:");client.print(now.day());client.print(" ");client.print(Month[now.month()]);client.println("</div>");
		int h=now.hour(),m=now.minute(),d=now.day(),M=now.month();
		m-=m%30;
		int element_no=0;
		for(int i=now.hour()*2+now.minute()/30+95;i>=96;i--){
			yield();
			element_no++;
			if(element_no%2)
				client.print("<div class=\"imp item\">Temperature=");
			else
				client.print("<div class=\"par item\">Temperature=");
			if(v[i]<100)
				client.print(v[i]);
			else
				client.print(v[i]-100);
			client.print((char)176);
			if(v[i]<100)
				client.print("C<span> OFF ");
			else
				client.print("C<span> ON ");
			client.print(h);
			if(h<0)
				client.print(0);
			client.print(':');
			if(m==0)
				client.print(0);
			client.print(m);
			client.println("</span></div>");
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
		client.print("<div class=\"separator\">Yesterday:");
		client.print(d);client.print(" ");
		client.print(Month[M]);
		client.println("</div>");
		for(int i=95;i>47;i--){
			element_no++;
			if(element_no%2)
				client.print("<div class=\"imp item\">Temperature=");
			else
				client.print("<div class=\"par item\">Temperature=");
			if(v[i]<100)
				client.print(v[i]);
			else
				client.print(v[i]-100);
			client.print((char)176);
			if(v[i]<100)
				client.print("C<span> OFF ");
			else
				client.print("C<span> ON ");
			if(h<0)
				client.print(0);
			client.print(h);
			client.print(':');
			if(m==0)
				client.print(0);
			client.print(m);
			client.println("</span></div>");
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
		client.print("<div class=\"separator\">2 days ago:");
		client.print(d);client.print(" ");
		client.print(Month[M]);
		client.println("</div>");
		for(int i=47;i>=0;i--){
			yield();
			element_no++;
			if(element_no%2)
				client.print("<div class=\"imp item\">Temperature=");
			else
				client.print("<div class=\"par item\">Temperature=");
			if(v[i]<100)
				client.print(v[i]);
			else
				client.print(v[i]-100);
			client.print((char)176);
			if(v[i]<100)
				client.print("C<span> OFF ");
			else
				client.print("C<span> ON ");
			if(h<0)
				client.print(0);
			client.print(h);
			client.print(':');
			if(m==0)
				client.print(0);
			client.print(m);
			client.println("</span></div>");
			m-=30;
			if(m<0){
				h--;
				m=30;
			}
		}
		client.println("</center></body></html>");
		client.flush();
		client.stop();
	}else if(page==TIME){
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println(""); //  do not forget this one
		client.println("<!DOCTYPE HTML>");
		client.println("<html>");
		client.println("<head>");
		client.println("<title>TIME</title>");
		client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
		client.println("<script src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js\"></script>");
		client.println("<script>function f(){var y=document.getElementById(\"year\").value;var M=document.getElementById(\"month\").value;var d=document.getElementById(\"day\").value;var h=document.getElementById(\"hour\").value;var m=document.getElementById(\"minute\").value;var s=document.getElementById(\"seconds\").value;$(\".b\").hide();$(\".prependable\").prepend(\"<a class='B a' href='/settime=\"+y+M+d+h+m+s+\"'>Set Time</a>\");}</script>");
		client.println("<style>");
		client.println("body,html,center{margin:0;width:100%;}");
		client.println(".bt{margin:0;font-size:50px;height:30%;width:100%;padding-top:25%;padding-bottom:25%;margin-bottom:5%;}");
		client.println("input{width:70%;height:5%;font-size:32px;margin-top:10px;border-style:solid;border-width:2px;border-color:black;padding-left:15px;}");
		client.println("a{text-align:center;text-decoration:none;color:black;}");
		client.println(".b{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}");
		client.println(".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;}");
		client.println(".B{width:45%;display:inline-block;height:50px;margin-top:15px;font-size:32px;}");
		client.println(".separate{width:100%;height:3px;background-color:black;margin-bottom:10px;}");
		client.println("h1{margin-top:10px;}");
		client.println("</style>");
		client.println("</head>");
		client.println("<body><center>");
		client.println("<h1>Set Time<div class=\"separate\"></div>");
		client.print("Date:");
		client.print(now.day());client.print('.');client.print(now.month());client.print('.');client.print(now.year());
		client.print("<br/><div class=\"separate\"></div>Time:");
		client.print(now.hour());client.print(':');client.print(now.minute());client.print(':');client.print(now.second());
		client.println("<br/><input type='number' id='year' placeholder='year...'/><br/>");
		client.println("<input type='number' id='month' placeholder='month...'/><br/>");
		client.println("<input type='number' id='day' placeholder='day...'/><br/>");
		client.println("<input type='number' id='hour' placeholder='hour...'/><br/>");
		client.println("<input type='number' id='minute' placeholder='minute...'/><br/>");
		client.println("<input type='number' id='seconds' placeholder='second...'/><br/>");
		client.println("<button onclick=\"f()\" class=\"b\">Prepare</button><div class=\"prependable\"></div>");
		client.println("<a href=\"/\" class=\"B a\">Home</a><br/>");
		client.println("</center></body>");
		client.println("</html>");
		client.flush();
		client.stop();
	}else if(page==SETTINGS){
		client.println("HTTP/1.1 200 OK");
		client.println("Content-Type: text/html");
		client.println(""); //  do not forget this one
		client.println("<!DOCTYPE HTML>");
		client.println("<html>");
		client.println("<head>");
		client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
		client.println("<title>Settings</title><style>");
		client.println("html,body{width:100%;height:100%;margin:0;}");
		client.println("a{color:black;text-decoration:none;}");
		client.println(".separate{width:100%;height:3px;background-color:black;}");
		client.println(".a{padding:1px 6px;align-items: flex-start;text-align: center;cursor: default;color: buttontext;background-color: buttonface;box-sizing: border-box;border-width: 2px;border-style: outset;border-color: buttonface;border-image: initial;text-rendering: auto;letter-spacing: normal;word-spacing: normal;text-transform: none;text-indent: 0px;text-shadow: none;margin: 0em 0em 0em 0em;-webkit-writing-mode: horizontal-tb;-webkit-appearance: button;font-size:32px;font-weight:600;margin-top:15px;width:60%;height:70px;line-height:70px;}");
		client.println(".input{height:50px;width:80%;border-top-style:groove;padding-left:10px;}");
		client.println("input{font-size:32px;text-align:center;}");
		client.println(".submit{margin-top:15px;height:50px;width:60%;margin-bottom:20px;}");
		client.println(".small{width:30%;height:50px;border-top-style:groove;margin-left:5%;margin-right:5%;}");
		client.println("form{font-size:50px;}");
		client.println("h1{font-size:40px;}");
		client.println("</style></head>");
		client.println("<body><center>");
		client.print("<h1>Time On<br/>- in minutes -</h1><form method='get'><input class='input' type='number' name='timeon' value='");
		client.print(time_on);
		client.print("'/><input class='submit' type='submit' value='Submit'/></form><div class='separate'></div>");
		client.print("<h1>Time Off<br/>- in minutes -</h1><form method='get'><input class='input' type='number' name='timeoff' value='");
		client.print(time_off);
		client.print("'/><input class='submit' type='submit' value='Submit'/></form><div class='separate'></div>");
		client.print("<h1 style='margin-bottom:5px;'>Keep on<br/>- from -</h1><form method='get'><input class='small' type='number' name='from_h' value='");
		client.print(from_h);
		client.print("'/> : <input class='small' type='number' name='from_m' value='");
		client.print(from_m);
		client.print("'/><h1 style='margin-top:5px;margin-bottom:5px;'>- to -</h1><input class='small' type='number' name='to_h' value='");
		client.print(to_h);
		client.print("'/> : <input class='small' type='number' name='to_m' value='");
		client.print(to_m);
		client.println("'><input type='submit' class='submit' value='Submit'/></form><div class='separate'></div><a href='/'><div class='a'>Home</a></a>");
		client.println("</html>");
		client.flush();
		client.stop();
	}
}

void treatRequest(){
	if(request.indexOf("/settime=")!=-1){
		String nval=request.substring(request.indexOf("/settime=")+9,request.indexOf("/settime=")+23);
		int a=nval.substring(0,4).toInt();
		int b=nval.substring(4,6).toInt();
		int c=nval.substring(6,8).toInt();
		int d=nval.substring(8,10).toInt();
		int e=nval.substring(10,12).toInt();
		int f=nval.substring(12,14).toInt();
		Serial.println(a);
		Serial.println(b);
		Serial.println(c);
		Serial.println(d);
		Serial.println(e);
		Serial.println(f);
		rtc.adjust(DateTime(a,b,c,d,e,f));
	}else if(request.indexOf("/value=")!=-1){
		//got a new value for webtemp
		webtemp=request.substring(request.indexOf("/value=")+7,request.indexOf("/value=")+9).toInt();
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
