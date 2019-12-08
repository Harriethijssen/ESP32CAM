/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp32-cam-take-photo-display-web-server/
  
  IMPORTANT!!! 
   - Select Board "AI Thinker ESP32-CAM"
   - GPIO 0 must be connected to GND to upload a sketch
   - After connecting GPIO 0 to GND, press the ESP32-CAM on-board RESET button to put your board in flashing mode
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>

#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>

#include <Update.h>

#include <SD_MMC.h>
#include <FS.h>
#include <EEPROM.h>            // read and write from flash memory

#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "FS.h"                // SD Card ESP32
#include "SD_MMC.h"            // SD Card ESP32
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
// #include "dl_lib.h"
#include "driver/rtc_io.h"
#include <EEPROM.h>            // read and write from flash memory


// define the number of bytes you want to access
#define EEPROM_SIZE 1


// Replace with your network credentials
const char* hostNamePrefix = "esp32Cam-";
const char* ssid = "DouglasFibre Basement24";
const char* password = "ditnetwerkisbeveiligdmetwpa";

// Create AsyncWebServer object on port 80 
AsyncWebServer server(80);

// boolean takeNewPhoto = false;

// Photo File Name to save in SPIFFS
// #define FILE_PHOTO "/photo.jpg"

// Pin definition for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

const char* loginIndex PROGMEM = 
R"rawliteral(
  <form name='loginForm'>
    <table width='20%' bgcolor='A09F9F' align='center'>
        <tr>
            <td colspan=2>
                <center><font size=4><b>ESP32 Login Page</b></font></center>
                <br>
            </td>
            <br>
            <br>
        </tr>
        <td>Username:</td>
        <td><input type='text' size=25 name='userid'><br></td>
        </tr>
        <br>
        <br>
        <tr>
            <td>Password:</td>
            <td><input type='Password' size=25 name='pwd'><br></td>
            <br>
            <br>
        </tr>
        <tr>
            <td><input type='submit' onclick='check(this.form)' value='Login'></td>
        </tr>
    </table>
  </form>
  <div id='container'>
    <h2>ESP32-CAM Last Photo</h2>
    <p>It might take more than 5 seconds to capture a photo.</p>
    <p>
      <button onclick='rotatePhoto();'>ROTATE</button>
      <button onclick='capturePhoto()'>CAPTURE PHOTO</button>
      <button onclick='location.reload();'>REFRESH PAGE</button>
    </p>
  </div>
  <div><img src='saved-photo' id='photo' width='70%'></div>
  <script>
    var deg = 0;
    function capturePhoto() {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', "/capture", true);
      xhr.send();
    }
    function rotatePhoto() {
      var img = document.getElementById("photo");
      deg += 90;
      if(isOdd(deg/90)){ document.getElementById("container").className = "vert"; }
      else{ document.getElementById("container").className = "hori"; }
      img.style.transform = "rotate(" + deg + "deg)";
    }
    function isOdd(n) { return Math.abs(n % 2) == 1; }
    function check(form) {
      if(form.userid.value=='admin' && form.pwd.value=='admin') window.open('/serverIndex');
      else alert('Error Password or Username'); /*displays error message*/
    }
  </script>
)rawliteral";
 
/*
 * Server Index Page
 */
 
const char* serverIndex = 
R"rawliteral(
  <script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>
  <form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>
    <input type='file' name='update'>
    <input type='submit' value='Update'>
  </form>
  <div id='prg'>progress: 0%</div>
  <script>
    $('form').submit(function(e){
      e.preventDefault();
      var form = $('#upload_form')[0];
      var data = new FormData(form);
      $.ajax({
        url: '/update',
        type: 'POST',
        data: data,
        contentType: false,
        processData:false,
        xhr: function() {
          var xhr = new window.XMLHttpRequest();
          xhr.upload.addEventListener('progress', function(evt) {
            if (evt.lengthComputable) {
              var per = evt.loaded / evt.total;
              $('#prg').html('progress: ' + Math.round(per*100) + '%');
            }
          }, false);
          return xhr;
        },
        success:function(d, s) { console.log('success!'); },
        error: function (a, b, c) {}
      });
    });
  </script>
)rawliteral";

const char* handleUpdateHtml = 
R"rawliteral(
  <form method='POST' action='/update' enctype='multipart/form-data'>
    <input type='file' name='update'>
    <input type='submit' value='Update'>
  </form>
)rawliteral";

// forward declarations
void capturePhotoSaveSD_MMC( void );
void handleUpdate(AsyncWebServerRequest *request);
void handleUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
void takeTestPicture();

