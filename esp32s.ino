/*
 * Bryan Zhu
 * Daniel Lu
 * HackNJIT 2018 Fall
 * TATA Consultancy Services
 */

#include <String>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

#define SOIL_MOIST_PIN 32

#define TEMP_HUMID_PIN 33
#define DHTTYPE DHT11

#define PHOTO_DARK_PIN 34
#define MOTION_DARK_PIN 35

/*
 * HiLetgo ESP32S Dev Board
 * https://www.es.co.th/Schemetic/PDF/ESP32.PDF
 * https://randomnerdtutorials.com/esp32-pinout-reference-gpios/ (for ESP32, not ESP32S)
 */

const int UPDATE_FREQ_JSON = 1000;
const int UPDATE_FREQ_WEATHER = 1080000/UPDATE_FREQ_JSON;  // every 3 hours

/* Sensor Thresholds (taken from measurements) */
const int MOISTURE_VALUE_WATER = 1500;
const int MOISTURE_VALUE_AIR = 3000;
const int LUM_VALUE_BRIGHT = 50;
const int LUM_VALUE_DARK = 3000;
const int LUM_VALUE_OBSTRUCT = 600;
const int HUMIDITY_AVERAGE = 62;

const String SOIL_MOIST_LOW = "dry";
const String SOIL_MOIST_MEDIUM = "wet";
const String SOIL_MOIST_HIGH = "flooded";
const String LIGHT_LEVEL_LOW = "low";
const String LIGHT_LEVEL_MEDIUM = "medium";
const String LIGHT_LEVEL_HIGH = "high";
const String WATER_AMOUNT_NONE = "cover your plants or bring them indoors";
const String WATER_AMOUNT_LOW = "don't water";
const String WATER_AMOUNT_MEDIUM = "water half an inch per square foot";
const String WATER_AMOUNT_HIGH = "water an inch per square foot";
const String SHADE_AMOUNT_CLOSE = "close roof/blinds";
const String SHADE_AMOUNT_OPEN = "open roof/blinds";

DHT temp_humid_sensor(TEMP_HUMID_PIN, DHTTYPE);

int raw_soil_moist_value = 0;
int raw_photo_value = 0;
int raw_motion_value = 0;

int tempc = 0;
int tempf = 0;
int humidity = 0;
String soil_moisture_level = SOIL_MOIST_MEDIUM;
float lum_value_percent = 50;
String light_level = LIGHT_LEVEL_MEDIUM;
bool motion_sensor_obstructed = false;

String day0_water_amount = WATER_AMOUNT_LOW;
String day0_shade_amount = SHADE_AMOUNT_OPEN;
String day1_water_amount = WATER_AMOUNT_LOW;
String day1_shade_amount = SHADE_AMOUNT_OPEN;
String day2_water_amount = WATER_AMOUNT_LOW;
String day2_shade_amount = SHADE_AMOUNT_OPEN;

String plant_name = "(unknown)";
String plant_name_previous = "(unknown)";
String plant_optimal_sun = "...";  // optimal_sun
String plant_optimal_water = "...";  // watering
String plant_water_preference = "";
String plant_image_url = "";

int weather_update_count = 0;

/*
 * https://openweathermap.org/weather-data
 * Relative Humidity Percentage (100% means air is saturated with water and rain is likely)
 * Precipitation for those three hours in millimeters (height, infinite area)
 * Cloudiness Percentage (100% means maximum cloud cover)
 */
float day0_humidity = 0;
float day0_rainfall = 0;
float day0_cloudcover = 0;
float day1_humidity = 0;
float day1_rainfall = 0;
float day1_cloudcover = 0;
float day2_humidity = 0;
float day2_rainfall = 0;
float day2_cloudcover = 0;

/* WiFi Authentication */
const char* ssid = "Public Safety Section 9";
const char* password = "12121212";

String JSON_URL = "https://api.myjson.com/bins/1e6vy6";
String OPEN_WEATHER_MAP_URL = "https://api.openweathermap.org/data/2.5/forecast?q=07012&appid=REPLACEWITHYOURAPIKEY";

