// Include required libraries
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

#include <MQUnifiedsensor.h>

/*Hardware Related Macros*/
#define Board ("ESP32")
#define Pin (33) // Pin input analog pada ESP32

#define Type ("MQ-3")            // Tipe sensor MQ3
#define Voltage_Resolution (3.3)    // Tegangan referensi yang digunakan pada pin analog
#define ADC_Bit_Resolution (12)   // Resolusi bit ADC pada ESP32
#define RatioMQ3CleanAir (60)     // RS / R0 = 60 ppm 

/*Globals*/
MQUnifiedsensor MQ3(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

// Define WiFi credentials
#define WIFI_SSID "uno"
#define WIFI_PASSWORD "satuduatigaempat"

// Define Firebase API Key, Project ID, and user credentials
#define DATABASE_URL "https://informasi-kadar-alkohol-pengen-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define API_KEY "AIzaSyAOhq8osK5Su8JMzWf9rMm80qALfamNK8k"
#define DATABASE_AUTH "J2JMNJ1OfLhh3cADOn9l6rKOvhgPXNiNpeTHpjDc"
#define FIREBASE_PROJECT_ID "informasi-kadar-alkohol-pengen"

#define USER_EMAIL "lisafajrin2304@gmail.com"
#define USER_PASSWORD "Lisa1234!"

// Define Firebase Data object, Firebase authentication, and configuration
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long previousMillis = 0;
const long interval = 1000; 

void setup() {
  Serial.begin(115200); // Inisialisasi komunikasi serial

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Print Firebase client version
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  // Assign the API key
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_AUTH;
  Firebase.begin(&config, &auth);
  config.api_key = API_KEY;

  // Assign the user sign-in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the callback function for the long-running token generation task
  config.token_status_callback = tokenStatusCallback;  // see addons/TokenHelper.h

  // Begin Firebase with configuration and authentication
  Firebase.begin(&config, &auth);

  // Reconnect to Wi-Fi if necessary
  Firebase.reconnectWiFi(true);

  // Set Indonesian time (GMT+7) NTP server
  configTime(7 * 3600, 0, "id.pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    delay(1000);
    Serial.println("Waiting for time to synchronize...");
  }
  Serial.println("Time synchronized");
  
  // Setel model matematika untuk menghitung konsentrasi PPM dan nilai konstanta
  MQ3.setRegressionMethod(1); //_PPM =  a*ratio^b
  MQ3.setA(4.8387);
  MQ3.setB(-2.68); // Konfigurasi persamaan untuk menghitung konsentrasi Benzena

  // Inisialisasi sensor MQ3
  MQ3.init();

  // Kalibrasi sensor MQ3
  Serial.print("Kalibrasi, mohon tunggu...");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ3.update(); // Perbarui data, Arduino akan membaca tegangan dari pin analog
    calcR0 += MQ3.calibrate(RatioMQ3CleanAir);
    Serial.print(".");
  }
  MQ3.setR0(calcR0 / 10);
  Serial.println("  selesai!");

  if (isinf(calcR0)) {
    Serial.println("Peringatan: Masalah koneksi, R0 tak terhingga (Deteksi sirkuit terbuka) harap periksa kabel dan sumber daya Anda");
    while (1);
  }
  if (calcR0 == 0) {
    Serial.println("Peringatan: Masalah koneksi ditemukan, R0 nol (Pin analog terhubung ke ground) harap periksa kabel dan sumber daya Anda");
    while (1);
  }
}

void loop() {
  // Create a FirebaseJson object for storing data
  FirebaseJson content;
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
  
    MQ3.update();     // Perbarui data, Arduino akan membaca tegangan dari pin analog
    MQ3.readSensor(); // Sensor akan membaca konsentrasi PPM menggunakan model, nilai a dan b yang ditetapkan sebelumnya atau dari setup

    // Konversi tegangan menjadi persentase
    float voltage = MQ3.getVoltage(); // Baca tegangan
    float maxVoltage = 4.58;   // Misalnya, tegangan maksimum adalah 5V
    int percentage = (int)((voltage / maxVoltage) * 100); // Konversi ke persen

    // Memeriksa apakah pengemudi dinyatakan mabuk
    String Status;
    if (percentage >= 10) {
      Status = "Pengemudi mabuk!";
    } else {
      Status = "Pengemudi normal.";
    }

    Serial.print("Alcohol Level: ");
    Serial.print(percentage); // Menampilkan persentase sebagai bilangan bulat
    Serial.println("%");

    //0,07 - 0,1 = batas pengemudi minum alkohol

    time_t now = time(nullptr);
    struct tm *timeinfo;
    char timeString[25];
    
    timeinfo = localtime(&now);
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    Serial.print("Current date and time: ");
    Serial.println(timeString);

    // Delay before the next reading
    delay(1000); //Frekuensi pengambilan sampel

    // Send data to Firebase
    if (Firebase.RTDB.setInt(&fbdo, "/Alkohol/Kadar_Alkohol", percentage) &&
        Firebase.RTDB.setString(&fbdo, "/Alkohol/Status", Status) &&
        Firebase.RTDB.setString(&fbdo, "/Alkohol/Time", timeString)) {
      Serial.println("Upload Firebase RTDB success!");
    } else {
      Serial.println("Upload failed.");
      Serial.println("Reason: " + fbdo.errorReason());
    }

    // Check if the values are valid (not NaN)
    if (!isnan(percentage) && !Status.isEmpty()) {
      // Set the 'presentase' and 'status' fields in the FirebaseJson object
      content.set("fields/Kadar_Alkohol/integerValue", String(percentage));
      content.set("fields/Status/stringValue", Status);
      content.set("fields/Time/stringValue", timeString);

      Serial.print("Update/Add Data... ");
      // Increment the last entry value
      char timeFire[25];
      strftime(timeFire, sizeof(timeFire), "%Y%m%d%H%M%S", timeinfo);
      Serial.print("Tanggal dan waktu saat ini: ");
      Serial.println(timeFire);

      // Construct the subcollection path with the incremented value as the document ID
      String subcollectionPath = "Alkohol/" + String(timeFire) + "Data/";
      Serial.println("Subcollection Path: " + subcollectionPath);
    
      // Firestore code
      if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", subcollectionPath.c_str(), content.raw(), "Kadar_Alkohol") && 
          Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", subcollectionPath.c_str(), content.raw(), "Status") && 
          Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", subcollectionPath.c_str(), content.raw(), "Time")) {
        Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
      } else {
        Serial.println(fbdo.errorReason());
      }
    } else {
      Serial.println("Failed to read data.");
    }

    // Delay before the next reading
    delay(5000);
  }
}