int pictureNumber = 0;
void setup() {


  // Serial port for debugging purposes
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();


  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  Serial.printf("psramFound: %s -frame_size: %d, -jpeg_quality: %d, -fb_count: %d\n", 
                (psramFound()) ? "True":"False", config.frame_size, config.jpeg_quality, config.fb_count);

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
  else {
    Serial.println("Camera initialized successfully");
  }

    //Serial.println("Starting SD Card");
  if(!SD_MMC.begin()){
    Serial.println("SD_MMC Mount Failed");
    return;
  }
  else {
    delay(500);
    Serial.println("SD_MMC mounted successfully");
  }

  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }

  takeTestPicture();
  
  delay(1000);

  takeTestPicture();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  delay(500);
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  char hostName[128];
  sprintf(hostName, "%s%d", hostNamePrefix, WiFi.localIP()[3]);
  Serial.print("HostName: ");
  Serial.println(hostName);

  takeTestPicture();

  // /*use mdns for host name resolution*/
  // if (!MDNS.begin(hostName)) { //http://{hostNamePrefix}{last octet from IPaddress}
  //   Serial.println("Error setting up MDNS responder!");
  //   while (1) {
  //     delay(1000);
  //   }
  // }

  // server.on("/", HTTP_GET, 
  //   [](AsyncWebServerRequest * request) {request->send_P(200, "text/html", loginIndex);}
  // );

  // server.on("/capture", HTTP_GET, 
  //   [](AsyncWebServerRequest * request) {
  //     takeNewPhoto = true;
  //     request->send_P(200, "text/plain", "Taking Photo");
  //   }
  // );

  // server.on("/saved-photo", HTTP_GET, 
  //   [](AsyncWebServerRequest * request) { request->send(SD_MMC, FILE_PHOTO, "image/jpg", false); }
  // );

  // server.on("/serverIndex", HTTP_GET, 
  //   [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", serverIndex);}
  // );

  // /*handling uploading firmware file */
  // server.on("/update", HTTP_GET, 
  //   [](AsyncWebServerRequest *request){ handleUpdate(request); }
  // );

  // server.on("/update", HTTP_POST,
  //   [](AsyncWebServerRequest *request) {},
  //   [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
  //                 size_t len, bool final) {
  //     handleUpdate(request, filename, index, data, len, final);
  //   }
  // );

  // Start server
  server.begin();

  // delay(2000);
  // Serial.println("Going to sleep now");
  // delay(2000);
  // esp_deep_sleep_start();
  // Serial.println("This will never be printed");
}

void loop() {
  // if (takeNewPhoto) {
  //   capturePhotoSaveSD_MMC();
  //   takeNewPhoto = false;
  // }
  // delay(1);
}

void takeTestPicture() {
  camera_fb_t * fb = NULL;
  
  // Take Picture with Camera
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }

    // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;

  // Path where new picture will be saved in SD Card
  String path = "/picture" + String(pictureNumber) +".jpg";

  fs::FS &fs = SD_MMC; 
  Serial.printf("Picture file name: %s\n", path.c_str());
  
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
  } 
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb); 
  
  // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  rtc_gpio_hold_en(GPIO_NUM_4);

}

// Capture Photo and Save it to SD_MMC
// void capturePhotoSaveSD_MMC( void ) {
//   Serial.println("DEBUG: capturePhotoSaveSpiff");
//   camera_fb_t * fb = NULL; // pointer

//   // Take a photo with the camera
//   Serial.println("Taking a photo...");

//   unsigned long startTime = millis();
//   fb = esp_camera_fb_get();
//   unsigned long duration = millis() - startTime;
//   if (!fb) {
//     Serial.println("Camera capture failed");
//     return;
//   }

//   // Photo file name
//   Serial.printf("Picture file name: %s -Duration: %lums -FrameSize: %d bytes)\n", FILE_PHOTO, duration, fb->len);
  
//   // Open file in using SPIFFS
//   fs::FS &fs = SD_MMC;
//   File file = fs.open(FILE_PHOTO, FILE_WRITE);
//   Serial.printf("DEBUG: after file open\n"); 

//   // Insert the data in the photo file
//   if (!file) {
//     Serial.println("Failed to open file in writing mode");
//   }
//   else {
//     file.write(fb->buf, fb->len); // payload (image), payload length
//     file.flush();
//     Serial.printf("The picture has been saved in %s - Size %d bytes\n", FILE_PHOTO, file.size());
//   }

//   // Close the file
//   file.close();

//   esp_camera_fb_return(fb);
// }

// void handleUpdate(AsyncWebServerRequest *request) {
//   request->send_P(200, "text/html", handleUpdateHtml);
// }

// void handleUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
//   if (!index){
//     Serial.println("Update");

//     // if filename includes spiffs, update the spiffs partition
//     int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
//     if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
//       Update.printError(Serial);
//     }
//   }

//   if (Update.write(data, len) != len) {
//     Update.printError(Serial);
//   }

//   if (final) {
//     AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
//     response->addHeader("Refresh", "20");  
//     response->addHeader("Location", "/");
//     request->send(response);
//     if (!Update.end(true)){
//       Update.printError(Serial);
//     } else {
//       Serial.println("Update complete");
//       Serial.flush();
//       ESP.restart();
//     }
//   }
// }