DynamicJsonBuffer json_buffer;

/*
 * DFRobot Capacitive Soil Moisture Sensor - SEN0193
 * https://www.dfrobot.com/wiki/index.php/Capacitive_Soil_Moisture_Sensor_SKU:SEN0193
 */
void get_soil_moist() {
    raw_soil_moist_value = analogRead(SOIL_MOIST_PIN);
    if (raw_soil_moist_value > (MOISTURE_VALUE_WATER + (MOISTURE_VALUE_AIR - MOISTURE_VALUE_WATER)*2/3)) {
        soil_moisture_level = SOIL_MOIST_LOW;
    } else if (raw_soil_moist_value < (MOISTURE_VALUE_WATER + (MOISTURE_VALUE_AIR - MOISTURE_VALUE_WATER)/3)) {
        soil_moisture_level = SOIL_MOIST_HIGH;
    } else {
        soil_moisture_level = SOIL_MOIST_MEDIUM;
    }
}

/*
 * DHT11 Temperature and Humidity Sensor
 * https://learn.adafruit.com/dht/using-a-dhtxx-sensor
 */
void get_temp_humid() {
    tempc = temp_humid_sensor.readTemperature();
    tempf = temp_humid_sensor.readTemperature(true);
    humidity = temp_humid_sensor.readHumidity();
}

/*
 * LDR Sensor Modules
 * https://www.instructables.com/id/LDR-Sensor-Module-Users-Manual-V10/
 */
void get_photo_dark_value() {
    raw_photo_value = analogRead(PHOTO_DARK_PIN);
    if (raw_photo_value < LUM_VALUE_BRIGHT) {
        lum_value_percent = 0;
    } else if  (raw_photo_value > LUM_VALUE_DARK) {
        lum_value_percent = 100;
    } else {
        lum_value_percent = 100*(raw_photo_value - LUM_VALUE_BRIGHT)/(LUM_VALUE_DARK - LUM_VALUE_BRIGHT);
    }
    if (raw_photo_value > (LUM_VALUE_BRIGHT + (LUM_VALUE_DARK - LUM_VALUE_BRIGHT)*2/3)) {
        light_level = LIGHT_LEVEL_LOW;
    } else if (raw_photo_value < (LUM_VALUE_BRIGHT + (LUM_VALUE_DARK - LUM_VALUE_BRIGHT)/3)) {
        light_level = LIGHT_LEVEL_HIGH;
    } else {
        light_level = LIGHT_LEVEL_MEDIUM;
    }
}
void get_motion_dark_value() {
    raw_motion_value = analogRead(MOTION_DARK_PIN);
    if (raw_motion_value > LUM_VALUE_OBSTRUCT) {
        motion_sensor_obstructed = true;
    }
}

void update_schedule() {
    /* Shade Amount depends on current photosensor reading and cloud cover forecast, what a bigger weight placed on the forecast for the current day */
    if (plant_optimal_sun.equals("Full Sun(at least 6 hours a day)")) {
        day0_shade_amount = SHADE_AMOUNT_OPEN;
        day1_shade_amount = SHADE_AMOUNT_OPEN;
        day2_shade_amount = SHADE_AMOUNT_OPEN;
    } else if (plant_optimal_sun.equals("Part Sun")) {
        if ((2*lum_value_percent + day0_cloudcover)/3 < 66) {
            day0_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day0_shade_amount = SHADE_AMOUNT_OPEN;
        }
        if (day1_cloudcover < 66) {
            day1_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day1_shade_amount = SHADE_AMOUNT_OPEN;
        }
        if (day2_cloudcover < 66) {
            day2_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day2_shade_amount = SHADE_AMOUNT_OPEN;
        }
    } else {
        if ((2*lum_value_percent + day0_cloudcover)/3 < 33) {
            day0_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day0_shade_amount = SHADE_AMOUNT_OPEN;
        }
        if (day1_cloudcover < 33) {
            day1_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day1_shade_amount = SHADE_AMOUNT_OPEN;
        }
        if (day2_cloudcover < 33) {
            day2_shade_amount = SHADE_AMOUNT_CLOSE;
        } else {
            day2_shade_amount = SHADE_AMOUNT_OPEN;
        }
    }

    /* Water Amount depends on plant water preference, soil moisture sensor readings, and rainfall forecasts*/
    day0_water_amount = "";
    day1_water_amount = "";
    day2_water_amount = "";
    float tdr = (day0_rainfall + day1_rainfall + day2_rainfall)*0.0393701;  // total and convert from mm to inches
    if (plant_water_preference.equals("2inches")) {
        if (tdr > 2 || (tdr > 1 && soil_moisture_level.equals("wet")) || soil_moisture_level.equals("flooded")) {
            day0_water_amount = WATER_AMOUNT_LOW;
            if (day2_rainfall*0.0393701 < 1) {
                day1_water_amount = WATER_AMOUNT_LOW;
                day2_water_amount = WATER_AMOUNT_MEDIUM;
            } else {
                day1_water_amount = WATER_AMOUNT_MEDIUM;
                day2_water_amount = WATER_AMOUNT_LOW;
            }
        } else if ((tdr > 1 && soil_moisture_level.equals("dry")) || soil_moisture_level.equals("wet")) {
            day0_water_amount = WATER_AMOUNT_MEDIUM;
            day1_water_amount = WATER_AMOUNT_MEDIUM;
            day2_water_amount = WATER_AMOUNT_MEDIUM;
        } else {
            day0_water_amount = WATER_AMOUNT_HIGH;
            day1_water_amount = WATER_AMOUNT_MEDIUM;
            day2_water_amount = WATER_AMOUNT_MEDIUM;
        }
    } else if (plant_water_preference.equals("sparse")) {
        if (tdr > 2 || (tdr > 1 && soil_moisture_level.equals("wet")) || soil_moisture_level.equals("flooded")) {
            day0_water_amount = WATER_AMOUNT_NONE;
            day1_water_amount = WATER_AMOUNT_NONE;
            day2_water_amount = WATER_AMOUNT_LOW;
        } else if ((tdr > 1 && soil_moisture_level.equals("dry")) || soil_moisture_level.equals("wet")) {
            day0_water_amount = WATER_AMOUNT_NONE;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_MEDIUM;
        } else {
            day0_water_amount = WATER_AMOUNT_LOW;
            day1_water_amount = WATER_AMOUNT_MEDIUM;
            day2_water_amount = WATER_AMOUNT_LOW;
        }
    } else if (plant_water_preference.equals("welldrained")) {
        if (tdr > 2 || (tdr > 1 && soil_moisture_level.equals("wet")) || soil_moisture_level.equals("flooded")) {
            day0_water_amount = WATER_AMOUNT_LOW;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_LOW;
        } else if ((tdr > 1 && soil_moisture_level.equals("dry")) || soil_moisture_level.equals("wet")) {
            day0_water_amount = WATER_AMOUNT_MEDIUM;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_LOW;
        } else {
            day0_water_amount = WATER_AMOUNT_MEDIUM;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_MEDIUM;
        }
    } else {
        if (tdr > 2 || (tdr > 1 && soil_moisture_level.equals("wet")) || soil_moisture_level.equals("flooded")) {
            day0_water_amount = WATER_AMOUNT_LOW;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_LOW;
        } else if ((tdr > 1 && soil_moisture_level.equals("dry")) || soil_moisture_level.equals("wet")) {
            day0_water_amount = WATER_AMOUNT_MEDIUM;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_LOW;
        } else {
            day0_water_amount = WATER_AMOUNT_HIGH;
            day1_water_amount = WATER_AMOUNT_LOW;
            day2_water_amount = WATER_AMOUNT_LOW;
        }
    }
}

void json_put() {
    String put_data = "";

    put_data = put_data + "{\"raw\":{";
    put_data = put_data + "\"raw_soil_moist_value\":";
    put_data = put_data + raw_soil_moist_value;
    put_data = put_data + ",\"raw_photo_value\":";
    put_data = put_data + raw_photo_value;
    put_data = put_data + ",\"raw_motion_value\":";
    put_data = put_data + raw_motion_value;

    put_data = put_data + "},\"reading\":{";
    put_data = put_data + "\"tempc\":";
    put_data = put_data + tempc;
    put_data = put_data + ",\"tempf\":";
    put_data = put_data + tempf;
    put_data = put_data + ",\"humidity\":";
    put_data = put_data + humidity;
    put_data = put_data + ",\"soil_moisture_level\":\"";
    put_data = put_data + soil_moisture_level;
    put_data = put_data + "\",\"light_level\":\"";
    put_data = put_data + light_level;
    put_data = put_data + "\",\"motion_sensor_obstructed\":";
    if (motion_sensor_obstructed == true) {
        put_data = put_data + "true";
    } else {
        put_data = put_data + "false";
    }

    put_data = put_data + "},\"schedule\":{";
    put_data = put_data + "\"day0\":{";
    put_data = put_data + "\"day0_water_amount\":\"";
    put_data = put_data + day0_water_amount;
    put_data = put_data + "\",\"day0_shade_amount\":\"";
    put_data = put_data + day0_shade_amount;
    put_data = put_data + "\"},\"day1\":{";
    put_data = put_data + "\"day1_water_amount\":\"";
    put_data = put_data + day1_water_amount;
    put_data = put_data + "\",\"day1_shade_amount\":\"";
    put_data = put_data + day1_shade_amount;
    put_data = put_data + "\"},\"day2\":{";
    put_data = put_data + "\"day2_water_amount\":\"";
    put_data = put_data + day2_water_amount;
    put_data = put_data + "\",\"day2_shade_amount\":\"";
    put_data = put_data + day2_shade_amount;

    put_data = put_data + "\"}},\"plant\":{";
    put_data = put_data + "\"plant_name\":\"";
    put_data = put_data + plant_name;
    put_data = put_data + "\",\"plant_optimal_sun\":\"";
    put_data = put_data + plant_optimal_sun;
    put_data = put_data + "\",\"plant_optimal_water\":\"";
    put_data = put_data + plant_optimal_water;
    put_data = put_data + "\"}}";

    // Serial.println(put_data);

    HTTPClient http;
    http.begin(JSON_URL);
    http.addHeader("Content-Type", "application/json");

    int http_response_code = http.sendRequest("PUT", put_data);

    if (http_response_code > 0) {
        String response = http.getString();
        Serial.print("PUT request response code: ");
        Serial.println(http_response_code);
    } else {
        Serial.print("ERROR | PUT request failed: ");
        Serial.println(http_response_code);
    }

    http.end();
}

void json_get_fields() {
    HTTPClient http;
    http.begin(JSON_URL);

    int http_response_code = http.GET();

    if (http_response_code > 0) {
        String response = http.getString();
        Serial.print("garden data GET request response code: ");
        Serial.println(http_response_code);

        JsonObject& root = json_buffer.parseObject(response);
        if (!root.success()) {
            Serial.println("ERROR | garden data JSON parsing failed");
        }

        JsonObject& reading = root["reading"];
        motion_sensor_obstructed = reading["motion_sensor_obstructed"].as<bool>();
        JsonObject& plant = root["plant"];
        plant_name = plant["plant_name"].as<String>();

    } else {
        Serial.print("ERROR | garden data GET request failed: ");
        Serial.println(http_response_code);
    }

    http.end();
}

void json_get_weather_info() {
    HTTPClient http;
    http.begin(OPEN_WEATHER_MAP_URL);

    int http_response_code = http.GET();

    if (http_response_code > 0) {
        String response = http.getString();
        Serial.print("weather forecast GET request response code: ");
        Serial.println(http_response_code);

        JsonObject& root = json_buffer.parseObject(response);
        if (!root.success()) {
            Serial.println("ERROR | weather forecast JSON parsing failed");
        }

        day0_humidity = 0;
        day0_rainfall = 0;
        day0_cloudcover = 0;
        day1_humidity = 0;
        day1_rainfall = 0;
        day1_cloudcover = 0;
        day2_humidity = 0;
        day2_rainfall = 0;
        day2_cloudcover = 0;

        JsonArray& list = root["list"];

        for (int i=0; i<8; i++) {
            JsonObject& three_hour_forecast = list[i];
            JsonObject& main = three_hour_forecast["main"];

            day0_humidity = day0_humidity + main["humidity"].as<int>();
            if (three_hour_forecast.containsKey("rain")) {
                JsonObject& rain = three_hour_forecast["rain"];
                if (rain.containsKey("3h")) {
                    day0_rainfall = day0_rainfall + three_hour_forecast["rain"]["3h"].as<int>();
                }
            }
            day0_cloudcover = day0_cloudcover + three_hour_forecast["clouds"]["all"].as<int>();
        }
        for (int i=8; i<16; i++) {
            JsonObject& three_hour_forecast = list[i];
            JsonObject& main = three_hour_forecast["main"];

            day1_humidity = day1_humidity + main["humidity"].as<int>();
            if (three_hour_forecast.containsKey("rain")) {
                JsonObject& rain = three_hour_forecast["rain"];
                if (rain.containsKey("3h")) {
                    day1_rainfall = day1_rainfall + three_hour_forecast["rain"]["3h"].as<int>();
                }
            }
            day1_cloudcover = day1_cloudcover + three_hour_forecast["clouds"]["all"].as<int>();
        }
        for (int i=16; i<24; i++) {
            JsonObject& three_hour_forecast = list[i];
            JsonObject& main = three_hour_forecast["main"];

            day2_humidity = day2_humidity + main["humidity"].as<int>();
            if (three_hour_forecast.containsKey("rain")) {
                JsonObject& rain = three_hour_forecast["rain"];
                if (rain.containsKey("3h")) {
                    day2_rainfall = day2_rainfall + three_hour_forecast["rain"]["3h"].as<int>();
                }
            }
            day2_cloudcover = day2_cloudcover + three_hour_forecast["clouds"]["all"].as<int>();
        }

    } else {
        Serial.print("ERROR | weather forecast GET request failed: ");
        Serial.println(http_response_code);
    }

    http.end();

    day0_humidity = day0_humidity/8;
    day0_cloudcover = day0_cloudcover/8;
    day1_humidity = day1_humidity/8;
    day1_cloudcover = day1_cloudcover/8;
    day2_humidity = day2_humidity/8;
    day2_cloudcover = day2_cloudcover/8;

    // Serial.print("Day 0 Humidity Average: ");
    // Serial.println(day0_humidity);
    // Serial.print("Day 0 Rainfall Total: ");
    // Serial.println(day0_rainfall);
    // Serial.print("Day 0 Cloud Cover Average: ");
    // Serial.println(day0_cloudcover);
}

/*
 * https://github.com/damwhit/harvest_helper/blob/master/data/garden_vegetables.csv
 * API call would crash the ESP32S since the array is 110+ KB, so use trimmed local version
 * 1 inch of water means 1 inch of water per square foot in gardener-speak
 */
void json_get_plant_info() {
    if (!plant_name_previous.equals(plant_name)) {
        String plant_database = "{\"Basil\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Make sure soil remains moist but is well-drained.\",\"image_url\":\"harvest_helper_production\/02_basil\"},\"Lettuce\":{\"optimal_sun\":\"Part Sun\",\"optimal_water\":\"welldrained\",\"watering\":\"Make sure soil remains moist but is well-drained. Lettuce will tell you when it needs water. Just look at it. If the leaves are wilting, sprinkle them anytime\u2014even in the heat of the day\u2014to cool them off and slow down the transpiration rate.\",\"image_url\":\"harvest_helper_production\/03_lettuce\"},\"Carrots\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water at least one inch per week.\",\"image_url\":\"harvest_helper_production\/04_carrot\"},\"Broccoli\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Provide consistent soil moisture with regular watering, especially in drought conditions. Some varieties of broccoli are heat tolerant, but all need moisture. Do not get developing heads wet when watering.\",\"image_url\":\"harvest_helper_production\/08_Broccoli\"},\"Parsnips\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"sparse\",\"watering\":\"Water during the summer if rainfall is less than 1 inch per week.\",\"image_url\":\"harvest_helper_production\/22_parsnips\"},\"Cabbage\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Keep soil moist with mulch and water 2 inches per week. Mulch thickly to retain regulate soil temperature.\",\"image_url\":\"harvest_helper_production\/10_cabbage\"},\"Chives\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"It is important to give chives consistent watering throughout the growing season for high yields. Moisten the soil thoroughly when watering. Use mulch to conserve moisture and keep the weeds down. Minimal care is needed for fully grown plants. After the flowers bloom, be sure to remove them so that the seeds aren\'t spread throughout your garden. Remember to divide the plants every 3 to 4 years in the spring. Chives are much more productive if divided regularly. Allow divided plants to grow for several weeks before harvesting.\",\"image_url\":\"harvest_helper_production\/13_chives\"},\"Sweet Corn\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Water well at planting time. Soil must be well drained and able to keep consistent moisture. In dry conditions, be sure to keep corn well watered due to its shallow roots. Water at a rate of 5 gallons per sq yard. Mulch helps reduce evaporation.\",\"image_url\":\"harvest_helper_production\/16_sweet_corn\"},\"Eggplant\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water well to where the soil is moist like a rung out sponge.\",\"image_url\":\"harvest_helper_production\/17_eggplant\"},\"Onion\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"sparse\",\"watering\":\"Generally, onions do not need consistent watering if mulch is used. About one inch of water per week (including rain water) is sufficient. If you want sweeter onions, water more. Onions will look healthy even if they are bone dry, be sure to water during drought conditions.\",\"image_url\":\"harvest_helper_production\/18_onion\"},\"Garlic\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Water every 3 to 5 days during bulbing (mid-May through June).\",\"image_url\":\"harvest_helper_production\/19_garlic\"},\"Cantaloupe\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"While melon plants are growing, blooming, and setting fruit, they need 1 to 2 inches of water per week. Water in the morning, and try to avoid wetting the leaves. Reduce watering once fruit are growing. Dry weather produces the sweetest melon. If you\u2019ve had an exceptional amount of rainfall during the ripening stage, this could cause the bland fruit.\",\"image_url\":\"harvest_helper_production\/21_cantaloupe\"},\"Peas\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"sparse\",\"watering\":\"Water sparsely unless the plants are wilting. Do not let plants dry out, or no pods will be produced.\",\"image_url\":\"harvest_helper_production\/23_peas\"},\"Potatoes\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Potatoes need consistent moisture, so water regularly when tubers start to form.\",\"image_url\":\"harvest_helper_production\/25_potatoes\"},\"Pumpkins\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Pumpkins are very thirsty plants and need lots of water. Water one inch per week. Water deeply, especially during fruit set. When watering: Try to keep foliage and fruit dry unless it\u2019s a sunny day. Dampness will make rot more likely.\",\"image_url\":\"harvest_helper_production\/26_pumpkin\"},\"Spinach\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Water regularly.\",\"image_url\":\"harvest_helper_production\/29_spinach\"},\"Turnips\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water at a rate of 1 inch per week to prevent the roots from becoming tough and bitter.\",\"image_url\":\"harvest_helper_production\/32_turnip\"},\"Chard\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"welldrained\",\"watering\":\"Water the plants evenly to help them grow better. Water often during dry spells in the summer.\",\"image_url\":\"harvest_helper_production\/31_swiss_chard\"},\"Thyme\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water normally and remember to trim the plants.\",\"image_url\":\"harvest_helper_production\/35_thyme\"},\"Oregano\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"sparse\",\"watering\":\"Oregano doesn\'t need quite as much water as most herbs. As the amount of watering depends on many variables, just water when the soil feels dry to the touch. Remember that it\'s better to water thoroughly and less often. If you have a container, water until the water comes out of the drainage holes in the bottom of the container.\",\"image_url\":\"harvest_helper_production\/36_oregano\"},\"Rosemary\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Water the plants evenly throughout the growing season.\",\"image_url\":\"harvest_helper_production\/37_rosemary\"},\"Okra\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Keep the plants well watered throughout the summer months; 1 inch of water per week is ideal, but use more if you are in a hot, arid region.\",\"image_url\":\"harvest_helper_production\/43_okra\"},\"Sweet Potato\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Remember to keep the potatoes watered. Deep watering in hot, dry periods will help to increase yields, although if you are planning to store some of the potatoes, do not give the plants extra water late in the season.\",\"image_url\":\"harvest_helper_production\/44_sweet_potato\"},\"Parsley\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"welldrained\",\"watering\":\"Throughout the summer, be sure to water the plants evenly.\",\"image_url\":\"harvest_helper_production\/39_parsley\"},\"Sage\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Be sure to water the young plants regularly until they are fully grown so that they don\'t dry out.\",\"image_url\":\"harvest_helper_production\/41_sage\"},\"Cauliflower\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Cauliflower requires consistent soil moisture. They need 1 to 1.5 inches of water each week; with normal rainfall, this usually requires supplement watering.\",\"image_url\":\"harvest_helper_production\/11_cauliflower\"},\"Dill\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water the plants freely during the growing season.\",\"image_url\":\"harvest_helper_production\/45_dill\"},\"Collard Greens\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Water the plants regularly but be sure not to overwater them. Mulch the soil heavily after the first hard freeze; the plants may continue to produce leaves throughout the winter.\",\"image_url\":\"harvest_helper_production\/14_collard_greens\"},\"Beets\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"2inches\",\"watering\":\"Beets need to maintain plenty of moisture.\",\"image_url\":\"harvest_helper_production\/07_beet\"},\"Bell Peppers\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Soil should be well-drained, but maintain adequate moisture either with mulch or plastic covering. Water one to two inches per week, but remember peppers are extremely heat sensitive. If you live in a warm or desert climate, watering everyday may be necessary.\",\"image_url\":\"harvest_helper_production\/24_bell_peppers\"},\"Cilantro\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"1inch\",\"watering\":\"Water the seedlings regularly throughout the growing season. They require about 1 inch of water per week for best growth. Once the plants are established, they do not need as much water per week. Keep them moist, but be careful not to overwater them.\",\"image_url\":\"harvest_helper_production\/38_cilantro\"},\"Mint\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"welldrained\",\"watering\":\"Be sure to water regularly to keep the soil evenly moist.\",\"image_url\":\"harvest_helper_production\/40_mint\"},\"Kale\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Water the plants regularly but be sure not to overwater them.\",\"image_url\":\"harvest_helper_production\/20_kale\"},\"Celery\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"2inches\",\"watering\":\"Celery is a heavy feeder. It also requires lots of water. Make sure to provide plenty of water during the entire growing season, especially during hot, dry weather. If celery does not get enough water, the stalks will be dry, and small. Fertilize regularly. Add mulch as needed, to help retain soil moisture and add nutrients. Tie growing celery stalks together to keep them from sprawling.\",\"image_url\":\"harvest_helper_production\/12_celery\"},\"Cucumbers\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"When seedlings emerge, begin to water frequently, and increase to a gallon per week after fruit forms. Water consistently; put your finger in the soil and when it is dry past the first joint of your finger, it is time to water. Inconsistent watering leads to bitter-tasting fruit. Water slowly in the morning or early afternoon, avoding the leaves.\",\"image_url\":\"harvest_helper_production\/15_cucumber\"},\"Tomatoes\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Make sure soil remains moist but is well-drained.\",\"image_url\":\"harvest_helper_production\/01_tomato\"},\"Beans\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Mulch soil to retain moisture; make sure that it is well-drained. Water regularly, from start of pod to set. Water on sunny days so foliage will not remain soaked.\",\"image_url\":\"harvest_helper_production\/06_beans\"},\"Brussels Sprouts\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Mulch to retain moisture and keep the soil temperature cool. Do not cultivate, roots are shallow and susceptible to damage. Protect the plant by mulching with straw or providing a cover if you plan to harvest into the winter.\",\"image_url\":\"harvest_helper_production\/09_brussels_sprouts\"},\"Radishes\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"welldrained\",\"watering\":\"Radishes require well-drained soil with consistent moisture. Keep soil evenly moist but not waterlogged.\",\"image_url\":\"harvest_helper_production\/27_radish\"},\"Rhubarb\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"1inch\",\"watering\":\"Water your plant well. It needs sufficient moisture during the summer.\",\"image_url\":\"harvest_helper_production\/28_rhubarb\"},\"Asparagus\":{\"optimal_sun\":\"Part Sun\",\"optimal_water\":\"1inch\",\"watering\":\"Water at least one inch per week.\",\"image_url\":\"harvest_helper_production\/05_asparagus\"},\"Summer Squash\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"For all type of squash, frequent and consistent watering is recommended. Water most diligently when fruits form and throughout their growth period. Water deeply once a week, applying at least one inch of water. Do not water shallowly; the soil needs to be moist 4 inches down.\",\"image_url\":\"harvest_helper_production\/30_summer_squash\"},\"Watermelon\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"Watering is very important from planting until fruit begins to form. While melon plants are growing, blooming, and setting fruit, they need 1 to 2 inches of water per week. Keep soil moist but not waterlogged. Water at the vine\'s base in the morning, and try to avoid wetting the leaves and avoid overhead watering. Reduce watering once fruit are growing. Dry weather produces the sweetest melon.\",\"image_url\":\"harvest_helper_production\/33_watermelon\"},\"Winter Squash\":{\"optimal_sun\":\"Full Sun(at least 6 hours a day)\",\"optimal_water\":\"2inches\",\"watering\":\"For all type of squash, frequent and consistent watering is recommended. Water most diligently when fruits form and throughout their growth period. Water deeply once a week, applying at least one inch of water. Do not water shallowly; the soil needs to be moist 4 inches down.\",\"image_url\":\"harvest_helper_production\/34_winter_squash\"},\"Tarragon\":{\"optimal_sun\":\"Full-Part Sun\",\"optimal_water\":\"1inch\",\"watering\":\"Be sure to water the young plants regularly until they are fully grown so that they don\'t dry out.\",\"image_url\":\"harvest_helper_production\/42_tarragon\"}}";
        JsonObject& root = json_buffer.parseObject(plant_database);
        if (!root.success()) {
            Serial.println("ERROR | plant database JSON parsing failed");
        }

        if (root.containsKey(plant_name)) {
            JsonObject& plant = root[plant_name];
            plant_optimal_water = plant["watering"].as<String>();
            plant_optimal_sun = plant["optimal_sun"].as<String>();
            plant_water_preference = plant["water_preference"].as<String>();
            plant_image_url = plant["image_url"].as<String>();
        } else {
            plant_name = "(unknown)";
            plant_optimal_sun = "...";
            plant_optimal_water = "...";
            plant_water_preference = "";
            plant_image_url = "";
        }
        plant_name_previous = plant_name;
    }
}

void setup() {
    Serial.begin(115200);
    delay(10);

    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    temp_humid_sensor.begin();

    json_get_weather_info();

}

void loop() {
    Serial.println();
    if (WiFi.status()== WL_CONNECTED) {

        get_soil_moist();
        // Serial.print("Soil Moisture Value: ");
        // Serial.println(raw_soil_moist_value);

        get_temp_humid();
        // Serial.print("Humidity Value: ");
        // Serial.println(humidity);
        // Serial.print("Temperature (Celcius) Value: ");
        // Serial.println(tempc);
        // Serial.print("Temperature (Farenheit) Value: ");
        // Serial.println(tempf);

        get_photo_dark_value();
        // Serial.print("Photo Darkness Value: ");
        // Serial.println(raw_photo_value);

        if (weather_update_count < UPDATE_FREQ_WEATHER) {
            weather_update_count = weather_update_count + 1;
        } else {
            json_get_weather_info();
            weather_update_count = 0;
        }

        json_get_fields();
        json_get_plant_info();

        get_motion_dark_value();
        // Serial.print("Motion Darkness Value: ");
        // Serial.println(raw_motion_value);

        update_schedule();

        // user values may be lost if they POST between microcontroller's GET and PUT
        json_put();

        delay(UPDATE_FREQ_JSON);
    } else {
        Serial.println("WiFi connection error");
    }
}
