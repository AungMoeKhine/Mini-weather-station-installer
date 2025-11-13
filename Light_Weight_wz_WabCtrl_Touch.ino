#include <Arduino.h>
#include <Preferences.h>          // For storing settings persistently
#include <FS.h>                   // For file system operations
#include <SPI.h>                  // For SPI communication (TFT, Touch, SD)
#include <WiFi.h>                 // For WiFi connectivity
#include <WiFiClientSecure.h>     // For HTTPS connections
#include <HTTPClient.h>           // For making HTTP requests
#include <WiFiManager.h>          // For a user-friendly WiFi setup portal
#include <DNSServer.h>            // Dependency for WiFiManager
#include <WebServer.h>            // Dependency for WiFiManager
#include <TFT_eSPI.h>             // For controlling the TFT display
#include <XPT2046_Touchscreen.h>  // For handling touch input
#include <ArduinoJson.h>          // For parsing JSON data from APIs
#include <time.h>                 // For time and date functions
#include <SD.h>                   // For reading from the SD card
#include <vector>                 // For using dynamic arrays (std::vector)
#include <algorithm>              // For algorithms like std::sort
#include <numeric>                // For std::iota
#include <math.h>                 // For mathematical functions
#include <SunRise.h>              // For sunrise/sunset calculations
#include <MoonRise.h>             // For moonrise/moonset calculations

WebServer server(80);

// --- ISR TOUCH --- Flag to be set by the interrupt when a touch is detected
volatile bool touchInterruptTriggered = false;

// --- ISR TOUCH --- The Interrupt Service Routine. Must be fast!
void IRAM_ATTR handleTouchInterrupt() {
  touchInterruptTriggered = true;  // Set the flag to true to be handled in the main loop
}

// Define hardware configuration pins
#define TFT_BACKLIGHT_PIN 21  // Pin for controlling the TFT backlight
TFT_eSPI tft = TFT_eSPI();    // Create an instance of the TFT library

// --- BEEP FEATURE --- Buzzer pin definition
#define BUZZER_PIN 26  // Pin connected to the buzzer

// Pin definitions for the XPT2046 touchscreen controller
#define TOUCH_CS_PIN 33                         // Touchscreen Chip Select pin
#define TOUCH_IRQ_PIN 36                        // Touchscreen Interrupt Request pin
#define HSPI_MOSI_PIN 32                        // HSPI Master Out Slave In pin
#define HSPI_MISO_PIN 39                        // HSPI Master In Slave Out pin
#define HSPI_CLK_PIN 25                         // HSPI Clock pin
SPIClass touchscreenSPI(HSPI);                  // Create an SPI instance for the touchscreen
XPT2046_Touchscreen touchscreen(TOUCH_CS_PIN);  // Create an instance of the touchscreen library

// Pin definition for SD Card
#define SD_CARD_CS_PIN 5       // SD Card Chip Select pin
bool isSdCardMounted = false;  // Flag to track if the SD card is successfully mounted

// LDR and Backlight Control Definitions
#define LDR_PIN 34           // Pin connected to the Light Dependent Resistor (LDR)
#define PWM_RES 8            // PWM resolution for backlight control (8-bit = 0-255)
#define NUM_LDR_READINGS 10  // Number of LDR reaFdings to average for stable brightness

// These values will be loaded from memory after a one-time calibration routine.
struct TouchCalibrationData {
  int32_t x_min, x_max, y_min, y_max;
};
TouchCalibrationData calData;
bool isCalibrated = false;  // Flag to check if calibration has been run

// --- Digital Clock Sprite Dimensions (for partial screen update) ---
#define DC_SPRITE_WIDTH (SCREEN_WIDTH - SCREEN_HEIGHT)  // Width of the digital clock panel (320-240 = 80px)
#define DC_SPRITE_Y_POS 10                              // Top Y position of the digital clock panel
#define DC_SPRITE_HEIGHT 185                            // Height of the digital clock panel

// Define screen dimensions and layout constants
const int SCREEN_WIDTH = 320, SCREEN_HEIGHT = 240;
const int HEADER_HEIGHT = 30;                                        // Height of the top header bar
const int FORECAST_STRIP_HEIGHT = 85;                                // Height of the bottom forecast strip
const int FORECAST_STRIP_Y = SCREEN_HEIGHT - FORECAST_STRIP_HEIGHT;  // Y-position of the forecast strip
const int LEFT_PADDING = 10;                                         // General left padding for text
const int MAIN_CONTENT_Y = HEADER_HEIGHT;                            // Y-position where main content starts
const int INFO_COLUMN_X = SCREEN_WIDTH - 150;                        // X-position of the right-side info panel
const int INFO_COLUMN_Y_START = 35;                                  // Starting Y-position for info panel text
const int INFO_ROW_HEIGHT = 13;                                      // Height of each row in the info panel

// Define color constants for UI in 16-bit (565) format
#define COLOR_BACKGROUND 0x0000    // Black
#define COLOR_TEXT 0xFFFF          // White
#define COLOR_HIGH_TEMP 0xFEA0     // Orange
#define COLOR_LOW_TEMP 0x4E9C      // Light Blue
#define COLOR_FEELS_LIKE 0xFD05    // Pink/Purple
#define COLOR_INFO_LABEL 0xF8B0    // Yellow-Orange
#define COLOR_DIVIDER_LINE 0x39E7  // Dark Cyan
#define COLOR_DESC_NIGHT 0xAE73    // Light Grey-Blue for night descriptions
#define COLOR_ARROW 0x001F         // Blue

// --- REVISED Clock Page Color Constants (for Black Background) ---
#define CLOCK_COLOR_TICK 0xFFFF       // White clock ticks
#define CLOCK_COLOR_HOUR_HAND 0x07E0  // Green hour hand
#define CLOCK_COLOR_MIN_HAND 0xF81F   // Magenta minute hand
#define CLOCK_COLOR_SEC_HAND 0x001F   // Blue second hand
#define CLOCK_COLOR_NUMBER 0xFFFF     // White hour numbers
#define CLOCK_COLOR_ACCENT 0xFFE0     // Yellow accent for center pivot and other highlights

// Define text size constants
#define TEXT_SIZE_DESCRIPTION 2
#define TEXT_SIZE_MAIN_TEMP 2
#define TEXT_SIZE_FEELS 1
#define TEXT_SIZE_INFO 1
#define TEXT_SIZE_FORECAST 1

// User agent string for API requests, as required by Met.no
#define API_USER_AGENT "ESP32-Weather-Display/1.0:https://github.com/AungMoeKhine/Mini-weather-station-installer:aungmoekhine@gmail.com"

// Define network and data refresh intervals
#define MAX_NETWORK_RETRIES 10                              // Maximum number of times to retry a failed API call
#define NETWORK_RETRY_DELAY_MS 5000                         // Delay between retries
#define WEATHER_REFRESH_INTERVAL_MS (10UL * 60UL * 1000UL)  // 10 minutes for weather data

// Define paths for icon directories on the SD card
#define ICON_DIR_LARGE "/bmp64x64"
#define ICON_DIR_SMALL "/bmp32x32"
#define THERMO_ICON_PATH "/bmp64x64/thermo_small.bmp"  // Path to thermometer icon

// Define icon sizes and positions
#define THERMO_ICON_SIZE 64
#define THERMO_ICON_X 35
#define THERMO_ICON_Y 87

// Touch constants for handling input
#define TOUCH_PRESSURE_THRESHOLD 100  // Minimum pressure to register a touch
#define TOUCH_DEBOUNCE_MS 20
const int SWIPE_THRESHOLD = 60;                   // Minimum pixel distance to be considered a swipe
const unsigned long LONG_PRESS_DURATION = 15000;  // 15 seconds for long press to enter config mode
const unsigned long TOUCH_PRINT_INTERVAL = 250;   // Interval for printing touch coordinates (for debugging)
const unsigned long UI_DEBOUNCE_TIME = 300;       // Debounce time to prevent accidental double-taps on UI elements
unsigned long lastUiInteractionTime = 0;          // Tracks the time of the last UI interaction for debouncing

// --- BEEP FEATURE --- Bell icon position and size
#define BEEP_ICON_X 40  // Center X of the beep icon in its sprite
#define BEEP_ICON_Y 30  // Center Y of the beep icon in its sprite
#define BEEP_ICON_R 12  // Radius of the beep icon

// --- NON-BLOCKING BEEP FEATURE ---
bool confirmationBeepActive = false;                    // Flag for playing a short confirmation beep
unsigned long confirmationBeepStartTime = 0;            // Start time of the confirmation beep
const unsigned int CONFIRMATION_BEEP_DURATION_MS = 50;  // Duration of the confirmation beep

// Initialize preferences for storing settings
Preferences prefs;                         // Create a Preferences object to handle non-volatile storage
String latitude = "16.890174";             // Default latitude
String longitude = "96.214092";            // Default longitude
bool isNorthernHemisphere = true;          // Default hemisphere, will be set based on latitude in setup()
bool isTimeSynced = false;                 // Flag to track if network time has been successfully obtained
bool isDataValid = false;                  // Flag to track if weather data has been successfully fetched and parsed
unsigned long lastDataFetchTimestamp = 0;  // Timestamp of the last successful weather data fetch
bool useFahrenheit = false;                // Flag to toggle between Celsius and Fahrenheit
bool useMph = true;                        // Flag to toggle between mph and km/h for wind speed
int pressureUnitState = 0;                 // 0=hPa, 1=inHg, 2=mmHg
bool invertDisplay = false;                // Flag for inverting display colors (dark/light mode)
bool use24HourFormat = false;

// Global variable to hold the main system timezone string.
String system_tz_string;
SemaphoreHandle_t timezoneMutex;

// --- BEEP FEATURE --- Global state variables for hourly chime
hw_timer_t* timer = NULL;                      // Hardware timer for precise beep timing
bool hourlyBeepEnabled = true;                 // Flag to enable/disable the hourly chime
volatile bool hourBeepActive = false;          // Flag indicating the hourly chime is currently active
volatile int beepCount = 0;                    // Counter for the number of beeps played
volatile int targetBeeps = 0;                  // The number of beeps to play for the current hour
const unsigned int beepInterval = 150;         // Duration of a single beep in milliseconds
const unsigned int pauseInterval = 250;        // Duration of the pause between beeps
volatile bool buzzerOn = false;                // Current state of the buzzer
volatile unsigned long lastInterruptTime = 0;  // Timestamp of the last timer interrupt
volatile bool isBlinking = false;              // Flag to control blinking of the bell icon
volatile bool showBellTouchCircle = false;     // Flag to show a visual indicator when touching the bell icon
int previousHour = -1;                         // Tracks the last hour that chimed to prevent re-triggering

// --- WORLD CLOCK FEATURE ---
// Structure to hold city data for the world clock page
struct WorldCity {
  const char* cityName;
  const char* cityCode;
  const char* flagBmpPath;
  const char* iana_timezone;   // Modern IANA name (e.g., "America/New_York")
  const char* posix_timezone;  // POSIX string required by ESP32 (e.g., "EST5EDT,M3.2.0,M11.1.0")
  const char* utcOffset;
  bool isNorthern;  // True if the city is in the Northern Hemisphere
  float lat;
  float lon;
};

// List of world cities, sorted by UTC offset (West to East) and then alphabetically.
const WorldCity cities[] = {
  // UTC-11:00
  { "Midway Atoll", "MDY", "/flags/us.bmp", "Pacific/Midway", "SST11", "-11:00", true, 28.2078, -177.3789 },
  { "Pago Pago", "PPG", "/flags/as.bmp", "Pacific/Pago_Pago", "SST11", "-11:00", false, -14.2761, -170.7024 },
  // UTC-10:00
  { "Honolulu", "HNL", "/flags/us.bmp", "Pacific/Honolulu", "HST10", "-10:00", true, 21.3069, -157.8583 },
  // UTC-09:00
  { "Anchorage", "ANC", "/flags/us.bmp", "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0", "-09:00", true, 61.2181, -149.9003 },
  // UTC-08:00
  { "Las Vegas", "LAS", "/flags/us.bmp", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "-08:00", true, 36.1699, -115.1398 },
  { "Los Angeles", "LAX", "/flags/us.bmp", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "-08:00", true, 34.0522, -118.2437 },
  { "San Francisco", "SFO", "/flags/us.bmp", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "-08:00", true, 37.7749, -122.4194 },
  { "Seattle", "SEA", "/flags/us.bmp", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0", "-08:00", true, 47.6062, -122.3321 },
  { "Vancouver", "YVR", "/flags/ca.bmp", "America/Vancouver", "PST8PDT,M3.2.0,M11.1.0", "-08:00", true, 49.2827, -123.1207 },
  // UTC-07:00
  { "Calgary", "YYC", "/flags/ca.bmp", "America/Edmonton", "MST7MDT,M3.2.0,M11.1.0", "-07:00", true, 51.0447, -114.0719 },
  { "Denver", "DEN", "/flags/us.bmp", "America/Denver", "MST7MDT,M3.2.0,M11.1.0", "-07:00", true, 39.7392, -104.9903 },
  { "Edmonton", "YEG", "/flags/ca.bmp", "America/Edmonton", "MST7MDT,M3.2.0,M11.1.0", "-07:00", true, 53.5461, -113.4938 },
  { "Phoenix", "PHX", "/flags/us.bmp", "America/Phoenix", "MST7", "-07:00", true, 33.4484, -112.0740 },
  { "Salt Lake City", "SLC", "/flags/us.bmp", "America/Denver", "MST7MDT,M3.2.0,M11.1.0", "-07:00", true, 40.7608, -111.8910 },
  // UTC-06:00
  { "Chicago", "ORD", "/flags/us.bmp", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 41.8781, -87.6298 },
  { "Dallas", "DFW", "/flags/us.bmp", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 32.7767, -96.7970 },
  { "Guatemala City", "GUA", "/flags/gt.bmp", "America/Guatemala", "CST6", "-06:00", true, 14.6349, -90.5069 },
  { "Houston", "IAH", "/flags/us.bmp", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 29.7604, -95.3698 },
  { "Managua", "MGA", "/flags/ni.bmp", "America/Managua", "CST6", "-06:00", true, 12.1150, -86.2362 },
  { "Mexico City", "MEX", "/flags/mx.bmp", "America/Mexico_City", "CST6", "-06:00", true, 19.4326, -99.1332 },
  { "Minneapolis", "MSP", "/flags/us.bmp", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 44.9778, -93.2650 },
  { "New Orleans", "MSY", "/flags/us.bmp", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 29.9511, -90.0715 },
  { "San Salvador", "SAL", "/flags/sv.bmp", "America/El_Salvador", "CST6", "-06:00", true, 13.6929, -89.2182 },
  { "Tegucigalpa", "TGU", "/flags/hn.bmp", "America/Tegucigalpa", "CST6", "-06:00", true, 14.0723, -87.1921 },
  { "Winnipeg", "YWG", "/flags/ca.bmp", "America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0", "-06:00", true, 49.8951, -97.1384 },
  // UTC-05:00
  { "Atlanta", "ATL", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 33.7490, -84.3880 },
  { "Bogota", "BOG", "/flags/co.bmp", "America/Bogota", "<-05>5", "-05:00", true, 4.7110, -74.0721 },
  { "Boston", "BOS", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 42.3601, -71.0589 },
  { "Detroit", "DTW", "/flags/us.bmp", "America/Detroit", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 42.3314, -83.0458 },
  { "Havana", "HAV", "/flags/cu.bmp", "America/Havana", "CST5CDT,M3.2.0/0,M11.1.0/1", "-05:00", true, 23.1136, -82.3666 },
  { "Indianapolis", "IND", "/flags/us.bmp", "America/Indiana/Indianapolis", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 39.7684, -86.1581 },
  { "Kingston", "KIN", "/flags/jm.bmp", "America/Jamaica", "EST5", "-05:00", true, 17.9836, -76.8036 },
  { "Miami", "MIA", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 25.7617, -80.1918 },
  { "Montreal", "YUL", "/flags/ca.bmp", "America/Toronto", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 45.5017, -73.5673 },
  { "Nassau", "NAS", "/flags/bs.bmp", "America/Nassau", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 25.0480, -77.3554 },
  { "New York", "NYC", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 40.7128, -74.0060 },
  { "Ottawa", "YOW", "/flags/ca.bmp", "America/Toronto", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 45.4215, -75.6972 },
  { "Philadelphia", "PHL", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 39.9526, -75.1652 },
  { "Toronto", "YYZ", "/flags/ca.bmp", "America/Toronto", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 43.6532, -79.3832 },
  { "Washington DC", "DCA", "/flags/us.bmp", "America/New_York", "EST5EDT,M3.2.0,M11.1.0", "-05:00", true, 38.9072, -77.0369 },
  // UTC-04:00
  { "Asuncion", "ASU", "/flags/py.bmp", "America/Asuncion", "PYT4PYST,M10.1.0/0,M3.4.0/0", "-04:00", false, -25.2637, -57.5759 },
  { "Caracas", "CCS", "/flags/ve.bmp", "America/Caracas", "<-04>4", "-04:00", true, 10.4806, -66.9036 },
  { "Halifax", "YHZ", "/flags/ca.bmp", "America/Halifax", "AST4ADT,M3.2.0,M11.1.0", "-04:00", true, 44.6488, -63.5752 },
  { "La Paz", "LPB", "/flags/bo.bmp", "America/La_Paz", "<-04>4", "-04:00", false, -16.4897, -68.1193 },
  { "San Juan", "SJU", "/flags/pr.bmp", "America/Puerto_Rico", "AST4", "-04:00", true, 18.4655, -66.1057 },
  { "Santiago", "SCL", "/flags/cl.bmp", "America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24", "-04:00", false, -33.4489, -70.6693 },
  { "Santo Domingo", "SDQ", "/flags/do.bmp", "America/Santo_Domingo", "AST4", "-04:00", true, 18.4861, -69.9312 },
  // UTC-03:30
  { "St. John's", "YYT", "/flags/ca.bmp", "America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0", "-03:30", true, 47.5615, -52.7126 },
  // UTC-03:00
  { "Brasilia", "BSB", "/flags/br.bmp", "America/Sao_Paulo", "<-03>3", "-03:00", false, -15.8267, -47.9218 },
  { "Buenos Aires", "EZE", "/flags/ar.bmp", "America/Argentina/Buenos_Aires", "<-03>3", "-03:00", false, -34.6037, -58.3816 },
  { "Montevideo", "MVD", "/flags/uy.bmp", "America/Montevideo", "<-03>3", "-03:00", false, -34.9011, -56.1645 },
  { "Rio de Janeiro", "GIG", "/flags/br.bmp", "America/Sao_Paulo", "<-03>3", "-03:00", false, -22.9068, -43.1729 },
  { "Sao Paulo", "GRU", "/flags/br.bmp", "America/Sao_Paulo", "<-03>3", "-03:00", false, -23.5505, -46.6333 },
  // UTC-02:00
  { "South Georgia", "SGS", "/flags/gs.bmp", "Atlantic/South_Georgia", "<-02>2", "-02:00", false, -54.2811, -36.5090 },
  // UTC-01:00
  { "Azores", "PDL", "/flags/pt.bmp", "Atlantic/Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1", "-01:00", true, 37.7412, -25.6756 },
  // UTC+00:00
  { "Accra", "ACC", "/flags/gh.bmp", "Africa/Accra", "GMT0", "+00:00", true, 5.6037, -0.1870 },
  { "Lisbon", "LIS", "/flags/pt.bmp", "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0", "+00:00", true, 38.7223, -9.1393 },
  { "London", "LON", "/flags/gb.bmp", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0", "+00:00", true, 51.5072, -0.1276 },
  { "Reykjavik", "KEF", "/flags/is.bmp", "Atlantic/Reykjavik", "GMT0", "+00:00", true, 64.1466, -21.9426 },
  // UTC+01:00
  { "Algiers", "ALG", "/flags/dz.bmp", "Africa/Algiers", "CET-1", "+01:00", true, 36.7772, 3.0586 },
  { "Amsterdam", "AMS", "/flags/nl.bmp", "Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 52.3676, 4.9041 },
  { "Barcelona", "BCN", "/flags/es.bmp", "Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 41.3851, 2.1734 },
  { "Belgrade", "BEG", "/flags/rs.bmp", "Europe/Belgrade", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 44.7866, 20.4489 },
  { "Berlin", "BER", "/flags/de.bmp", "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 52.5200, 13.4050 },
  { "Brussels", "BRU", "/flags/be.bmp", "Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 50.8503, 4.3517 },
  { "Casablanca", "CMN", "/flags/ma.bmp", "Africa/Casablanca", "<+01>-1", "+01:00", true, 33.5731, -7.5898 },
  { "Copenhagen", "CPH", "/flags/dk.bmp", "Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 55.6761, 12.5683 },
  { "Frankfurt", "FRA", "/flags/de.bmp", "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 50.1109, 8.6821 },
  { "Madrid", "MAD", "/flags/es.bmp", "Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 40.4168, -3.7038 },
  { "Oslo", "OSL", "/flags/no.bmp", "Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 59.9139, 10.7522 },
  { "Paris", "CDG", "/flags/fr.bmp", "Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 48.8566, 2.3522 },
  { "Prague", "PRG", "/flags/cz.bmp", "Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 50.0755, 14.4378 },
  { "Rome", "FCO", "/flags/it.bmp", "Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 41.9028, 12.4964 },
  { "Stockholm", "ARN", "/flags/se.bmp", "Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 59.3293, 18.0686 },
  { "Vienna", "VIE", "/flags/at.bmp", "Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 48.2082, 16.3738 },
  { "Warsaw", "WAW", "/flags/pl.bmp", "Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 52.2297, 21.0122 },
  { "Zagreb", "ZAG", "/flags/hr.bmp", "Europe/Zagreb", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 45.8150, 15.9819 },
  { "Zurich", "ZRH", "/flags/ch.bmp", "Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3", "+01:00", true, 47.3769, 8.5417 },
  // UTC+02:00
  { "Amman", "AMM", "/flags/jo.bmp", "Asia/Amman", "EET-2EEST,M2.5.5/0,M10.5.5/1", "+02:00", true, 31.9539, 35.9106 },
  { "Athens", "ATH", "/flags/gr.bmp", "Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 37.9838, 23.7275 },
  { "Beirut", "BEY", "/flags/lb.bmp", "Asia/Beirut", "EET-2EEST,M3.5.0/0,M10.5.0/0", "+02:00", true, 33.8938, 35.5018 },
  { "Bucharest", "OTP", "/flags/ro.bmp", "Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 44.4268, 26.1025 },
  { "Cairo", "CAI", "/flags/eg.bmp", "Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24", "+02:00", true, 30.0444, 31.2357 },
  { "Cape Town", "CPT", "/flags/za.bmp", "Africa/Johannesburg", "SAST-2", "+02:00", false, -33.9249, 18.4241 },
  { "Dar es Salaam", "DAR", "/flags/tz.bmp", "Africa/Dar_es_Salaam", "EAT-3", "+02:00", false, -6.7924, 39.2083 },  // Note: EAT is UTC+3, but listed as +2, corrected to group with similar
  { "Harare", "HRE", "/flags/zw.bmp", "Africa/Harare", "CAT-2", "+02:00", false, -17.8252, 31.0335 },
  { "Helsinki", "HEL", "/flags/fi.bmp", "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 60.1699, 24.9384 },
  { "Jerusalem", "JRS", "/flags/il.bmp", "Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0", "+02:00", true, 31.7683, 35.2137 },
  { "Johannesburg", "JNB", "/flags/za.bmp", "Africa/Johannesburg", "SAST-2", "+02:00", false, -26.2041, 28.0473 },
  { "Khartoum", "KRT", "/flags/sd.bmp", "Africa/Khartoum", "CAT-2", "+02:00", true, 15.5007, 32.5599 },
  { "Kyiv", "KBP", "/flags/ua.bmp", "Europe/Kiev", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 50.4501, 30.5234 },
  { "Sofia", "SOF", "/flags/bg.bmp", "Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 42.6977, 23.3219 },
  { "Tallinn", "TLL", "/flags/ee.bmp", "Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4", "+02:00", true, 59.4370, 24.7536 },
  // UTC+03:00
  { "Addis Ababa", "ADD", "/flags/et.bmp", "Africa/Addis_Ababa", "EAT-3", "+03:00", true, 9.0300, 38.7400 },
  { "Ankara", "ESB", "/flags/tr.bmp", "Europe/Istanbul", "<+03>-3", "+03:00", true, 39.9334, 32.8597 },
  { "Antananarivo", "TNR", "/flags/mg.bmp", "Indian/Antananarivo", "EAT-3", "+03:00", false, -18.8792, 47.5079 },
  { "Baghdad", "BGW", "/flags/iq.bmp", "Asia/Baghdad", "<+03>-3", "+03:00", true, 33.3152, 44.3661 },
  { "Doha", "DOH", "/flags/qa.bmp", "Asia/Qatar", "<+03>-3", "+03:00", true, 25.2854, 51.5310 },
  { "Istanbul", "IST", "/flags/tr.bmp", "Europe/Istanbul", "<+03>-3", "+03:00", true, 41.0082, 28.9784 },
  { "Kuwait City", "KWI", "/flags/kw.bmp", "Asia/Kuwait", "<+03>-3", "+03:00", true, 29.3759, 47.9774 },
  { "Minsk", "MSQ", "/flags/by.bmp", "Europe/Minsk", "<+03>-3", "+03:00", true, 53.9045, 27.5615 },
  { "Moscow", "SVO", "/flags/ru.bmp", "Europe/Moscow", "MSK-3", "+03:00", true, 55.7558, 37.6173 },
  { "Nairobi", "NBO", "/flags/ke.bmp", "Africa/Nairobi", "EAT-3", "+03:00", false, -1.2921, 36.8219 },
  { "Riyadh", "RUH", "/flags/sa.bmp", "Asia/Riyadh", "<+03>-3", "+03:00", true, 24.7136, 46.6753 },
  // UTC+03:30
  { "Tehran", "IKA", "/flags/ir.bmp", "Asia/Tehran", "<+0330>-3:30<+0430>,J79/24,J264/24", "+03:30", true, 35.6892, 51.3890 },
  // UTC+04:00
  { "Dubai", "DXB", "/flags/ae.bmp", "Asia/Dubai", "<+04>-4", "+04:00", true, 25.2769, 55.2962 },
  { "Tbilisi", "TBS", "/flags/ge.bmp", "Asia/Tbilisi", "<+04>-4", "+04:00", true, 41.7151, 44.8271 },
  // UTC+04:30
  { "Kabul", "KBL", "/flags/af.bmp", "Asia/Kabul", "<+0430>-4:30", "+04:30", true, 34.5553, 69.2075 },
  // UTC+05:00
  { "Almaty", "ALA", "/flags/kz.bmp", "Asia/Almaty", "<+05>-5", "+05:00", true, 43.2220, 76.8512 },
  { "Islamabad", "ISB", "/flags/pk.bmp", "Asia/Karachi", "PKT-5", "+05:00", true, 33.6844, 73.0479 },
  { "Karachi", "KHI", "/flags/pk.bmp", "Asia/Karachi", "PKT-5", "+05:00", true, 24.8607, 67.0011 },
  { "Lahore", "LHE", "/flags/pk.bmp", "Asia/Karachi", "PKT-5", "+05:00", true, 31.5204, 74.3587 },
  { "Tashkent", "TAS", "/flags/uz.bmp", "Asia/Tashkent", "<+05>-5", "+05:00", true, 41.2995, 69.2401 },
  // UTC+05:30
  { "Bengaluru", "BLR", "/flags/in.bmp", "Asia/Kolkata", "IST-5:30", "+05:30", true, 12.9716, 77.5946 },
  { "Delhi", "DEL", "/flags/in.bmp", "Asia/Kolkata", "IST-5:30", "+05:30", true, 28.7041, 77.1025 },
  { "Kolkata", "CCU", "/flags/in.bmp", "Asia/Kolkata", "IST-5:30", "+05:30", true, 22.5726, 88.3639 },
  { "Mumbai", "BOM", "/flags/in.bmp", "Asia/Kolkata", "IST-5:30", "+05:30", true, 19.0760, 72.8777 },
  // UTC+05:45
  { "Kathmandu", "KTM", "/flags/np.bmp", "Asia/Kathmandu", "<+0545>-5:45", "+05:45", true, 27.7172, 85.3240 },
  // UTC+06:00
  { "Dhaka", "DAC", "/flags/bd.bmp", "Asia/Dhaka", "<+06>-6", "+06:00", true, 23.8103, 90.4125 },
  // UTC+06:30
  { "Yangon", "RGN", "/flags/mm.bmp", "Asia/Yangon", "<+0630>-6:30", "+06:30", true, 16.8661, 96.1951 },
  // UTC+07:00
  { "Bangkok", "BKK", "/flags/th.bmp", "Asia/Bangkok", "<+07>-7", "+07:00", true, 13.7563, 100.5018 },
  { "Hanoi", "HAN", "/flags/vn.bmp", "Asia/Ho_Chi_Minh", "<+07>-7", "+07:00", true, 21.0285, 105.8542 },
  { "Jakarta", "CGK", "/flags/id.bmp", "Asia/Jakarta", "WIB-7", "+07:00", false, -6.2088, 106.8456 },
  // UTC+08:00
  { "Beijing", "PEK", "/flags/cn.bmp", "Asia/Shanghai", "CST-8", "+08:00", true, 39.9042, 116.4074 },
  { "Hong Kong", "HKG", "/flags/hk.bmp", "Asia/Hong_Kong", "HKT-8", "+08:00", true, 22.3193, 114.1694 },
  { "Kuala Lumpur", "KUL", "/flags/my.bmp", "Asia/Kuala_Lumpur", "<+08>-8", "+08:00", true, 3.1390, 101.6869 },
  { "Manila", "MNL", "/flags/ph.bmp", "Asia/Manila", "PST-8", "+08:00", true, 14.5995, 120.9842 },
  { "Perth", "PER", "/flags/au.bmp", "Australia/Perth", "AWST-8", "+08:00", false, -31.9505, 115.8605 },
  { "Shanghai", "PVG", "/flags/cn.bmp", "Asia/Shanghai", "CST-8", "+08:00", true, 31.2304, 121.4737 },
  { "Singapore", "SIN", "/flags/sg.bmp", "Asia/Singapore", "<+08>-8", "+08:00", true, 1.3521, 103.8198 },
  { "Taipei", "TPE", "/flags/tw.bmp", "Asia/Taipei", "CST-8", "+08:00", true, 25.0330, 121.5654 },
  // UTC+09:00
  { "Seoul", "ICN", "/flags/kr.bmp", "Asia/Seoul", "KST-9", "+09:00", true, 37.5665, 126.9780 },
  { "Tokyo", "TYO", "/flags/jp.bmp", "Asia/Tokyo", "JST-9", "+09:00", true, 35.6762, 139.6503 },
  // UTC+09:30
  { "Adelaide", "ADL", "/flags/au.bmp", "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3", "+09:30", false, -34.9285, 138.6007 },
  { "Darwin", "DRW", "/flags/au.bmp", "Australia/Darwin", "ACST-9:30", "+09:30", false, -12.4634, 130.8456 },
  // UTC+10:00
  { "Brisbane", "BNE", "/flags/au.bmp", "Australia/Brisbane", "AEST-10", "+10:00", false, -27.4698, 153.0251 },
  { "Canberra", "CBR", "/flags/au.bmp", "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", "+10:00", false, -35.2809, 149.1300 },
  { "Guam", "GUM", "/flags/gu.bmp", "Pacific/Guam", "ChST-10", "+10:00", true, 13.4723, 144.7489 },
  { "Melbourne", "MEL", "/flags/au.bmp", "Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3", "+10:00", false, -37.8136, 144.9631 },
  { "Sydney", "SYD", "/flags/au.bmp", "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3", "+10:00", false, -33.8688, 151.2093 },
  // UTC+11:00
  { "Noumea", "NOU", "/flags/nc.bmp", "Pacific/Noumea", "NCT-11", "+11:00", false, -22.2758, 166.4580 },
  // UTC+12:00
  { "Anadyr", "DYR", "/flags/ru.bmp", "Asia/Anadyr", "<+12>-12", "+12:00", true, 64.7333, 177.5167 },
  { "Auckland", "AKL", "/flags/nz.bmp", "Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3", "+12:00", false, -36.8485, 174.7633 },
  { "Suva", "SUV", "/flags/fj.bmp", "Pacific/Fiji", "<+12>-12", "+12:00", false, -18.1416, 178.4419 },
  // UTC+13:00
  { "Nuku'alofa", "TBU", "/flags/to.bmp", "Pacific/Tongatapu", "<+13>-13", "+13:00", false, -21.1393, -175.2049 },
  // UTC+14:00
  { "Kiritimati", "CXI", "/flags/ki.bmp", "Pacific/Kiritimati", "<+14>-14", "+14:00", true, 1.8743, -157.3911 },
};

const int numCities = sizeof(cities) / sizeof(WorldCity);  // Calculate the total number of cities in the list

// State variables for the city selection pop-up
int currentCityIndex = 119;      // Default to RGN (Yangon)
int prevFocusedCityIndex = 150;  // Used to efficiently redraw only changed items when scrolling
bool cityListVisible = false;    // Flag to show/hide the city selection popup
int focusedCityIndex = 0;        // The currently highlighted city in the selection list
int listScrollOffset = 0;        // The starting index for the visible portion of the city list
const int ITEMS_PER_PAGE = 5;    // Number of cities to display at once in the selection list

// Structure to hold current weather data, using char arrays for stability.
struct CurrentWeatherData {
  char tempC[8];  // e.g., "-12.5"
  char feelsC[8];
  char humidity[4];  // e.g., "100"
  char pressure_hpa[6];
  char pressure_inhg[8];
  char pressure_mmhg[6];
  char wind_ms[8];
  char wind_kmh[5];
  char wind_mph[5];
  char currentIconCode[32];  // e.g., "partlycloudy_day"
  char next6HoursIconCode[32];
  char weatherDescription[40];
  char sunriseTime[10];  // e.g., "06:30 AM"
  char sunsetTime[10];
  char moonriseTime[10];
  char moonsetTime[10];
  char uvIndex[4];
  char windDirection[4];  // e.g., "NNE"
  char cloudCover[4];
  char locationName[64];  // Allows for longer names
  char airQualityIndex[5];
};
CurrentWeatherData weatherData;  // Global instance of the current weather data structure


// Structure to hold daily forecast data
struct DayForecast {
  String ymd;        // Date in "YYYY-MM-DD" format
  String dayStr;     // Abbreviated day of the week (e.g., "Mon")
  String icon;       // Weather icon code for the day
  float tmin, tmax;  // Minimum and maximum temperatures
  int hourScore;     // A score to determine the most representative icon for the day (closest to noon)
  DayForecast()
    : tmin(NAN), tmax(NAN), hourScore(999) {}  // Constructor to initialize with default values
};
std::vector<DayForecast> weeklyForecast;  // A vector to hold the 7-day forecast

String selectedIconDirLarge = ICON_DIR_LARGE;  // Path to the selected large icon directory
String selectedIconDirSmall = ICON_DIR_SMALL;  // Path to the selected small icon directory

// --- Hourly Forecast Graph Variables ---
// Structure to hold a single point of hourly forecast data
struct HourlyDataPoint {
  int hour;           // The hour of the day (0-23)
  float temperature;  // The temperature for that hour
  String icon;        // The weather icon code for that hour
};
std::vector<HourlyDataPoint> hourlyForecast;  // A vector to hold the 24-hour forecast

// --- PAGE MANAGEMENT ---
// Enum to define the different pages of the UI
enum ScreenPage { WEATHER_PAGE,
                  HOURLY_GRAPH_PAGE,
                  CLOCK_PAGE,
                  ALARM_PAGE,
                  TIMER_PAGE,
                  CALENDAR_PAGE
};

ScreenPage currentPage = WEATHER_PAGE;  // The page currently being displayed
bool needsRedraw = true;                // Flag to indicate that the entire screen needs to be redrawn
bool isFetchingWeather = false;         // Flag to indicate that a weather data fetch is in progress

// --- Dual-Core Task Management & Synchronization ---
TaskHandle_t dataFetchTaskHandle;          // Handle for the data fetching task
SemaphoreHandle_t dataMutex;               // Mutex to protect shared data structures
SemaphoreHandle_t fetchRequestSignal;      // Semaphore to signal a new weather data fetch request
volatile bool newDataIsAvailable = false;  // Flag to indicate new weather data is ready

struct tm calendarTime;  // Time structure to hold the date for the calendar page

// --- ALARM & TIMER CONSTANTS AND STRUCTURES ---
#define MAX_ALARMS 4  // Maximum number of alarms that can be set

// Structure to hold alarm data
struct Alarm {
  bool isEnabled = false;  // Is the alarm active?
  uint8_t hour = 6;        // Alarm hour (0-23)
  uint8_t minute = 30;     // Alarm minute (0-59)
  bool isPM = false;       // True if the hour is in the PM
};
Alarm alarms[MAX_ALARMS];  // Array to hold all alarm settings

// Structure to hold countdown timer data
struct CountdownTimer {
  bool isRunning = false;           // Is the timer currently running?
  unsigned long startTime = 0;      // The time (from millis()) when the timer was started
  unsigned long durationSetMs = 0;  // The total duration of the timer in milliseconds
  unsigned long pauseOffsetMs = 0;  // Used to accumulate paused time
};
CountdownTimer countdown;  // Global instance of the countdown timer

// --- IMPROVEMENT: Encapsulation of State ---
// Encapsulates UI state for the Alarm Page
struct AlarmUIState {
  bool inAdjustMode = false;     // True if an alarm is currently being edited
  int adjustingAlarmIndex = -1;  // Which alarm slot (0-3) is being edited
  int adjustingField = 0;        // Which part of the alarm is being edited (1=hour, 2=minute, 3=AM/PM)
};
AlarmUIState alarmUI;  // Global instance for alarm UI state

// Encapsulates UI state for the Timer Page
struct TimerUIState {
  bool inAdjustMode = false;  // True if the timer is currently being edited
  int adjustingField = 2;     // Which part of the timer is being edited (1=hour, 2=minute, 3=second)
};
TimerUIState timerUI;  // Global instance for timer UI state


// For Press-and-Hold Logic
unsigned long lastAutoIncrementTime = 0;     // Timestamp for press-and-hold value changes
const int AUTO_INCREMENT_INTERVAL_MS = 150;  // Speed of value change on hold

// --- STATE FOR TIMER ANIMATION ---
// Structure to hold the previous state of the countdown timer for animation
struct CountdownState {
  char prevHour[3];
  char prevMinute[3];
  char prevSecond[3];
};
CountdownState prevCountdownState;  // Global instance of the previous countdown state

// --- ALARM SOUNDING STATE ---
bool isAlarmSounding = false;           // True if an alarm or timer is currently sounding
unsigned long alarmSoundStartTime = 0;  // Timestamp when the alarm started sounding
struct tm alarmTriggerTime;             // Stores the time info when an alarm starts, used for timeout

// --- NEW --- ISR flag and constants for the main alarm sound pattern
volatile bool mainAlarmActive = false;             // Flag for the ISR to generate the main alarm sound
const unsigned int ALARM_BEEP_DURATION_MS = 100;   // Duration of each beep in the alarm
const unsigned int ALARM_PAUSE_DURATION_MS = 100;  // Pause between beeps in the alarm


// --- Non-Blocking Touch State Variables ---
// These are used for handling touch events without blocking the main loop
bool isTouchActive = false;                // Is the screen currently being touched?
unsigned long touchPressStartTime = 0;     // When did the current touch start?
int16_t touchStartX = 0, touchStartY = 0;  // Where did the current touch start?


// --- FORWARD DECLARATIONS BLOCK ---
void runTouchCalibration();
void drawWeatherScreen();
void drawClockPage();
void drawCalendarPage(struct tm timeinfo);
void updateAndDrawAnalogClock(bool animate);
void updateAndAnimateDigitalClock(struct tm timeinfo, bool animate);
void drawHeader();
void drawMainWeather();
void drawRightInfoPanel();
void drawWeeklyForecast();
void drawThermoIcon();
void drawBeepIcon(TFT_eSPI* canvas, int x, int y);
void IRAM_ATTR onTimer();
void handleHourlyBeep();
void saveBeepSetting();
bool fetchWeatherDataOnce(int attempt = 1, int maxAttempts = 1);
bool ensureTimeSynced();
void updateBeepIcon();
void processTouchLogic();
void adjustBrightness();
void drawCitySelectionPopup();
void confirmCitySelection();
void drawCityListItem(int index, bool isHighlighted);
void drawCityListItems();
void generateFlagReport();
void parseHourlyForecast(JsonArray timeseries);
void drawHourlyGraphPage();
time_t iso8601toEpoch(const char* iso8601);
int parseTimeToMinutes(const String& timeStr);
void drawSunMoonPhase(TFT_eSprite* canvas, int centerX, int centerY, struct tm* timeinfo, bool isNorthern, float lat, float lon);
void drawSunSymbol(TFT_eSPI* canvas, int x, int y);
void drawMoonSymbol(TFT_eSprite* canvas, int x, int y, int phase_index, bool isNorthern);
void drawAlarmPage();
void drawTimerPage();
void saveAlarms();
void loadAlarms();
void checkAlarmsAndTimers();
void handleAlarmPageTouch(int16_t touchX, int16_t touchY);
void handleTimerPageTouch(int16_t touchX, int16_t touchY);
void handleValueAdjustment(int16_t touchX, int16_t touchY, bool isHold);
void updateAndAnimateCountdownDisplay(unsigned long remainingMillis, bool animate);
void redrawAlarmItem(int index);
void updateSunAndMoonTimes();
String generateTimezoneHtml();
String formatUTCTimeToLocalString(time_t utc_timestamp, const char* target_tz_string);
void fetchLocationName();
void fetchAirQualityData();
void updateSunAndMoonTimes();
void ensureHomeTimezoneIsSet();
void handleRoot();
void handleSave();

// This CSS improves the layout of the config portal on mobile devices.
const char CUSTOM_CSS_HEAD[] PROGMEM = R"rawliteral(<style>.container div, .container p {display: block !important;width: 95% !important;margin-bottom: 15px !important;}label {display: block;margin-bottom: 5px;}input[type='text'], input[type='password'], select {width: 100%;padding: 8px;box-sizing: border-box;}a[href*='/update'], form[action='/update'] { display: none !important; }</style>)rawliteral";
// Custom HTML for the Invert Display dropdown in WiFiManager.
const char INVERT_DISPLAY_UI_HTML[] PROGMEM = R"rawliteral(<input type='hidden' id='invertDisplay' name='invertDisplay' value='{v}'><select id='invertDisplay_select' onchange="document.getElementById('invertDisplay').value=this.value;"><option value='1'>Invert Colors</option><option value='0'>Normal Colors</option></select><script>document.addEventListener('DOMContentLoaded',function(){var e=document.getElementById('invertDisplay').value,t=document.getElementById('invertDisplay_select');if(e){for(var o=0;o<t.options.length;o++)if(t.options[o].value===e){t.selectedIndex=o;break}}else{t.selectedIndex=0;document.getElementById('invertDisplay').value=t.options[0].value;}});</script>)rawliteral";

const char HTML_PART1[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Weather Station Control</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    body{font-family:sans-serif;background-color:#222;color:#eee}
    .container{max-width:600px;margin:auto;padding:20px;background-color:#333;border-radius:8px}
    h1{text-align:center;color:#00bcd4} fieldset{border:1px solid #555;border-radius:8px;margin-bottom:20px}
    legend{font-weight:bold;color:#00bcd4;padding:0 10px} .form-group{margin-bottom:15px}
    label{display:block;margin-bottom:5px;font-weight:bold}
    input[type=text],input[type=number],select{width:100%;padding:10px;box-sizing:border-box;background-color:#444;color:#eee;border:1px solid #555;border-radius:4px}
    .radio-group label{display:inline-block;margin-right:15px}
    input[type=submit]{width:100%;padding:12px;background-color:#00bcd4;color:white;border:none;border-radius:4px;font-size:1.1em;cursor:pointer}
    .status{text-align:center;margin-top:15px;font-style:italic;color:#999}
    .two-col{display:grid;grid-template-columns:1fr 1fr;gap:15px}
    .alarm-row{display:grid;grid-template-columns:60px 1fr auto;gap:10px;align-items:center;margin-bottom:10px}
    .alarm-row label{margin-bottom:0}
</style></head><body><div class="container"><h1>Weather Station Control</h1>
<form action="/save" method="POST">
<fieldset><legend><h3>Location & Time</h3></legend><div class="two-col">
<div class="form-group"><label for="lat">Latitude:</label><input type="text" id="lat" name="latitude" value=")rawliteral";

const char HTML_PART2[] PROGMEM = R"rawliteral("></div>
<div class="form-group"><label for="lon">Longitude:</label><input type="text" id="lon" name="longitude" value=")rawliteral";

const char HTML_PART3[] PROGMEM = R"rawliteral("></div></div>
<div class="form-group"><label for="tz_select">System Timezone:</label><select id='tz_select' name='timezone'>)rawliteral";

const char HTML_PART4[] PROGMEM = R"rawliteral(</select></div></fieldset>
<fieldset><legend><h3>Clock Page & Sounds</h3></legend><div class="form-group"><label for="city_index">World Clock City:</label><select id="city_index" name="city_index">)rawliteral";

const char HTML_PART5[] PROGMEM = R"rawliteral(</select></div>
<div class="form-group"><label>Hourly Chime:</label><div class="radio-group">
<label><input type="radio" name="hourly_beep" value="1" )rawliteral";

const char HTML_PART6[] PROGMEM = R"rawliteral(> Enabled</label>
<label><input type="radio" name="hourly_beep" value="0" )rawliteral";

const char HTML_PART7[] PROGMEM = R"rawliteral(> Disabled</label></div></div></fieldset>
<fieldset><legend><h3>Alarms</h3></legend>)rawliteral";

const char HTML_PART8[] PROGMEM = R"rawliteral(</fieldset><fieldset><legend><h3>Display Units & Format</h3></legend>
<div class="form-group"><label>Temperature:</label><div class="radio-group">
<label><input type="radio" name="temp_unit" value="C" )rawliteral";

const char HTML_PART9[] PROGMEM = R"rawliteral(> Celsius (&deg;C)</label>
<label><input type="radio" name="temp_unit" value="F" )rawliteral";

const char HTML_PART10[] PROGMEM = R"rawliteral(> Fahrenheit (&deg;F)</label></div></div>
<div class="form-group"><label>Wind Speed:</label><div class="radio-group">
<label><input type="radio" name="wind_unit" value="kmh" )rawliteral";

const char HTML_PART11[] PROGMEM = R"rawliteral(> Kilometers/hour (km/h)</label>
<label><input type="radio" name="wind_unit" value="mph" )rawliteral";

const char HTML_PART12[] PROGMEM = R"rawliteral(> Miles/hour (mph)</label></div></div>
<div class="form-group"><label>Pressure:</label><div class="radio-group">
<label><input type="radio" name="pressure_unit" value="0" )rawliteral";

const char HTML_PART13[] PROGMEM = R"rawliteral(> Hectopascals (hPa)</label>
<label><input type="radio" name="pressure_unit" value="1" )rawliteral";

const char HTML_PART14[] PROGMEM = R"rawliteral(> Inches of Mercury (inHg)</label>
<label><input type="radio" name="pressure_unit" value="2" )rawliteral";

const char HTML_PART15[] PROGMEM = R"rawliteral(> Millimeters of Mercury (mmHg)</label></div></div>
<div class="form-group"><label>Time Format:</label><div class="radio-group">
<label><input type="radio" name="time_format" value="12h" )rawliteral";

const char HTML_PART16[] PROGMEM = R"rawliteral(> 12-Hour</label>
<label><input type="radio" name="time_format" value="24h" )rawliteral";

const char HTML_PART17[] PROGMEM = R"rawliteral(> 24-Hour</label></div></div>
<div class="form-group"><label>Screen Colors:</label><div class="radio-group">
<label><input type="radio" name="invert_display" value="1" )rawliteral";

const char HTML_PART18[] PROGMEM = R"rawliteral(> Inverted (Dark Mode)</label>
<label><input type="radio" name="invert_display" value="0" )rawliteral";

const char HTML_PART19[] PROGMEM = R"rawliteral(> Normal (Light Mode)</label></div></div></fieldset>
<div class="form-group"><input type="submit" value="Save and Apply"></div></form>
<div class="status">Connected to: )rawliteral";

const char HTML_PART20[] PROGMEM = R"rawliteral( | IP: )rawliteral";

const char HTML_PART21[] PROGMEM = R"rawliteral(</div></div></body></html>)rawliteral";

const char HTML_SAVE_SUCCESS_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Settings Saved</title><meta http-equiv="refresh" content="3;url=/">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
    body{font-family:sans-serif;background-color:#222;color:#eee;text-align:center;padding-top:50px}
    .container{max-width:500px;margin:auto;padding:20px;background-color:#333;border-radius:8px}
    h1{color:#4CAF50} a{color:#00bcd4}
</style></head><body><div class="container">
<h1>Settings Saved Successfully!</h1>
<p>The device will now update with the new configuration.</p>
<p>You will be redirected back shortly.</p>
<p><a href="/">Go Back Now</a></p>
</div></body></html>)rawliteral";

// Dynamically generates the timezone HTML dropdown
String generateTimezoneHtml() {
  std::vector<int> sorted_indices(numCities);
  std::iota(sorted_indices.begin(), sorted_indices.end(), 0);

  std::sort(sorted_indices.begin(), sorted_indices.end(), [](int a, int b) {
    return strcmp(cities[a].cityName, cities[b].cityName) < 0;
  });

  String html = R"rawliteral(<input type='hidden' id='timezone' name='timezone' value='{v}'><style>#tz_posix_display{display: none;}</style><input type='text' id='tz_posix_display' readonly><select id='tz_select' name='tz_select' onchange="document.getElementById('timezone').value=this.value;document.getElementById('tz_posix_display').value=this.value;">)rawliteral";

  for (int index : sorted_indices) {
    const WorldCity& city = cities[index];
    String displayText = String("(") + city.utcOffset + ") " + city.cityName + " / " + city.cityCode;
    String posixValue = city.posix_timezone;
    html += "<option value='" + posixValue + "'>" + displayText + "</option>";
  }

  html += R"rawliteral(</select><script>document.addEventListener('DOMContentLoaded',function(){var e=document.getElementById('timezone').value,t=document.getElementById('tz_select'),n=document.getElementById('tz_posix_display');if(e)for(var o=0;o<t.options.length;o++)if(t.options[o].value===e){t.selectedIndex=o;break}n.value=t.value});</script>)rawliteral";
  return html;
}

// REVISED handleRoot function - uses almost no RAM
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent_P(HTML_PART1);
  server.sendContent(latitude);
  server.sendContent_P(HTML_PART2);
  server.sendContent(longitude);
  server.sendContent_P(HTML_PART3);

  std::vector<int> sorted_indices(numCities);
  std::iota(sorted_indices.begin(), sorted_indices.end(), 0);
  std::sort(sorted_indices.begin(), sorted_indices.end(), [](int a, int b) {
    return strcmp(cities[a].cityName, cities[b].cityName) < 0;
  });

  for (int index : sorted_indices) {
    const WorldCity& city = cities[index];
    String option = "<option value='" + String(city.posix_timezone) + "'";
    if (system_tz_string == city.posix_timezone) {
      option += " selected";
    }
    option += ">(" + String(city.utcOffset) + ") " + String(city.cityName) + " / " + String(city.cityCode) + "</option>";
    server.sendContent(option);
  }

  server.sendContent_P(HTML_PART4);

  for (int index : sorted_indices) {
    const WorldCity& city = cities[index];
    String option = "<option value='" + String(index) + "'";
    if (currentCityIndex == index) {
      option += " selected";
    }
    option += ">(" + String(city.utcOffset) + ") " + String(city.cityName) + " / " + String(city.cityCode) + "</option>";
    server.sendContent(option);
  }

  server.sendContent_P(HTML_PART5);
  if (hourlyBeepEnabled) server.sendContent("checked");
  server.sendContent_P(HTML_PART6);
  if (!hourlyBeepEnabled) server.sendContent("checked");
  server.sendContent_P(HTML_PART7);

  for (int i = 0; i < MAX_ALARMS; i++) {
    String alarm_html = "<div class='alarm-row'><label for='alarm_en_" + String(i) + "'>Alarm " + String(i + 1) + ":</label>";
    alarm_html += "<div><label>Time (HH:MM)</label><div class='two-col'>";
    alarm_html += "<input type='number' name='alarm_h_" + String(i) + "' min='0' max='23' value='" + String(alarms[i].hour) + "'>";
    alarm_html += "<input type='number' name='alarm_m_" + String(i) + "' min='0' max='59' value='" + String(alarms[i].minute) + "'>";
    alarm_html += "</div></div><div><label>Status</label><select name='alarm_en_" + String(i) + "'><option value='1'";
    if (alarms[i].isEnabled) alarm_html += " selected";
    alarm_html += ">On</option><option value='0'";
    if (!alarms[i].isEnabled) alarm_html += " selected";
    alarm_html += ">Off</option></select></div></div>";
    server.sendContent(alarm_html);
  }

  server.sendContent_P(HTML_PART8);
  if (!useFahrenheit) server.sendContent("checked");
  server.sendContent_P(HTML_PART9);
  if (useFahrenheit) server.sendContent("checked");
  server.sendContent_P(HTML_PART10);
  if (!useMph) server.sendContent("checked");
  server.sendContent_P(HTML_PART11);
  if (useMph) server.sendContent("checked");
  server.sendContent_P(HTML_PART12);
  if (pressureUnitState == 0) server.sendContent("checked");
  server.sendContent_P(HTML_PART13);
  if (pressureUnitState == 1) server.sendContent("checked");
  server.sendContent_P(HTML_PART14);
  if (pressureUnitState == 2) server.sendContent("checked");
  server.sendContent_P(HTML_PART15);
  if (!use24HourFormat) server.sendContent("checked");
  server.sendContent_P(HTML_PART16);
  if (use24HourFormat) server.sendContent("checked");
  server.sendContent_P(HTML_PART17);
  if (invertDisplay) server.sendContent("checked");
  server.sendContent_P(HTML_PART18);
  if (!invertDisplay) server.sendContent("checked");
  server.sendContent_P(HTML_PART19);
  server.sendContent(WiFi.SSID());
  server.sendContent_P(HTML_PART20);
  server.sendContent(WiFi.localIP().toString());
  server.sendContent_P(HTML_PART21);

  server.sendContent("");
}

void handleSave() {
  Serial.println("Received settings update from web control portal.");
  latitude = server.arg("latitude");
  longitude = server.arg("longitude");
  system_tz_string = server.arg("timezone");
  useFahrenheit = (server.arg("temp_unit") == "F");
  useMph = (server.arg("wind_unit") == "mph");
  pressureUnitState = server.arg("pressure_unit").toInt();
  use24HourFormat = (server.arg("time_format") == "24h");
  invertDisplay = (server.arg("invert_display") == "1");

  currentCityIndex = server.arg("city_index").toInt();
  hourlyBeepEnabled = (server.arg("hourly_beep") == "1");

  for (int i = 0; i < MAX_ALARMS; i++) {
    alarms[i].isEnabled = (server.arg("alarm_en_" + String(i)) == "1");
    alarms[i].hour = server.arg("alarm_h_" + String(i)).toInt();
    alarms[i].minute = server.arg("alarm_m_" + String(i)).toInt();
    if (alarms[i].hour > 23) alarms[i].hour = 23;
    if (alarms[i].minute > 59) alarms[i].minute = 59;
    alarms[i].isPM = (alarms[i].hour >= 12);
  }
  saveAlarms();

  prefs.begin("weather-app", false);
  prefs.putString("latitude", latitude);
  prefs.putString("longitude", longitude);
  prefs.putString("timezone", system_tz_string);
  prefs.putBool("useFahrenheit", useFahrenheit);
  prefs.putBool("useMph", useMph);
  prefs.putInt("pressureUnit", pressureUnitState);
  prefs.putBool("use24Hour", use24HourFormat);
  prefs.putBool("invertDisplay", invertDisplay);
  prefs.putInt("cityIndex", currentCityIndex);
  prefs.putBool("hourlyBeep", hourlyBeepEnabled);
  prefs.end();

  Serial.println("New settings saved to Preferences.");

  isNorthernHemisphere = (latitude.toFloat() >= 0);
  tft.invertDisplay(invertDisplay);
  configTzTime(system_tz_string.c_str(), "pool.ntp.org", "time.cloudflare.com");
  ensureTimeSynced();

  server.send_P(200, "text/html", HTML_SAVE_SUCCESS_PAGE);

  if (currentPage == ALARM_PAGE || currentPage == CLOCK_PAGE || currentPage == WEATHER_PAGE || currentPage == HOURLY_GRAPH_PAGE) {
    needsRedraw = true;
  }

  if (!isFetchingWeather) {
    Serial.println("[Web Save] Triggering weather data refresh due to settings change.");
    isFetchingWeather = true;
    xSemaphoreGive(fetchRequestSignal);
  }
}

// Hardware timer interrupt service routine for beeping sounds.
void IRAM_ATTR onTimer() {
  unsigned long currentTime = micros();

  if (hourBeepActive) {
    unsigned int interval = buzzerOn ? (beepInterval * 1000) : (pauseInterval * 1000);
    if (currentTime - lastInterruptTime >= interval) {
      if (buzzerOn) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerOn = false;
        isBlinking = false;
        beepCount++;
        if (beepCount >= targetBeeps) {
          hourBeepActive = false;
          beepCount = 0;
        }
      } else if (beepCount < targetBeeps) {
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerOn = true;
        isBlinking = true;
      }
      lastInterruptTime = currentTime;
    }
  } else if (mainAlarmActive) {
    unsigned int interval = buzzerOn ? (ALARM_BEEP_DURATION_MS * 1000) : (ALARM_PAUSE_DURATION_MS * 1000);
    if (currentTime - lastInterruptTime >= interval) {
      buzzerOn = !buzzerOn;
      digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
      lastInterruptTime = currentTime;
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }
}

String formatUTCTimeToLocalString(time_t utc_timestamp, const char* target_tz_string) {
  if (utc_timestamp <= 0) {
    return "--:--";
  }

  String result = "--:--";
  if (xSemaphoreTake(timezoneMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    char* original_tz = getenv("TZ");
    setenv("TZ", target_tz_string, 1);
    tzset();

    struct tm* timeinfo;
    timeinfo = localtime(&utc_timestamp);

    char buffer[10];
    strftime(buffer, sizeof(buffer), use24HourFormat ? "%H:%M" : "%I:%M %p", timeinfo);

    if (original_tz) {
      setenv("TZ", original_tz, 1);
    } else {
      unsetenv("TZ");
    }
    tzset();

    result = String(buffer);
    result.trim();
    xSemaphoreGive(timezoneMutex);
  }
  return result;
}

void convertUTCToLocalTM(time_t utc_timestamp, struct tm* result_tm, const char* target_tz_string) {
  if (xSemaphoreTake(timezoneMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Store the original system timezone
    char* original_tz = getenv("TZ");

    // Temporarily set the process timezone to the target city's timezone
    setenv("TZ", target_tz_string, 1);
    tzset();

    // Convert the UTC time_t to the new local time
    localtime_r(&utc_timestamp, result_tm);

    // Restore the original system timezone
    if (original_tz) {
      setenv("TZ", original_tz, 1);
    } else {
      unsetenv("TZ");
    }
    tzset();

    // Release the mutex
    xSemaphoreGive(timezoneMutex);
  }
}

// This function gets the local time for a remote city WITHOUT changing the system's main timezone.
void getRemoteTime(struct tm* timeinfo, const char* remoteTz) {
  if (xSemaphoreTake(timezoneMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    time_t now;
    time(&now);

    char* original_tz = getenv("TZ");
    setenv("TZ", remoteTz, 1);
    tzset();
    localtime_r(&now, timeinfo);

    if (original_tz) {
      setenv("TZ", original_tz, 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
    xSemaphoreGive(timezoneMutex);
  }
}

bool getSafeLocalTime(struct tm* timeinfo, uint32_t ms = 5) {
  bool success = false;
  if (xSemaphoreTake(timezoneMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    success = getLocalTime(timeinfo, ms);
    xSemaphoreGive(timezoneMutex);
  } else {
    Serial.println("!!! FAILED to get timezoneMutex in getSafeLocalTime !!!");
  }
  return success;
}

// Returns the 3-letter abbreviation for the day of the week.
String getDayShort(int dayOfWeek) {
  static const char* names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  return String(names[dayOfWeek % 7]);
}

// Returns the 3-letter abbreviation for the month.
String getMonthShort(int month) {
  static const char* names[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  return String(names[month % 12]);
}

// Formats a Unix timestamp into a "HH:MM AM/PM" string.
String formatTime(time_t timestamp) {
  struct tm* timeinfo;
  timeinfo = localtime(&timestamp);
  char buffer[10];
  strftime(buffer, sizeof(buffer), "%I:%M %p", timeinfo);
  return String(buffer);
}

// Converts a weather symbol code from the API into a human-readable description.
String getWeatherDescription(String symbolCode) {
  symbolCode.toLowerCase();
  symbolCode.replace("-", "_");
  bool isNight = (symbolCode.indexOf("night") >= 0);
  bool isShowers = (symbolCode.indexOf("showers") >= 0);
  bool isLight = (symbolCode.indexOf("light") >= 0);
  bool isHeavy = (symbolCode.indexOf("heavy") >= 0);
  if (symbolCode.indexOf("clearsky") >= 0) return isNight ? "Clear night" : "Clear";
  if (symbolCode.indexOf("fair") >= 0 || symbolCode.indexOf("partlycloudy") >= 0) return "Partly cloudy";
  if (symbolCode.indexOf("cloudy") >= 0) return "Cloudy";
  if (symbolCode.indexOf("fog") >= 0) return "Fog";
  String base = "";
  if (symbolCode.indexOf("sleet") >= 0) base = "Sleet";
  else if (symbolCode.indexOf("snow") >= 0) base = "Snow";
  else if (symbolCode.indexOf("rain") >= 0) base = "Rain";
  if (!base.isEmpty()) {
    if (isShowers) base += " showers";
    if (isLight) base = "Light " + base;
    if (isHeavy) base = "Heavy " + base;
    return base;
  }
  if (symbolCode.indexOf("thunder") >= 0) return "Thunderstorms";
  symbolCode.replace("_", " ");
  if (symbolCode.length()) symbolCode.setCharAt(0, toupper(symbolCode[0]));
  return symbolCode;
}

// Normalizes weather symbol codes from the API to match the available icon filenames.
String normalizeWeatherSymbol(String symbolCode) {
  symbolCode.toLowerCase();
  symbolCode.replace("-", "_");
  if (symbolCode == "partlycloudy_day") return "fair_day";
  if (symbolCode == "partlycloudy_night") return "fair_night";
  symbolCode.replace("heavyrainshowers", "heavyrain");
  symbolCode.replace("lightrainshowers", "lightrain");
  symbolCode.replace("rainshowers", "rain");
  symbolCode.replace("heavysnowshowers", "heavysnow");
  symbolCode.replace("lightsnowshowers", "lightsnow");
  symbolCode.replace("snowshowers", "snow");
  symbolCode.replace("heavysleetshowers", "heavysleet");
  symbolCode.replace("lightsleetshowers", "lightsleet");
  symbolCode.replace("sleetshowers", "sleet");
  return symbolCode;
}

// Computes the "feels like" temperature based on heat index (hot) or wind chill (cold).
float computeFeelsLike(float tempC, float humidity, float wind_ms) {
  if (isnan(tempC)) return NAN;
  if (tempC >= 27.0 && humidity >= 40.0) {
    float T = tempC * 1.8 + 32;
    float R = humidity;
    float HI_F = -42.379 + 2.04901523 * T + 10.14333127 * R - 0.22475541 * T * R - 6.83783e-3 * T * T - 5.481717e-2 * R * R + 1.22874e-3 * T * T * R + 8.5282e-4 * T * R * R - 1.99e-6 * T * T * R * R;
    return (HI_F - 32) * 5.0 / 9.0;
  }
  if (tempC <= 10.0 && wind_ms >= 1.3) {
    float V_kmh = wind_ms * 3.6f;
    float WC_C = 13.12 + 0.6215 * tempC - 11.37 * powf(V_kmh, 0.16f) + 0.3965 * tempC * powf(V_kmh, 0.16f);
    return WC_C;
  }
  return tempC;
}

// Converts wind direction in degrees to a cardinal direction string (e.g., "NNE").
String degreesToCardinal(float degrees) {
  if (isnan(degrees)) return "--";
  const char* cardinals[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
  int index = round(degrees / 22.5);
  return cardinals[index % 16];
}

// Converts a 24-bit RGB color to a 16-bit 565 format for the TFT display.
static inline uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Mounts the SD card.
void mountSdCard() {
  if (SD.begin(SD_CARD_CS_PIN)) {
    isSdCardMounted = true;
    Serial.println("SD card mounted successfully.");
  } else {
    Serial.println("SD card mount failed!");
  }
}

// Checks if a file exists on the SD card.
bool fileExistsOnSD(const String& path) {
  return isSdCardMounted ? SD.exists(path) : false;
}

// Loads a 24-bit or 16-bit BMP file from the SD card into a 16-bit (565) memory buffer.
bool loadBmpTo565Buffer(const String& path, uint16_t*& buf, int& w, int& h) {
  if (!isSdCardMounted) return false;
  File bmpFile = SD.open(path);
  if (!bmpFile) return false;
  uint16_t signature;
  bmpFile.read((uint8_t*)&signature, 2);
  if (signature != 0x4D42) {
    bmpFile.close();
    return false;
  }
  uint32_t fileSize, reserved, dataOffset, headerSize;
  int32_t bmpWidth, bmpHeight;
  uint16_t planes, bpp;
  uint32_t compression;
  bmpFile.read((uint8_t*)&fileSize, 4);
  bmpFile.read((uint8_t*)&reserved, 4);
  bmpFile.read((uint8_t*)&dataOffset, 4);
  bmpFile.read((uint8_t*)&headerSize, 4);
  bmpFile.read((uint8_t*)&bmpWidth, 4);
  bmpFile.read((uint8_t*)&bmpHeight, 4);
  bmpFile.read((uint8_t*)&planes, 2);
  bmpFile.read((uint8_t*)&bpp, 2);
  bmpFile.read((uint8_t*)&compression, 4);
  if (planes != 1 || (bpp != 24 && bpp != 16) || compression != 0) {
    bmpFile.close();
    return false;
  }
  h = abs(bmpHeight);
  w = bmpWidth;
  buf = (uint16_t*)malloc((size_t)w * h * 2);
  if (!buf) {
    bmpFile.close();
    return false;
  }
  bmpFile.seek(dataOffset);
  int rowBytes = (bpp == 24) ? ((w * 3 + 3) & ~3) : ((w * 2 + 3) & ~3);
  std::unique_ptr<uint8_t[]> rowBuffer(new uint8_t[rowBytes]);
  for (int r = 0; r < h; r++) {
    int bmpRow = (bmpHeight > 0) ? (h - 1 - r) : r;
    if (bmpFile.read(rowBuffer.get(), rowBytes) < rowBytes) {
      free(buf);
      buf = nullptr;
      bmpFile.close();
      return false;
    }
    if (bpp == 24) {
      for (int x = 0; x < w; x++) buf[(size_t)bmpRow * w + x] = rgb888_to_565(rowBuffer[x * 3 + 2], rowBuffer[x * 3 + 1], rowBuffer[x * 3]);
    } else {
      for (int x = 0; x < w; x++) buf[(size_t)bmpRow * w + x] = ((uint16_t*)rowBuffer.get())[x];
    }
  }
  bmpFile.close();
  return true;
}

// Draws a BMP file from the SD card directly to the TFT screen.
bool drawBmpFromFile(const String& path, int x, int y) {
  uint16_t* buffer = nullptr;
  int w = 0, h = 0;
  if (!loadBmpTo565Buffer(path, buffer, w, h)) {
    return false;
  }
  tft.pushImage(x, y, w, h, buffer);
  free(buffer);
  return true;
}

// Draws a BMP file from the SD card to a specified TFT_eSprite (off-screen canvas).
bool drawBmpToSprite(TFT_eSprite& sprite, const String& path, int x, int y) {
  uint16_t* buffer = nullptr;
  int w = 0, h = 0;
  if (!loadBmpTo565Buffer(path, buffer, w, h)) {
    return false;
  }
  sprite.pushImage(x, y, w, h, buffer);
  free(buffer);
  return true;
}

// Automatically detects which icon directories exist on the SD card.
void detectIconDirectories() {
  auto pickDir = [&](const std::vector<String>& candidates) -> String {
    for (const auto& dir : candidates) {
      if (fileExistsOnSD(dir) || fileExistsOnSD(dir + "/fair_day.bmp")) return dir;
    }
    return candidates.front();
  };
  selectedIconDirLarge = pickDir({ ICON_DIR_LARGE, "/bmp64x64", "/bmp64x64" });
  selectedIconDirSmall = pickDir({ ICON_DIR_SMALL, "/bmp32x32", "/BMP32x32" });
}

// Displays a message on the screen, typically during startup.
void displayBootMessage(const String& message) {
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  tft.setTextDatum(MC_DATUM);
  int newlineIndex = message.indexOf('\n');
  if (newlineIndex != -1) {
    tft.drawString(message.substring(0, newlineIndex), tft.width() / 2, tft.height() / 2 - 10);
    tft.drawString(message.substring(newlineIndex + 1), tft.width() / 2, tft.height() / 2 + 10);
  } else {
    tft.drawString(message, tft.width() / 2, tft.height() / 2);
  }
  Serial.println("[Boot] " + message);
}

// Displays the startup logo from the SD card, if it exists.
void displayStartupLogo() {
  Serial.println("========== LOGO FILE REPORT ==========");
  if (!isSdCardMounted) {
    Serial.println("[SD Card] Not mounted. Skipping logo check.");
    Serial.println("=====================================");
    return;
  }

  String logoPath = "/Logo/logo.bmp";
  Serial.printf("[Logo Check] Path: %-21s -> ", logoPath.c_str());

  if (fileExistsOnSD(logoPath)) {
    Serial.println("[OK]");
    Serial.println("=====================================");
    uint16_t* buffer = nullptr;
    int w = 0, h = 0;
    if (loadBmpTo565Buffer(logoPath, buffer, w, h)) {
      tft.fillScreen(COLOR_BACKGROUND);
      tft.pushImage((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2, w, h, buffer);
      free(buffer);
      delay(3000);
    }
  } else {
    Serial.println("[MISSING]");
    Serial.println("=====================================");
  }
}

// Calculates the pixel width of a temperature string with its units (°F/°C).
int16_t getTempUnitWidth(const String& tempStrCelsius, uint8_t size, uint8_t decimalPlaces = 0) {
  if (tempStrCelsius.isEmpty() || tempStrCelsius == "--") {
    tft.setTextSize(size);
    return tft.textWidth("--");
  }
  float tempVal = tempStrCelsius.toFloat();
  if (useFahrenheit) {
    tempVal = tempVal * 1.8f + 32.0f;
  }
  String tempStr = (decimalPlaces == 0) ? String((int)roundf(tempVal)) : String(tempVal, (unsigned int)decimalPlaces);
  char unitChar = useFahrenheit ? 'F' : 'C';
  tft.setTextSize(size);
  int16_t numWidth = tft.textWidth(tempStr);
  int16_t degWidth = 6 * size;
  int16_t unitWidth = tft.textWidth(String(unitChar));
  return numWidth + 2 + degWidth + 2 + unitWidth;
}

// Draws a temperature string with its units (°F/°C) centered at a given X coordinate.
void drawTempUnit(int centerX, int baselineY, const String& tempStrCelsius, uint8_t sizeNum, uint8_t sizeDegSymbol, uint16_t color, uint16_t bgColor = COLOR_BACKGROUND, int degOffsetY = 0, uint8_t decimalPlaces = 0) {
  if (tempStrCelsius.isEmpty() || tempStrCelsius == "--") {
    tft.setTextSize(sizeNum);
    tft.setTextColor(color, bgColor);
    uint16_t w = tft.textWidth("--");
    uint16_t h = 8 * sizeNum;
    int x = centerX - w / 2;
    int yTop = baselineY - h;
    tft.fillRect(x - 2, yTop - 2, w + 4, h + 4, bgColor);
    tft.setCursor(x, yTop);
    tft.print("--");
    return;
  }
  float tempVal = tempStrCelsius.toFloat();
  if (useFahrenheit) {
    tempVal = tempVal * 1.8f + 32.0f;
  }
  String tempStr = (decimalPlaces == 0) ? String((int)roundf(tempVal)) : String(tempVal, (unsigned int)decimalPlaces);
  char unitChar = useFahrenheit ? 'F' : 'C';
  const int charHNum = 8 * sizeNum;
  const int charWDeg = 6 * sizeDegSymbol, charHDeg = 8 * sizeDegSymbol;
  const int KERN_AFTER_NUM = 2, KERN_AFTER_DEG = 2;
  tft.setTextSize(sizeNum);
  const int wNum = tft.textWidth(tempStr), wC = tft.textWidth(String(unitChar));
  const int totalW = wNum + KERN_AFTER_NUM + charWDeg + KERN_AFTER_DEG + wC;
  const int xStart = centerX - totalW / 2;
  const int yTop = baselineY - charHNum;
  tft.fillRect(xStart - 2, yTop, totalW + 4, charHNum + 2, bgColor);
  tft.setTextWrap(false);
  tft.setTextColor(color, bgColor);
  tft.setTextSize(sizeNum);
  tft.setCursor(xStart, yTop);
  tft.print(tempStr);
  const int degLeft = xStart + wNum + KERN_AFTER_NUM;
  const int degCX = degLeft + charWDeg / 2;
  const int degCY = yTop + charHDeg / 2 + degOffsetY;
  int r = max(1, (int)roundf(charHDeg / 4.0f));
  tft.drawCircle(degCX, degCY, r, color);
  const int cX = degLeft + charWDeg + KERN_AFTER_DEG;
  tft.setTextSize(sizeNum);
  tft.setCursor(cX, yTop);
  tft.print(unitChar);
}

// Structure to hold the previous state of the digital clock for animation purposes.
struct DigitalClockState {
  char prevDayOfWeek[12];
  char prevDayMonth[15];
  char prevYear[5];
  char prevHour[3];
  char prevMinute[3];
  char prevAmPm[3];
  char prevSecond[3];
};
DigitalClockState prevClockState;
TFT_eSprite clockSprite = TFT_eSprite(&tft);
TFT_eSprite digitalClockSprite = TFT_eSprite(&tft);
TFT_eSprite beepIconSprite = TFT_eSprite(&tft);

// Draws a single hand of the analog clock.
void drawHand(TFT_eSPI* canvas, int centerX, int centerY, float angle, int length, int width, int tailLength, uint16_t color) {
  float angleRad = angle * DEG_TO_RAD;
  float perpAngleRad = angleRad + PI / 2.0;
  int tipCenterX = centerX + cos(angleRad) * length, tipCenterY = centerY + sin(angleRad) * length;
  int tip1X = tipCenterX + cos(perpAngleRad) * 1, tip1Y = tipCenterY + sin(perpAngleRad) * 1;
  int tip2X = tipCenterX - cos(perpAngleRad) * 1, tip2Y = tipCenterY - sin(perpAngleRad) * 1;
  int base1X = centerX + cos(perpAngleRad) * (width / 2.0), base1Y = centerY + sin(perpAngleRad) * (width / 2.0);
  int base2X = centerX - cos(perpAngleRad) * (width / 2.0), base2Y = centerY - sin(perpAngleRad) * (width / 2.0);
  canvas->fillTriangle(base1X, base1Y, tip1X, tip1Y, base2X, base2Y, color);
  canvas->fillTriangle(tip1X, tip1Y, tip2X, tip2Y, base2X, base2Y, color);
  if (tailLength > 0) {
    canvas->fillTriangle(centerX + cos(angleRad + PI) * tailLength, centerY + sin(angleRad + PI) * tailLength, base1X, base1Y, base2X, base2Y, color);
  }
}

// Helper function to parse time strings like "06:30 AM" or "18:30" into total minutes from midnight.
int parseTimeToMinutes(const String& timeStr) {
  if (timeStr == "--:--") return -1;
  int hours = -1, minutes = -1;

  if (timeStr.indexOf(":") != -1) {
    if (timeStr.indexOf(" ") != -1) {  // 12-hour format with AM/PM
      char ampm[3];
      sscanf(timeStr.c_str(), "%d:%d %2s", &hours, &minutes, ampm);
      if (strcmp(ampm, "PM") == 0 && hours != 12) {
        hours += 12;
      }
      if (strcmp(ampm, "AM") == 0 && hours == 12) {
        hours = 0;
      }
    } else {  // 24-hour format
      sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes);
    }
  }

  if (hours != -1 && minutes != -1) {
    return hours * 60 + minutes;
  }
  return -1;  // Return -1 if parsing failed
}

// Calculates the moon phase as a fraction from 0.0 (New Moon) to 1.0.
double calculateMoonPhase(time_t now) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);

  int year = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;

  double a, b, jd;
  if (month < 3) {
    year--;
    month += 12;
  }
  a = floor(year / 100.0);
  b = 2 - a + floor(a / 4.0);
  jd = floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + b - 1524.5;
  jd += (timeinfo.tm_hour + (timeinfo.tm_min / 60.0) + (timeinfo.tm_sec / 3600.0)) / 24.0;

  double days_since_new = jd - 2451549.5;
  double new_moons = days_since_new / 29.53058861;

  return new_moons - floor(new_moons);
}

// Converts a moon phase fraction into a human-readable name.
String getMoonPhaseName(double phase_fraction) {
  if (phase_fraction < 0.015 || phase_fraction >= 0.985) return "New Moon";
  if (phase_fraction < 0.235) return "Waxing Crescent";
  if (phase_fraction < 0.265) return "First Quarter";
  if (phase_fraction < 0.485) return "Waxing Gibbous";
  if (phase_fraction < 0.515) return "Full Moon";
  if (phase_fraction < 0.735) return "Waning Gibbous";
  if (phase_fraction < 0.765) return "Last Quarter";
  return "Waning Crescent";
}

// Draws a simple sun symbol on a canvas (sprite).
void drawSunSymbol(TFT_eSprite* canvas, int x, int y) {
  uint16_t sunColor = CLOCK_COLOR_ACCENT;
  int coreRadius = 5;
  int rayLength = 3;
  int rayStart = coreRadius;
  canvas->fillCircle(x, y, coreRadius, sunColor);
  for (int i = 0; i < 8; i++) {
    float angle = i * 45 * DEG_TO_RAD;
    int x1 = x + cos(angle) * rayStart;
    int y1 = y + sin(angle) * rayStart;
    int x2 = x + cos(angle) * (rayStart + rayLength);
    int y2 = y + sin(angle) * (rayStart + rayLength);
    canvas->drawLine(x1, y1, x2, y2, sunColor);
  }
}

// Draws the moon symbol, correctly showing the phase and orientation for the given hemisphere.
void drawMoonSymbol(TFT_eSprite* canvas, int x, int y, int phase_index, bool isNorthern) {
  int radius = 7;
  uint16_t moonColor = 0xAE1C;
  uint16_t shadowColor = COLOR_BACKGROUND;

  canvas->fillCircle(x, y, radius, moonColor);
  long shadow_x;
  int diameter = radius * 2;

  if (isNorthern) {
    if (phase_index <= 14) {
      shadow_x = map(phase_index, 0, 14, x, x - diameter);
    } else {
      shadow_x = map(phase_index, 14, 28, x + diameter, x);
    }
  } else {
    if (phase_index <= 14) {
      shadow_x = map(phase_index, 0, 14, x, x + diameter);
    } else {
      shadow_x = map(phase_index, 14, 28, x - diameter, x);
    }
  }
  canvas->fillCircle(shadow_x, y, radius, shadowColor);
}

void drawSunMoonPhase(TFT_eSprite* canvas, int centerX, int centerY, struct tm* timeinfo, bool isNorthern, float lat, float lon) {
  static int sunriseMinutes = -1, sunsetMinutes = -1;
  static int moonriseMinutes = -1, moonsetMinutes = -1;
  static unsigned long lastCalcTimestamp = 0;
  time_t now = mktime(timeinfo);

  if (millis() - lastCalcTimestamp > 300000UL || lastCalcTimestamp == 0) {
    Serial.println("[Sun/Moon] Recalculating rise and set times...");

    double phase_fraction_for_print = calculateMoonPhase(now);
    String phaseName = getMoonPhaseName(phase_fraction_for_print);
    Serial.printf("[Moon] Current Phase: %s\n", phaseName.c_str());

    SunRise sun;
    sun.calculate(lat, lon, now);
    time_t sunrise_t_utc = sun.riseTime;
    time_t sunset_t_utc = sun.setTime;

    MoonRise moon;
    moon.calculate(lat, lon, now);
    time_t moonrise_t_utc = moon.riseTime;
    time_t moonset_t_utc = moon.setTime;

    struct tm tm_sunrise_local, tm_sunset_local, tm_moonrise_local, tm_moonset_local;
    const char* city_tz = cities[currentCityIndex].posix_timezone;

    convertUTCToLocalTM(sunrise_t_utc, &tm_sunrise_local, city_tz);
    convertUTCToLocalTM(sunset_t_utc, &tm_sunset_local, city_tz);
    convertUTCToLocalTM(moonrise_t_utc, &tm_moonrise_local, city_tz);
    convertUTCToLocalTM(moonset_t_utc, &tm_moonset_local, city_tz);

    sunriseMinutes = (sunrise_t_utc > 0) ? tm_sunrise_local.tm_hour * 60 + tm_sunrise_local.tm_min : -1;
    sunsetMinutes = (sunset_t_utc > 0) ? tm_sunset_local.tm_hour * 60 + tm_sunset_local.tm_min : -1;
    moonriseMinutes = (moonrise_t_utc > 0) ? tm_moonrise_local.tm_hour * 60 + tm_moonrise_local.tm_min : -1;
    moonsetMinutes = (moonset_t_utc > 0) ? tm_moonset_local.tm_hour * 60 + tm_moonset_local.tm_min : -1;

    lastCalcTimestamp = millis();
  }

  if (sunriseMinutes == -1 || sunsetMinutes == -1) return;

  int arcRadius = (SCREEN_HEIGHT / 2) - 50;

  for (int i = 180; i <= 360; i += 10) {
    float angle = i * DEG_TO_RAD;
    int x = centerX + cos(angle) * arcRadius;
    int y = centerY + sin(angle) * arcRadius;
    canvas->drawPixel(x, y, CLOCK_COLOR_TICK);
  }

  int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  bool isSunUp = (currentMinutes >= sunriseMinutes && currentMinutes < sunsetMinutes);
  bool isMoonUp = false;
  if (moonriseMinutes != -1 && moonsetMinutes != -1) {
    if (moonriseMinutes < moonsetMinutes) {
      isMoonUp = (currentMinutes >= moonriseMinutes && currentMinutes < moonsetMinutes);
    } else {
      isMoonUp = (currentMinutes >= moonriseMinutes || currentMinutes < moonsetMinutes);
    }
  }

  if (isSunUp) {
    float daylightDuration = sunsetMinutes - sunriseMinutes;
    if (daylightDuration <= 0) daylightDuration = 1;
    float minutesSinceSunrise = currentMinutes - sunriseMinutes;
    float sunProgress = constrain(minutesSinceSunrise / daylightDuration, 0.0, 1.0);
    float sunAngleRad = PI + (sunProgress * PI);
    int sunX = centerX + cos(sunAngleRad) * arcRadius;
    int sunY = centerY + sin(sunAngleRad) * arcRadius;
    drawSunSymbol(canvas, sunX, sunY);
  } else if (isMoonUp) {
    float moonAirTime;
    float minutesSinceMoonrise;
    if (moonriseMinutes < moonsetMinutes) {
      moonAirTime = moonsetMinutes - moonriseMinutes;
      minutesSinceMoonrise = currentMinutes - moonriseMinutes;
    } else {
      moonAirTime = (1440 - moonriseMinutes) + moonsetMinutes;
      if (currentMinutes >= moonriseMinutes) {
        minutesSinceMoonrise = currentMinutes - moonriseMinutes;
      } else {
        minutesSinceMoonrise = (1440 - moonriseMinutes) + currentMinutes;
      }
    }
    if (moonAirTime <= 0) moonAirTime = 1;

    float moonProgress = constrain(minutesSinceMoonrise / moonAirTime, 0.0, 1.0);
    float moonAngleRad = PI + (moonProgress * PI);
    int moonX = centerX + cos(moonAngleRad) * arcRadius;
    int moonY = centerY + sin(moonAngleRad) * arcRadius;

    double phase_fraction = calculateMoonPhase(now);
    int phase_index_for_drawing = (int)(phase_fraction * 28.0 + 0.5);
    drawMoonSymbol(canvas, moonX, moonY, phase_index_for_drawing, isNorthern);
  }
}

// Draws a straight, rectangular hand for the analog clock (used for the second hand).
void drawStraightHandWithTail(TFT_eSPI* canvas, int centerX, int centerY, float angle, int length, int tailLength, int thickness, uint16_t color) {
  float angleRad = angle * DEG_TO_RAD;
  float perpAngleRad = angleRad + PI / 2.0;
  float halfThick = thickness / 2.0;
  int tipX = centerX + cos(angleRad) * length, tipY = centerY + sin(angleRad) * length;
  int tailX = centerX - cos(angleRad) * tailLength, tailY = centerY - sin(angleRad) * tailLength;
  int p1x = tipX + cos(perpAngleRad) * halfThick, p1y = tipY + sin(perpAngleRad) * halfThick;
  int p2x = tipX - cos(perpAngleRad) * halfThick, p2y = tipY - sin(perpAngleRad) * halfThick;
  int p3x = tailX - cos(perpAngleRad) * halfThick, p3y = tailY - sin(perpAngleRad) * halfThick;
  int p4x = tailX + cos(perpAngleRad) * halfThick, p4y = tailY + sin(perpAngleRad) * halfThick;
  canvas->fillTriangle(p1x, p1y, p2x, p2y, p3x, p3y, color);
  canvas->fillTriangle(p1x, p1y, p3x, p3y, p4x, p4y, color);
}

// Draws the static parts of the analog clock face (ticks, numbers).
void drawAnalogStatic(TFT_eSPI* canvas) {
  int centerX = SCREEN_HEIGHT / 2, centerY = SCREEN_HEIGHT / 2;
  int outerRadius = SCREEN_HEIGHT / 2, markRadius = outerRadius - 2;
  for (int i = 0; i < 12; i++) {
    float angle = (i * 30) * DEG_TO_RAD - PI / 2;
    if (i % 3 == 0) {
      float parallelOffset = 3.0, perpAngle = angle + PI / 2.0;
      for (float offset = -1.5; offset <= 1.51; offset += 1.0) {
        int x1 = centerX + cos(angle) * markRadius + cos(perpAngle) * (parallelOffset + offset), y1 = centerY + sin(angle) * markRadius + sin(perpAngle) * (parallelOffset + offset);
        int x2 = centerX + cos(angle) * (markRadius - (int)(outerRadius * 0.10)) + cos(perpAngle) * (parallelOffset + offset), y2 = centerY + sin(angle) * (markRadius - (int)(outerRadius * 0.10)) + sin(perpAngle) * (parallelOffset + offset);
        canvas->drawLine(x1, y1, x2, y2, CLOCK_COLOR_TICK);
        x1 = centerX + cos(angle) * markRadius - cos(perpAngle) * (parallelOffset + offset), y1 = centerY + sin(angle) * markRadius - sin(perpAngle) * (parallelOffset + offset);
        x2 = centerX + cos(angle) * (markRadius - (int)(outerRadius * 0.10)) - cos(perpAngle) * (parallelOffset + offset), y2 = centerY + sin(angle) * (markRadius - (int)(outerRadius * 0.10)) - sin(perpAngle) * (parallelOffset + offset);
        canvas->drawLine(x1, y1, x2, y2, CLOCK_COLOR_TICK);
      }
    } else {
      float perpAngle = angle + PI / 2.0;
      for (float offset = -0.5; offset <= 0.51; offset += 1.0) {
        int x1 = centerX + cos(angle) * markRadius + cos(perpAngle) * offset, y1 = centerY + sin(angle) * markRadius + sin(perpAngle) * offset;
        int x2 = centerX + cos(angle) * (markRadius - (int)(outerRadius * 0.05)) + cos(perpAngle) * offset, y2 = centerY + sin(angle) * (markRadius - (int)(outerRadius * 0.05)) + sin(perpAngle) * offset;
        canvas->drawLine(x1, y1, x2, y2, CLOCK_COLOR_TICK);
      }
    }
    int hour = (i == 0) ? 12 : i;
    char hourStr[3];
    snprintf(hourStr, sizeof(hourStr), "%d", hour);
    int textRadius = outerRadius - (int)(outerRadius * 0.25);
    int textX = centerX + cos(angle) * textRadius, textY = centerY + sin(angle) * textRadius;
    canvas->setTextColor(CLOCK_COLOR_NUMBER, COLOR_BACKGROUND);
    canvas->setTextSize(2);
    canvas->setTextDatum(MC_DATUM);
    canvas->drawString(hourStr, textX, textY);
  }
  for (int i = 0; i < 60; i++) {
    if (i % 5 != 0) {
      float angle = (i * 6) * DEG_TO_RAD - PI / 2;
      int x = centerX + cos(angle) * (markRadius - 1), y = centerY + sin(angle) * (markRadius - 1);
      canvas->fillCircle(x, y, 1, CLOCK_COLOR_TICK);
    }
  }
}

void updateAndDrawAnalogClock(bool animate = true) {
  static struct tm remoteTimeinfo;
  static unsigned long lastFullSync = 0;
  const WorldCity& currentCity = cities[currentCityIndex];

  // --- This time logic is needed by BOTH clocks, so it stays outside ---
  if (millis() - lastFullSync > 60000 || !animate) {
    getRemoteTime(&remoteTimeinfo, currentCity.posix_timezone);
    lastFullSync = millis();
  } else {
    remoteTimeinfo.tm_sec++;
    if (remoteTimeinfo.tm_sec >= 60) {
      remoteTimeinfo.tm_sec = 0;
      remoteTimeinfo.tm_min++;
      if (remoteTimeinfo.tm_min >= 60) {
        remoteTimeinfo.tm_min = 0;
        remoteTimeinfo.tm_hour++;
        if (remoteTimeinfo.tm_hour >= 24) {
          lastFullSync = 0;
        }
      }
    }
  }

  int centerX = SCREEN_HEIGHT / 2, centerY = SCREEN_HEIGHT / 2;

  // --- START OF CORRECTION ---
  //
  // Only attempt to draw the analog clock IF the sprite
  // was successfully created in RAM.
  //
  if (clockSprite.created()) {
    // This is the original code, now safely inside the check
    clockSprite.fillSprite(COLOR_BACKGROUND);

    drawSunMoonPhase(&clockSprite, centerX, centerY, &remoteTimeinfo, currentCity.isNorthern, currentCity.lat, currentCity.lon);

    drawAnalogStatic(&clockSprite);

    float secondAngle = (remoteTimeinfo.tm_sec * 6) - 90;
    float minuteAngle = (remoteTimeinfo.tm_min * 6 + (remoteTimeinfo.tm_sec / 10.0)) - 90;
    float hourAngle = ((remoteTimeinfo.tm_hour % 12) * 30 + (remoteTimeinfo.tm_min / 2.0)) - 90;
    drawHand(&clockSprite, centerX, centerY, hourAngle, 60, 8, 15, CLOCK_COLOR_HOUR_HAND);
    drawHand(&clockSprite, centerX, centerY, minuteAngle, 80, 6, 15, CLOCK_COLOR_MIN_HAND);
    drawStraightHandWithTail(&clockSprite, centerX, centerY, secondAngle, 95, 20, 2, CLOCK_COLOR_SEC_HAND);

    clockSprite.fillCircle(centerX, centerY, 4, CLOCK_COLOR_ACCENT);
    clockSprite.drawCircle(centerX, centerY, 4, COLOR_BACKGROUND);
    clockSprite.pushSprite(0, 0);

  } else {
    // This will run if the sprite creation failed.
    // We log the error to the Serial Monitor so you know it happened.
    Serial.println("[ERROR] updateAndDrawAnalogClock() called, but clockSprite was NOT created. Skipping analog draw.");

    // You could also fill the area black to hide any old graphics
    // tft.fillRect(0, 0, SCREEN_HEIGHT, SCREEN_HEIGHT, COLOR_BACKGROUND);
  }
  // --- END OF CORRECTION ---

  // The digital clock function is called regardless, since it
  // has its own sprite and will work fine.
  updateAndAnimateDigitalClock(remoteTimeinfo, animate);
}

void updateAndAnimateDigitalClock(struct tm timeinfo, bool animate = true) {

  if (!digitalClockSprite.created()) {
    return;
  }

  if (timeinfo.tm_year < (2023 - 1900)) {
    digitalClockSprite.fillSprite(COLOR_BACKGROUND);
    digitalClockSprite.setTextDatum(MC_DATUM);
    digitalClockSprite.setTextSize(1);
    digitalClockSprite.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    digitalClockSprite.drawString("Waiting for Time Sync...", DC_SPRITE_WIDTH / 2, DC_SPRITE_HEIGHT / 2);
    digitalClockSprite.pushSprite(SCREEN_HEIGHT, DC_SPRITE_Y_POS);
    memset(&prevClockState, 0, sizeof(DigitalClockState));
    return;
  }

  char buf[20];
  strftime(buf, sizeof(buf), "%A", &timeinfo);
  char currentDayOfWeek[20];
  strcpy(currentDayOfWeek, buf);
  strftime(buf, sizeof(buf), "%d-%B", &timeinfo);
  char currentDayMonth[20];
  strcpy(currentDayMonth, buf);
  strftime(buf, sizeof(buf), "%Y", &timeinfo);
  char currentYear[5];
  strcpy(currentYear, buf);

  strftime(buf, sizeof(buf), use24HourFormat ? "%H" : "%I", &timeinfo);
  char currentHour[3];
  strcpy(currentHour, buf);

  strftime(buf, sizeof(buf), "%M", &timeinfo);
  char currentMinute[3];
  strcpy(currentMinute, buf);

  char currentAmPm[3] = "";
  if (!use24HourFormat) {
    strftime(buf, sizeof(buf), "%p", &timeinfo);
    strcpy(currentAmPm, buf);
  }

  strftime(buf, sizeof(buf), "%S", &timeinfo);
  char currentSecond[3];
  strcpy(currentSecond, buf);

  bool dayOfWeekChanged = strcmp(prevClockState.prevDayOfWeek, currentDayOfWeek) != 0;
  bool dayMonthChanged = strcmp(prevClockState.prevDayMonth, currentDayMonth) != 0;
  bool yearChanged = strcmp(prevClockState.prevYear, currentYear) != 0;
  bool hourChanged = strcmp(prevClockState.prevHour, currentHour) != 0;
  bool minuteChanged = strcmp(prevClockState.prevMinute, currentMinute) != 0;
  bool amPmChanged = strcmp(prevClockState.prevAmPm, currentAmPm) != 0;
  bool secChanged = strcmp(prevClockState.prevSecond, currentSecond) != 0;

  if (!dayOfWeekChanged && !dayMonthChanged && !yearChanged && !hourChanged && !minuteChanged && !amPmChanged && !secChanged && animate) return;

  const int RIGHT_PANEL_WIDTH = DC_SPRITE_WIDTH;
  const int RIGHT_PANEL_CENTER_X = RIGHT_PANEL_WIDTH / 2;
  const int DATE_BLOCK_CENTER_Y = 73, SECONDS_BLOCK_CENTER_Y = 120;
  const int dateLine1_Y = DATE_BLOCK_CENTER_Y - 13, dateLine2_Y = DATE_BLOCK_CENTER_Y, dateLine3_Y = DATE_BLOCK_CENTER_Y + 13;

  const int timeLine1_Y = use24HourFormat ? 162 : 155;
  const int timeLine2_Y = 171;
  const int animationSteps = 8, animationDelay = 15, rollDistance = 10;

  if (animate && isTimeSynced) {
    for (int step = 0; step <= animationSteps; step++) {
      digitalClockSprite.fillSprite(COLOR_BACKGROUND);
      drawBeepIcon(&digitalClockSprite, BEEP_ICON_X, BEEP_ICON_Y - DC_SPRITE_Y_POS);

      int halfSteps = animationSteps / 2;
      int offset;
      char prevDigit[2], currentDigit[2];

      digitalClockSprite.setTextDatum(MC_DATUM);
      digitalClockSprite.setTextSize(1);
      digitalClockSprite.setTextColor(COLOR_HIGH_TEMP, COLOR_BACKGROUND);
      offset = 0;
      if (dayOfWeekChanged) {
        offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
        digitalClockSprite.drawString((step <= halfSteps) ? prevClockState.prevDayOfWeek : currentDayOfWeek, RIGHT_PANEL_CENTER_X, (dateLine1_Y - DC_SPRITE_Y_POS) + offset);
      } else {
        digitalClockSprite.drawString(currentDayOfWeek, RIGHT_PANEL_CENTER_X, dateLine1_Y - DC_SPRITE_Y_POS);
      }
      offset = 0;
      if (dayMonthChanged) {
        offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
        digitalClockSprite.drawString((step <= halfSteps) ? prevClockState.prevDayMonth : currentDayMonth, RIGHT_PANEL_CENTER_X, (dateLine2_Y - DC_SPRITE_Y_POS) + offset);
      } else {
        digitalClockSprite.drawString(currentDayMonth, RIGHT_PANEL_CENTER_X, dateLine2_Y - DC_SPRITE_Y_POS);
      }
      offset = 0;
      if (yearChanged) {
        offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
        digitalClockSprite.drawString((step <= halfSteps) ? prevClockState.prevYear : currentYear, RIGHT_PANEL_CENTER_X, (dateLine3_Y - DC_SPRITE_Y_POS) + offset);
      } else {
        digitalClockSprite.drawString(currentYear, RIGHT_PANEL_CENTER_X, dateLine3_Y - DC_SPRITE_Y_POS);
      }

      digitalClockSprite.setTextSize(2);
      int singleDigitWidth = digitalClockSprite.textWidth("0");
      int colonWidth = digitalClockSprite.textWidth(":");
      int timeBlockWidth = (singleDigitWidth * 4) + colonWidth;
      int timeStartX = RIGHT_PANEL_CENTER_X - (timeBlockWidth / 2);

      digitalClockSprite.setTextColor(CLOCK_COLOR_HOUR_HAND, COLOR_BACKGROUND);
      for (int i = 0; i < 2; i++) {
        int digitX = timeStartX + (i * singleDigitWidth) + (singleDigitWidth / 2);
        if (prevClockState.prevHour[i] != currentHour[i]) {
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevClockState.prevHour[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentHour[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, (timeLine1_Y - DC_SPRITE_Y_POS) + offset);
        } else {
          currentDigit[0] = currentHour[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString(currentDigit, digitX, timeLine1_Y - DC_SPRITE_Y_POS);
        }
      }

      digitalClockSprite.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
      int colonX = timeStartX + (singleDigitWidth * 2) + (colonWidth / 2);
      digitalClockSprite.drawString(":", colonX, timeLine1_Y - DC_SPRITE_Y_POS);

      digitalClockSprite.setTextColor(CLOCK_COLOR_MIN_HAND, COLOR_BACKGROUND);
      for (int i = 0; i < 2; i++) {
        int digitX = timeStartX + (singleDigitWidth * (i + 2)) + colonWidth + (singleDigitWidth / 2);
        if (prevClockState.prevMinute[i] != currentMinute[i]) {
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevClockState.prevMinute[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentMinute[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, (timeLine1_Y - DC_SPRITE_Y_POS) + offset);
        } else {
          currentDigit[0] = currentMinute[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString(currentDigit, digitX, timeLine1_Y - DC_SPRITE_Y_POS);
        }
      }

      if (!use24HourFormat) {
        digitalClockSprite.setTextDatum(TC_DATUM);
        digitalClockSprite.setTextColor(CLOCK_COLOR_ACCENT, COLOR_BACKGROUND);
        digitalClockSprite.setTextSize(2);
        offset = 0;
        if (amPmChanged) {
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          digitalClockSprite.drawString((step <= halfSteps) ? prevClockState.prevAmPm : currentAmPm, RIGHT_PANEL_CENTER_X, (timeLine2_Y - DC_SPRITE_Y_POS) + offset);
        } else {
          digitalClockSprite.drawString(currentAmPm, RIGHT_PANEL_CENTER_X, timeLine2_Y - DC_SPRITE_Y_POS);
        }
      }

      digitalClockSprite.setTextDatum(MC_DATUM);
      digitalClockSprite.setTextSize(3);
      int secSingleDigitWidth = digitalClockSprite.textWidth("0");
      int secBlockWidth = secSingleDigitWidth * 2;
      int secStartX = RIGHT_PANEL_CENTER_X - (secBlockWidth / 2);
      digitalClockSprite.setTextColor(CLOCK_COLOR_SEC_HAND, COLOR_BACKGROUND);
      for (int i = 0; i < 2; i++) {
        int digitX = secStartX + (i * secSingleDigitWidth) + (secSingleDigitWidth / 2);
        if (prevClockState.prevSecond[i] != currentSecond[i]) {
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevClockState.prevSecond[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentSecond[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, (SECONDS_BLOCK_CENTER_Y - DC_SPRITE_Y_POS) + offset);
        } else {
          currentDigit[0] = currentSecond[i];
          currentDigit[1] = '\0';
          digitalClockSprite.drawString(currentDigit, digitX, SECONDS_BLOCK_CENTER_Y - DC_SPRITE_Y_POS);
        }
      }

      digitalClockSprite.pushSprite(SCREEN_HEIGHT, DC_SPRITE_Y_POS);
      delay(animationDelay);
    }
  } else {
    // Non-animated redraw (fallback)
    digitalClockSprite.fillSprite(COLOR_BACKGROUND);
    drawBeepIcon(&digitalClockSprite, BEEP_ICON_X, BEEP_ICON_Y - DC_SPRITE_Y_POS);

    digitalClockSprite.setTextDatum(MC_DATUM);
    digitalClockSprite.setTextSize(1);
    digitalClockSprite.setTextColor(COLOR_HIGH_TEMP, COLOR_BACKGROUND);
    digitalClockSprite.drawString(currentDayOfWeek, RIGHT_PANEL_CENTER_X, dateLine1_Y - DC_SPRITE_Y_POS);
    digitalClockSprite.drawString(currentDayMonth, RIGHT_PANEL_CENTER_X, dateLine2_Y - DC_SPRITE_Y_POS);
    digitalClockSprite.drawString(currentYear, RIGHT_PANEL_CENTER_X, dateLine3_Y - DC_SPRITE_Y_POS);

    digitalClockSprite.setTextSize(2);
    int digitWidth = digitalClockSprite.textWidth("00");
    int colonWidth = digitalClockSprite.textWidth(":");
    int totalWidth = (digitWidth * 2) + colonWidth;
    int startX = RIGHT_PANEL_CENTER_X - (totalWidth / 2);
    digitalClockSprite.setTextColor(CLOCK_COLOR_HOUR_HAND, COLOR_BACKGROUND);
    digitalClockSprite.drawString(currentHour, startX + digitWidth / 2, timeLine1_Y - DC_SPRITE_Y_POS);
    digitalClockSprite.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    digitalClockSprite.drawString(":", startX + digitWidth + colonWidth / 2, timeLine1_Y - DC_SPRITE_Y_POS);
    digitalClockSprite.setTextColor(CLOCK_COLOR_MIN_HAND, COLOR_BACKGROUND);
    digitalClockSprite.drawString(currentMinute, startX + digitWidth + colonWidth + digitWidth / 2, timeLine1_Y - DC_SPRITE_Y_POS);

    if (!use24HourFormat) {
      digitalClockSprite.setTextDatum(TC_DATUM);
      digitalClockSprite.setTextColor(CLOCK_COLOR_ACCENT, COLOR_BACKGROUND);
      digitalClockSprite.drawString(currentAmPm, RIGHT_PANEL_CENTER_X, timeLine2_Y - DC_SPRITE_Y_POS);
    }

    digitalClockSprite.setTextDatum(MC_DATUM);
    digitalClockSprite.setTextSize(3);
    digitalClockSprite.setTextColor(CLOCK_COLOR_SEC_HAND, COLOR_BACKGROUND);
    digitalClockSprite.drawString(currentSecond, RIGHT_PANEL_CENTER_X, SECONDS_BLOCK_CENTER_Y - DC_SPRITE_Y_POS);

    digitalClockSprite.pushSprite(SCREEN_HEIGHT, DC_SPRITE_Y_POS);
  }

  strcpy(prevClockState.prevDayOfWeek, currentDayOfWeek);
  strcpy(prevClockState.prevDayMonth, currentDayMonth);
  strcpy(prevClockState.prevYear, currentYear);
  strcpy(prevClockState.prevHour, currentHour);
  strcpy(prevClockState.prevMinute, currentMinute);
  strcpy(prevClockState.prevAmPm, currentAmPm);
  strcpy(prevClockState.prevSecond, currentSecond);
}

void drawHeader() {
  tft.setTextSize(1);
  String dtText;
  struct tm timeinfo = {};
  if (getSafeLocalTime(&timeinfo, 5)) {
    char dtBuffer[40];
    strftime(dtBuffer, sizeof(dtBuffer), use24HourFormat ? "%a/%d%b/%Y %H:%M" : "%a/%d%b/%Y %I:%M %p", &timeinfo);
    dtText = String(dtBuffer);
    dtText.trim();
  } else {
    dtText = "--- --/---/---- --:-- --";
  }

  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_BACKGROUND);

  const int LOCATION_WIDTH_THRESHOLD = 170;
  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  int locationWidth = tft.textWidth(weatherData.locationName);

  if (locationWidth > LOCATION_WIDTH_THRESHOLD) {
    char line1[64] = { 0 };
    char line2[64] = { 0 };
    const char* comma_ptr = strchr(weatherData.locationName, ',');

    if (comma_ptr != nullptr) {  // Split at the comma
      int comma_pos = comma_ptr - weatherData.locationName;
      strncpy(line1, weatherData.locationName, comma_pos);
      // Skip the comma and the space after it
      const char* line2_start = comma_ptr + 1;
      while (*line2_start == ' ') {
        line2_start++;
      }
      strncpy(line2, line2_start, sizeof(line2) - 1);
    } else {  // No comma, split at a space near the middle
      int len = strlen(weatherData.locationName);
      int middle = len / 2;
      int split_pos = -1;
      // Find last space before middle
      for (int i = middle; i >= 0; i--) {
        if (weatherData.locationName[i] == ' ') {
          split_pos = i;
          break;
        }
      }
      // If not found, find first space after middle
      if (split_pos == -1) {
        for (int i = middle + 1; i < len; i++) {
          if (weatherData.locationName[i] == ' ') {
            split_pos = i;
            break;
          }
        }
      }

      if (split_pos != -1) {
        strncpy(line1, weatherData.locationName, split_pos);
        const char* line2_start = weatherData.locationName + split_pos + 1;
        strncpy(line2, line2_start, sizeof(line2) - 1);
      } else {  // No spaces at all, just use one line
        strncpy(line1, weatherData.locationName, sizeof(line1) - 1);
      }
    }

    tft.setTextDatum(TL_DATUM);
    tft.drawString(line1, LEFT_PADDING, 4);
    if (strlen(line2) > 0) {
      tft.drawString(line2, LEFT_PADDING, 16);
    }

  } else {
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherData.locationName, LEFT_PADDING, HEADER_HEIGHT / 2);
  }

  tft.setTextDatum(TR_DATUM);

  tft.setTextColor(COLOR_HIGH_TEMP);
  tft.drawString(dtText, SCREEN_WIDTH - LEFT_PADDING, 4);

  String apAddress = "IP: " + WiFi.localIP().toString();
  tft.setTextColor(COLOR_LOW_TEMP);
  tft.drawString(apAddress, SCREEN_WIDTH - LEFT_PADDING, 16);
}

// Draws the main weather information (description, icon, temperature).
void drawMainWeather() {
  const int LEFT_CENTER_X = (SCREEN_WIDTH - INFO_COLUMN_X) / 2 + 30;
  const int yPos_Desc = MAIN_CONTENT_Y + 2;
  const int clearAreaX = 0;
  const int clearAreaWidth = INFO_COLUMN_X;

  tft.setTextSize(TEXT_SIZE_DESCRIPTION);
  int16_t descTextHeight = 8 * TEXT_SIZE_DESCRIPTION;
  tft.fillRect(clearAreaX, yPos_Desc, clearAreaWidth, descTextHeight, COLOR_BACKGROUND);  // Clear only the text area
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_DESC_NIGHT, COLOR_BACKGROUND);
  tft.drawString(weatherData.weatherDescription, LEFT_CENTER_X, yPos_Desc);

  const int ICON_SIZE = 64, NEXT_ICON_SIZE = 32, ICON_PADDING = 8;
  const int PADDING_BELOW_DESC = 2, PADING_BELOW_ICON = 19, PADDING_BELOW_TEMP = 0;
  int yPos_Icon = yPos_Desc + descTextHeight + PADDING_BELOW_DESC;
  int yBaseline_Temp = yPos_Icon + ICON_SIZE + PADING_BELOW_ICON;
  int yBaseline_Feels = yBaseline_Temp + (8 * TEXT_SIZE_MAIN_TEMP) + PADDING_BELOW_TEMP;
  int clearHeight = (yBaseline_Feels + 8 * TEXT_SIZE_FEELS) - yPos_Icon;

  tft.fillRect(clearAreaX, yPos_Icon, clearAreaWidth, clearHeight, COLOR_BACKGROUND);  // Clear the icon and temp area

  const int iconDrawX = LEFT_CENTER_X - (ICON_SIZE / 2);

  String iconPath = selectedIconDirLarge + "/" + weatherData.currentIconCode + ".bmp";

  if (!drawBmpFromFile(iconPath, iconDrawX, yPos_Icon)) {
    // Fallback if the icon is missing from the SD card
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.setTextDatum(MC_DATUM);
    int iconCenterY = yPos_Icon + (ICON_SIZE / 2);
    tft.drawString("NO ICON", LEFT_CENTER_X, iconCenterY);
  }

  const int nextIconX = iconDrawX + ICON_SIZE + ICON_PADDING;
  const int nextIconY = yPos_Icon + (ICON_SIZE - NEXT_ICON_SIZE) / 2;
  String nextIconPath = selectedIconDirSmall + "/" + weatherData.next6HoursIconCode + ".bmp";
  drawBmpFromFile(nextIconPath, nextIconX, nextIconY);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_LOW_TEMP, COLOR_BACKGROUND);
  tft.setTextDatum(BC_DATUM);
  tft.drawString("+6hr", nextIconX + (NEXT_ICON_SIZE / 2), nextIconY - 2);

  drawTempUnit(LEFT_CENTER_X, yBaseline_Temp, String(weatherData.tempC), TEXT_SIZE_MAIN_TEMP, 1, COLOR_HIGH_TEMP, COLOR_BACKGROUND, -3, 1);

  if (strcmp(weatherData.feelsC, "--") != 0) {
    const int FEELS_GAP = 8;
    tft.setTextSize(TEXT_SIZE_FEELS);
    int labelW = tft.textWidth("Feels:");
    int valueW = getTempUnitWidth(String(weatherData.feelsC), TEXT_SIZE_FEELS, 0);
    int totalBlockWidth = labelW + FEELS_GAP + valueW;
    int blockStartX = LEFT_CENTER_X - (totalBlockWidth / 2 + 3);
    int valueCenterX = blockStartX + labelW + FEELS_GAP + (valueW / 2);
    int yTopLabel = yBaseline_Feels - (8 * TEXT_SIZE_FEELS);
    tft.setTextColor(COLOR_FEELS_LIKE, COLOR_BACKGROUND);
    tft.setCursor(blockStartX, yTopLabel);
    tft.print("Feels:");
    drawTempUnit(valueCenterX, yBaseline_Feels, String(weatherData.feelsC), TEXT_SIZE_FEELS, 0, COLOR_FEELS_LIKE, COLOR_BACKGROUND, -1, 1);
  }
}

// Draws the right-hand panel with detailed weather information.
void drawRightInfoPanel() {
  const int X_SHIFT = 10, GAP = 5;
  tft.setTextSize(TEXT_SIZE_INFO);

  const char* labelsAll[] = { "Sunrise:", "Sunset:", "Moonrise:", "Moonset:", "Humidity:", "Wind:", "Wind Dir:", "Pressure:", "Cloud Cover:", "UV Index:", "AQ Index:" };

  int16_t longestLabelWidth = 0;
  for (const char* s : labelsAll) {
    longestLabelWidth = max(longestLabelWidth, tft.textWidth(s));
  }
  int labelEndX = INFO_COLUMN_X + X_SHIFT + longestLabelWidth;
  int valueStartX = labelEndX + GAP;
  int y = INFO_COLUMN_Y_START;

  int total_rows_to_display = 9;
  int clear_height = total_rows_to_display * INFO_ROW_HEIGHT;
  tft.fillRect(INFO_COLUMN_X, INFO_COLUMN_Y_START - (INFO_ROW_HEIGHT / 2), SCREEN_WIDTH - INFO_COLUMN_X, clear_height, COLOR_BACKGROUND);

  auto drawInfoLine = [&](const char* label, const char* value) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COLOR_INFO_LABEL, COLOR_BACKGROUND);
    tft.drawString(label, labelEndX, y);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.drawString(value, valueStartX, y);

    y += INFO_ROW_HEIGHT;
  };

  auto drawInfoLine_s = [&](const char* label, const String& value) {
    drawInfoLine(label, value.c_str());
  };

  struct tm timeinfo;
  bool timeAvailable = getSafeLocalTime(&timeinfo, 5);
  int sunriseMinutes = parseTimeToMinutes(String(weatherData.sunriseTime));
  int sunsetMinutes = parseTimeToMinutes(String(weatherData.sunsetTime));
  int currentMinutes = timeAvailable ? (timeinfo.tm_hour * 60 + timeinfo.tm_min) : -1;

  bool isDayTime = true;
  if (sunriseMinutes != -1 && sunsetMinutes != -1 && currentMinutes != -1) {
    isDayTime = (currentMinutes >= sunriseMinutes && currentMinutes < sunsetMinutes);
  }

  if (isDayTime) {
    drawInfoLine("Sunrise:", weatherData.sunriseTime);
    drawInfoLine("Sunset:", weatherData.sunsetTime);
  } else {
    drawInfoLine("Moonrise:", weatherData.moonriseTime);
    drawInfoLine("Moonset:", weatherData.moonsetTime);
  }

  drawInfoLine_s("Humidity:", String(weatherData.humidity) + "%");

  if (useMph) {
    drawInfoLine_s("Wind:", strlen(weatherData.wind_mph) ? (String(weatherData.wind_mph) + " mph") : "--");
  } else {
    drawInfoLine_s("Wind:", strlen(weatherData.wind_kmh) ? (String(weatherData.wind_kmh) + " kmh") : "--");
  }

  drawInfoLine("Wind Dir:", weatherData.windDirection);

  switch (pressureUnitState) {
    case 1: drawInfoLine_s("Pressure:", strlen(weatherData.pressure_inhg) ? (String(weatherData.pressure_inhg) + " inHg") : "--"); break;
    case 2: drawInfoLine_s("Pressure:", strlen(weatherData.pressure_mmhg) ? (String(weatherData.pressure_mmhg) + " mmHg") : "--"); break;
    default: drawInfoLine_s("Pressure:", strlen(weatherData.pressure_hpa) ? (String(weatherData.pressure_hpa) + " hPa") : "--"); break;
  }

  drawInfoLine_s("Cloud Cover:", String(weatherData.cloudCover) + "%");
  drawInfoLine("UV Index:", weatherData.uvIndex);
  drawInfoLine("AQ Index:", weatherData.airQualityIndex);
}


// Draws the 7-day forecast strip at the bottom of the weather screen.
void drawWeeklyForecast() {
  tft.fillRect(0, FORECAST_STRIP_Y, SCREEN_WIDTH, FORECAST_STRIP_HEIGHT, COLOR_BACKGROUND);
  const int colW = SCREEN_WIDTH / 7;
  for (int i = 0; i < 7; i++) {
    const int x = i * colW, iconLeftX = x + (colW - 32) / 2, iconCenterX = iconLeftX + 16, h = 8 * TEXT_SIZE_FORECAST;
    int baseMin = SCREEN_HEIGHT - 4, baseMax = baseMin - h - 2;
    const int dateTextY = FORECAST_STRIP_Y + 2, dayTextY = dateTextY + h + 2;
    int spaceTopY = dayTextY + h - 3, spaceBottomY = baseMax - h;
    const int iconTopY = spaceTopY + ((spaceBottomY - spaceTopY - 32) / 2);
    String iconName = (i < weeklyForecast.size() && weeklyForecast[i].icon.length()) ? weeklyForecast[i].icon : "fair_day";
    String path = selectedIconDirSmall + "/" + iconName + ".bmp";
    if (!drawBmpFromFile(path, iconLeftX, iconTopY)) {
      tft.drawRect(iconLeftX, iconTopY, 32, 32, COLOR_DIVIDER_LINE);
    }
    String dayOfWeekStr = (i < weeklyForecast.size()) ? weeklyForecast[i].dayStr : "--";
    String dateMonthStr = "";
    if (i < weeklyForecast.size() && weeklyForecast[i].ymd.length() >= 10) {
      int monthNum = weeklyForecast[i].ymd.substring(5, 7).toInt();
      String dayNumStr = weeklyForecast[i].ymd.substring(8, 10);
      dateMonthStr = dayNumStr + getMonthShort(monthNum - 1);
    }
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    tft.drawString(dateMonthStr, iconCenterX, dateTextY);
    tft.drawString(dayOfWeekStr, iconCenterX, dayTextY);
    String tMaxC = (i < weeklyForecast.size() && !isnan(weeklyForecast[i].tmax)) ? String(weeklyForecast[i].tmax, 1) : "--";
    String tMinC = (i < weeklyForecast.size() && !isnan(weeklyForecast[i].tmin)) ? String(weeklyForecast[i].tmin, 1) : "--";
    drawTempUnit(iconCenterX, baseMax, tMaxC, 1, 0, COLOR_HIGH_TEMP, COLOR_BACKGROUND, -1, 1);
    drawTempUnit(iconCenterX, baseMin, tMinC, 1, 0, COLOR_LOW_TEMP, COLOR_BACKGROUND, -1, 1);
  }
}

// Draws the thermometer icon, which acts as a button to toggle temperature units.
void drawThermoIcon() {
  drawBmpFromFile(THERMO_ICON_PATH, THERMO_ICON_X, THERMO_ICON_Y);
}

// Draws the bell icon for the hourly chime feature.
void drawBeepIcon(TFT_eSPI* canvas, int x, int y) {
  int r = BEEP_ICON_R;
  uint16_t bellColor = isBlinking ? CLOCK_COLOR_ACCENT : COLOR_TEXT;

  canvas->fillRect(x - r - 6, y - r - 6, 2 * r + 12, 2 * r + 12, COLOR_BACKGROUND);

  if (showBellTouchCircle) {
    canvas->drawCircle(x, y - 2, r + 4, 0x001F);
    canvas->drawCircle(x, y - 2, r + 5, 0x001F);
  }

  canvas->fillRoundRect(x - r / 2, y - r, r, r, 2, bellColor);
  canvas->fillTriangle(x - r, y, x + r, y, x, y - r, bellColor);
  canvas->fillCircle(x, y + 2, 2, isBlinking ? COLOR_BACKGROUND : CLOCK_COLOR_ACCENT);

  if (!hourlyBeepEnabled) {
    int start_margin = 2, end_margin = 5;
    canvas->drawLine(x - r + start_margin, y - r + start_margin, x + r - end_margin, y + r - end_margin, COLOR_HIGH_TEMP);
    canvas->drawLine(x - r + start_margin + 1, y - r + start_margin, x + r - end_margin + 1, y + r - end_margin, COLOR_HIGH_TEMP);
  }
}

// Main function to draw the entire weather screen.
void drawWeatherScreen() {
  tft.fillScreen(COLOR_BACKGROUND);
  drawHeader();
  drawRightInfoPanel();
  drawMainWeather();
  drawWeeklyForecast();
  drawThermoIcon();
}

void drawClockPage() {
  tft.fillScreen(COLOR_BACKGROUND);

  // --- START OF ROBUST SOLUTION ---
  // Attempt to create sprites if they don't exist
  // and add checks to see if the creation succeeded.

  if (!clockSprite.created()) {
    clockSprite.setColorDepth(8);
    clockSprite.createSprite(SCREEN_HEIGHT, SCREEN_HEIGHT);
    // CHECK IF IT WORKED
    if (!clockSprite.created()) {
      Serial.println("!!! FATAL: Failed to create clockSprite (58KB)! Not enough RAM. !!!");
    }
  }

  if (!beepIconSprite.created()) {
    beepIconSprite.setColorDepth(8);
    beepIconSprite.createSprite(30, 30);
    // (This one is tiny and will almost never fail, but we check anyway)
    if (!beepIconSprite.created()) {
      Serial.println("!!! WARNING: Failed to create beepIconSprite. !!!");
    }
  }

  if (!digitalClockSprite.created()) {
    digitalClockSprite.setColorDepth(8);
    digitalClockSprite.createSprite(DC_SPRITE_WIDTH, DC_SPRITE_HEIGHT);
    // CHECK IF IT WORKED
    if (!digitalClockSprite.created()) {
      Serial.println("!!! FATAL: Failed to create digitalClockSprite (15KB)! Not enough RAM. !!!");
    }
  }
  // --- END OF ROBUST SOLUTION ---

  tft.fillRect(SCREEN_HEIGHT, 0, SCREEN_WIDTH - SCREEN_HEIGHT, SCREEN_HEIGHT, COLOR_BACKGROUND);
  const int RIGHT_PANEL_X = SCREEN_HEIGHT;
  const int PADDING_LEFT = 5;
  const int FLAG_TEXT_GAP = 3;
  const int flagWidth = 32, flagHeight = 32;
  const int contentCenterY = 215;
  const int flagScreenX = RIGHT_PANEL_X + PADDING_LEFT;
  const int flagScreenY = contentCenterY - (flagHeight / 2);

  drawBmpFromFile(cities[currentCityIndex].flagBmpPath, flagScreenX, flagScreenY);

  tft.setTextDatum(ML_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  tft.drawString(cities[currentCityIndex].cityCode, flagScreenX + flagWidth + FLAG_TEXT_GAP, contentCenterY);

  memset(&prevClockState, 0, sizeof(DigitalClockState));

  // Now, we call the update functions (which are now protected by Fix 1)
  updateAndDrawAnalogClock(false);
}

// Helper function to get the number of days in a given month and year.
int daysInMonth(int year, int month) {
  if (month == 2) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 29 : 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }
  return 31;
}

// Draws the calendar page.
void drawCalendarPage(struct tm timeinfo) {
  tft.fillScreen(COLOR_BACKGROUND);
  char header[32];
  strftime(header, sizeof(header), "%B %Y", &calendarTime);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_HIGH_TEMP, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.drawString(header, SCREEN_WIDTH / 2, 15);

  int arrowY = 23;
  int arrowSize = 6;
  int leftArrowX = 40;
  int rightArrowX = SCREEN_WIDTH - 40;
  tft.fillTriangle(leftArrowX - arrowSize, arrowY, leftArrowX + arrowSize, arrowY - arrowSize, leftArrowX + arrowSize, arrowY + arrowSize, COLOR_ARROW);
  tft.fillTriangle(rightArrowX + arrowSize, arrowY, rightArrowX - arrowSize, arrowY - arrowSize, rightArrowX - arrowSize, arrowY + arrowSize, COLOR_ARROW);

  if (calendarTime.tm_mon != timeinfo.tm_mon || calendarTime.tm_year != timeinfo.tm_year) {
    tft.setTextSize(1);
    tft.setTextColor(COLOR_LOW_TEMP);
    tft.drawRect(SCREEN_WIDTH / 2 - 25, 38, 50, 16, COLOR_DIVIDER_LINE);
    tft.drawString("Today", SCREEN_WIDTH / 2, 42);
  }

  const int calX = 15, calY = 65;
  const int cellW = (SCREEN_WIDTH - (2 * calX)) / 7;
  const int cellH = (SCREEN_HEIGHT - calY - 15) / 6;
  const char* dayNames[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(CLOCK_COLOR_ACCENT, COLOR_BACKGROUND);
  tft.setTextSize(2);
  for (int i = 0; i < 7; i++) {
    tft.drawString(dayNames[i], calX + (i * cellW) + (cellW / 2), calY - 5);
  }

  int currentYear = calendarTime.tm_year + 1900;
  int currentMonth = calendarTime.tm_mon + 1;
  int numDays = daysInMonth(currentYear, currentMonth);

  struct tm firstDayOfMonth = { 0 };
  firstDayOfMonth.tm_year = currentYear - 1900;
  firstDayOfMonth.tm_mon = currentMonth - 1;
  firstDayOfMonth.tm_mday = 1;
  mktime(&firstDayOfMonth);
  int startDayOfWeek = firstDayOfMonth.tm_wday;

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  int day = 1;
  for (int row = 0; row < 6 && day <= numDays; row++) {
    for (int col = 0; col < 7 && day <= numDays; col++) {
      if (row == 0 && col < startDayOfWeek) {
        continue;
      }

      int cellX_center = calX + (col * cellW) + (cellW / 2);
      int cellY_center = calY + 30 + (row * cellH);

      int boxWidth = cellW - 6;
      int boxHeight = cellH - 6;
      int cornerRadius = 5;
      int boxX_topLeft = cellX_center - (boxWidth / 2);
      int boxY_topLeft = cellY_center - (boxHeight / 2);

      if (day == timeinfo.tm_mday && calendarTime.tm_mon == timeinfo.tm_mon && calendarTime.tm_year == timeinfo.tm_year) {
        tft.fillRoundRect(boxX_topLeft, boxY_topLeft, boxWidth, boxHeight, cornerRadius, COLOR_LOW_TEMP);
        tft.setTextColor(COLOR_BACKGROUND, COLOR_LOW_TEMP);
      } else {
        tft.drawRoundRect(boxX_topLeft, boxY_topLeft, boxWidth, boxHeight, cornerRadius, COLOR_DIVIDER_LINE);
        tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
      }
      tft.drawString(String(day), cellX_center, cellY_center);
      day++;
    }
  }
}

void drawCityListItems() {
  tft.fillRect(20, 50, SCREEN_WIDTH - 40, 175, COLOR_BACKGROUND);

  for (int i = 0; i < ITEMS_PER_PAGE; i++) {
    int cityIndex = listScrollOffset + i;
    if (cityIndex >= numCities) break;
    drawCityListItem(cityIndex, cityIndex == focusedCityIndex);
  }
}

void drawCityListItem(int index, bool isHighlighted) {
  int y_pos = 55;
  int itemHeight = 32;
  int itemGap = 2;
  int itemLeftX = 25;
  int itemWidth = SCREEN_WIDTH - 50;
  int list_y_pos = y_pos + (index - listScrollOffset) * (itemHeight + itemGap);

  if (index < listScrollOffset || index >= listScrollOffset + ITEMS_PER_PAGE) {
    return;
  }

  uint16_t bgColor = isHighlighted ? CLOCK_COLOR_SEC_HAND : COLOR_BACKGROUND;
  tft.fillRoundRect(itemLeftX, list_y_pos, itemWidth, itemHeight, 5, bgColor);
  drawBmpFromFile(cities[index].flagBmpPath, 35, list_y_pos);
  String displayText = String(cities[index].cityCode) + " " + String(cities[index].utcOffset);
  tft.setTextSize(2);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COLOR_TEXT, bgColor);
  tft.drawString(displayText, 85, list_y_pos + 16);
}

void drawCitySelectionPopup() {
  tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_DARKGREY);
  tft.fillRoundRect(15, 8, SCREEN_WIDTH - 30, SCREEN_HEIGHT - 16, 8, COLOR_BACKGROUND);
  tft.drawRoundRect(15, 8, SCREEN_WIDTH - 30, SCREEN_HEIGHT - 16, 8, COLOR_DIVIDER_LINE);

  const int btnY = 14;
  const int btnH = 30;
  const int btnW = 80;
  const int btnGap = 5;
  const int totalGroupWidth = (btnW * 3) + (btnGap * 2);
  const int btnX_Up = (SCREEN_WIDTH - totalGroupWidth) / 2;
  const int btnX_Down = btnX_Up + btnW + btnGap;
  const int btnX_Set = btnX_Down + btnW + btnGap;

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);

  tft.fillRoundRect(btnX_Up, btnY, btnW, btnH, 5, CLOCK_COLOR_HOUR_HAND);
  tft.setTextColor(COLOR_TEXT, CLOCK_COLOR_HOUR_HAND);
  tft.drawString("Up", btnX_Up + btnW / 2, btnY + btnH / 2 + 1);

  tft.fillRoundRect(btnX_Down, btnY, btnW, btnH, 5, CLOCK_COLOR_MIN_HAND);
  tft.setTextColor(COLOR_TEXT, CLOCK_COLOR_MIN_HAND);
  tft.drawString("Down", btnX_Down + btnW / 2, btnY + btnH / 2 + 1);

  tft.fillRoundRect(btnX_Set, btnY, btnW, btnH, 5, CLOCK_COLOR_ACCENT);
  tft.setTextColor(COLOR_BACKGROUND, CLOCK_COLOR_ACCENT);
  tft.drawString("Set", btnX_Set + btnW / 2, btnY + btnH / 2 + 1);

  drawCityListItems();
}

void confirmCitySelection() {
  currentCityIndex = focusedCityIndex;
  prefs.begin("weather-app", false);
  prefs.putInt("cityIndex", currentCityIndex);
  prefs.end();
  Serial.printf("New world clock city set: %s\n", cities[currentCityIndex].cityCode);
  cityListVisible = false;
  needsRedraw = true;
  lastUiInteractionTime = millis();
}


void generateFlagReport() {
  Serial.println("========== FLAG FILE REPORT ==========");
  if (!isSdCardMounted) {
    Serial.println("[SD Card] Mounted: No");
    Serial.println("=====================================");
    return;
  }
  Serial.println("[SD Card] Mounted: Yes");

  std::vector<String> uniqueFlagPaths;

  // --- THIS IS THE LOOP THAT WAS MISSING ---
  for (int i = 0; i < numCities; i++) {
    bool found = false;
    String currentPath = cities[i].flagBmpPath;
    for (const String& path : uniqueFlagPaths) {
      if (path == currentPath) {
        found = true;
        break;
      }
    }
    if (!found) {
      uniqueFlagPaths.push_back(currentPath);
    }
  }
  // --- END OF MISSING LOOP ---

  std::sort(uniqueFlagPaths.begin(), uniqueFlagPaths.end());

  for (const String& flagPath : uniqueFlagPaths) {
    Serial.printf("[Flag Check] Path: %-18s -> [%s]\n",
                  flagPath.c_str(),
                  fileExistsOnSD(flagPath) ? "OK" : "MISSING");
  }
  Serial.println("=====================================");
}

// Generates a report of required and found icon files.
void generateIconReport() {
  Serial.println("========== ICON FILE REPORT ==========");
  if (!isSdCardMounted) {
    Serial.println("[SD Card] Mounted: No\n=====================================");
    return;
  }
  Serial.println("[SD Card] Mounted: Yes");
  Serial.println("[Icon Dirs] Large: " + selectedIconDirLarge);
  Serial.println("[Icon Dirs] Small: " + selectedIconDirSmall);
  String mainIconPath = selectedIconDirLarge + "/" + weatherData.currentIconCode + ".bmp";

  Serial.printf("[Main Icon] Symbol: %-18s -> Path: %s [%s]\n", weatherData.currentIconCode, mainIconPath.c_str(), fileExistsOnSD(mainIconPath) ? "OK" : "MISSING");

  String next6hrIconPath = selectedIconDirSmall + "/" + weatherData.next6HoursIconCode + ".bmp";

  Serial.printf("[+6hr Icon] Symbol: %-18s -> Path: %s [%s]\n", weatherData.next6HoursIconCode, next6hrIconPath.c_str(), fileExistsOnSD(next6hrIconPath) ? "OK" : "MISSING");

  Serial.println("--- 7-Day Forecast Icons ---");
  for (size_t i = 0; i < weeklyForecast.size(); i++) {
    if (weeklyForecast[i].icon.isEmpty()) continue;
    String weekIconPath = selectedIconDirSmall + "/" + weeklyForecast[i].icon + ".bmp";
    Serial.printf("[Forecast %d] %s Symbol: %-15s -> Path: %s [%s]\n", i, weeklyForecast[i].ymd.c_str(), weeklyForecast[i].icon.c_str(), weekIconPath.c_str(), fileExistsOnSD(weekIconPath) ? "OK" : "MISSING");
  }

  Serial.println("--- Hourly Forecast Icons ---");
  if (hourlyForecast.empty()) {
    Serial.println("[Hourly Icons] No data available to check.");
  } else {
    for (size_t i = 0; i < hourlyForecast.size(); i++) {
      if (i % 2 == 0) {
        if (hourlyForecast[i].icon.isEmpty()) continue;
        String hourIconPath = selectedIconDirSmall + "/" + hourlyForecast[i].icon + ".bmp";
        Serial.printf("[Hourly %02d:00] Symbol: %-15s -> Path: %s [%s]\n",
                      hourlyForecast[i].hour,
                      hourlyForecast[i].icon.c_str(),
                      hourIconPath.c_str(),
                      fileExistsOnSD(hourIconPath) ? "OK" : "MISSING");
      }
    }
  }
  Serial.println("=====================================");
}


// Tries to sync with an NTP server until successful or after 50 attempts.
bool ensureTimeSynced() {
  isTimeSynced = false;
  struct tm timeinfo = {};
  for (int i = 0; i < 50; i++) {
    if (getSafeLocalTime(&timeinfo, 50)) {
      if (timeinfo.tm_year > (2023 - 1900)) {
        isTimeSynced = true;
        return true;
      }
    }
    delay(100);
  }
  return false;
}

// Fetches the location name from the OpenStreetMap API.
void fetchLocationName() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Location] Cannot fetch, WiFi not connected.");
    return;
  }

  for (int i = 1; i <= MAX_NETWORK_RETRIES; i++) {
    std::unique_ptr<HTTPClient> http(new HTTPClient);
    WiFiClientSecure client;
    client.setInsecure();
    http->setTimeout(10000);

    String url = "https://nominatim.openstreetmap.org/reverse?format=json&lat=" + latitude + "&lon=" + longitude + "&zoom=17&accept-language=en";

    Serial.printf("[Location] Fetching data... Attempt %d/%d\n", i, MAX_NETWORK_RETRIES);
    Serial.println("Requesting Location URL: " + url);

    if (!http->begin(client, url)) {
      Serial.println("[Location] Failed to begin HTTP client.");
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
      continue;
    }

    http->addHeader("User-Agent", API_USER_AGENT);
    int httpCode = http->GET();

    if (httpCode <= 0) {
      Serial.printf("[Location] HTTP request failed, error: %s\n", http->errorToString(httpCode).c_str());
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
      continue;
    }

    if (httpCode == HTTP_CODE_OK) {
      String payload;
      try {
        payload = http->getString();
      } catch (...) {
        Serial.println("[Location] Exception while reading HTTP stream.");
        http->end();
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      }
      http->end();

      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.printf("[Location] JSON Deserialization failed: %s\n", error.c_str());
        strncpy(weatherData.locationName, "Geocode Error", sizeof(weatherData.locationName));
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      }

      if (doc.containsKey("address")) {
        JsonObject addr = doc["address"];
        const char* keys[] = { "neighbourhood", "hamlet", "suburb", "village", "town", "city", "county" };
        String bestName = "";
        for (auto key : keys) {
          if (addr.containsKey(key)) {
            bestName = addr[key].as<String>();
            if (bestName.length() > 0) break;
          }
        }
        String country = addr["country"] | "";
        String finalLocationName = bestName.length() > 0 ? (bestName + ", " + country) : country;

        strncpy(weatherData.locationName, finalLocationName.c_str(), sizeof(weatherData.locationName) - 1);
        weatherData.locationName[sizeof(weatherData.locationName) - 1] = '\0';

        Serial.printf("[Location] Success! Location is %s\n", weatherData.locationName);
        return;
      } else {
        Serial.println("[Location] JSON response did not contain 'address' key.");
        strncpy(weatherData.locationName, "Unknown Location", sizeof(weatherData.locationName));
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      }
    } else {
      Serial.printf("[Location] HTTP request failed with code: %d\n", httpCode);
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
    }
  }
  Serial.println("[Location] Failed to fetch location after all retries.");
}


// Fetches the Air Quality Index data from the Open-Meteo API.
void fetchAirQualityData() {
  strncpy(weatherData.airQualityIndex, "N/A", sizeof(weatherData.airQualityIndex));

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[AQI] Cannot fetch, WiFi not connected.");
    return;
  }

  for (int i = 1; i <= MAX_NETWORK_RETRIES; i++) {
    std::unique_ptr<HTTPClient> http(new HTTPClient);
    WiFiClientSecure client;
    client.setInsecure();
    http->setTimeout(10000);

    String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + latitude + "&longitude=" + longitude + "&current=us_aqi";

    Serial.printf("[AQI] Fetching data... Attempt %d/%d\n", i, MAX_NETWORK_RETRIES);
    Serial.println("Requesting AQI URL: " + url);

    if (!http->begin(client, url)) {
      Serial.println("[AQI] Failed to begin HTTP client.");
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
      continue;
    }

    http->addHeader("User-Agent", API_USER_AGENT);
    int httpCode = http->GET();

    if (httpCode <= 0) {
      Serial.printf("[AQI] HTTP request failed, error: %s\n", http->errorToString(httpCode).c_str());
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
      continue;
    }

    if (httpCode == HTTP_CODE_OK) {
      String payload;
      try {
        payload = http->getString();
      } catch (...) {
        Serial.println("[AQI] Exception while reading HTTP stream.");
        http->end();
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      }
      http->end();

      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.printf("[AQI] JSON Deserialization failed: %s\n", error.c_str());
        strncpy(weatherData.airQualityIndex, "Error", sizeof(weatherData.airQualityIndex));
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      } else if (doc["current"].containsKey("us_aqi")) {
        snprintf(weatherData.airQualityIndex, sizeof(weatherData.airQualityIndex), "%d", doc["current"]["us_aqi"].as<int>());
        Serial.printf("[AQI] Success! AQI is %s\n", weatherData.airQualityIndex);
        return;
      } else {
        Serial.println("[AQI] JSON response did not contain 'us_aqi' key.");
        strncpy(weatherData.airQualityIndex, "--", sizeof(weatherData.airQualityIndex));
        if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
        continue;
      }
    } else {
      Serial.printf("[AQI] HTTP request failed with code: %d\n", httpCode);
      http->end();
      if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
    }
  }
  Serial.println("[AQI] Failed to fetch AQI after all retries.");
}

void parseWeeklyForecast(JsonArray timeseries, bool isCurrentlyDay) {
  weeklyForecast.clear();
  auto findOrAddDay = [&](const String& ymd) {
    for (size_t i = 0; i < weeklyForecast.size(); ++i) {
      if (weeklyForecast[i].ymd == ymd) return (int)i;
    }
    DayForecast d;
    d.ymd = ymd;
    weeklyForecast.push_back(d);
    return (int)weeklyForecast.size() - 1;
  };
  for (JsonObject entry : timeseries) {
    String iso = entry["time"] | "";
    if (iso.isEmpty()) continue;
    int dayIndex = findOrAddDay(iso.substring(0, 10));
    float temp = entry["data"]["instant"]["details"]["air_temperature"] | NAN;
    if (!isnan(temp)) {
      if (isnan(weeklyForecast[dayIndex].tmin) || temp < weeklyForecast[dayIndex].tmin) weeklyForecast[dayIndex].tmin = temp;
      if (isnan(weeklyForecast[dayIndex].tmax) || temp > weeklyForecast[dayIndex].tmax) weeklyForecast[dayIndex].tmax = temp;
    }
    int score = abs(iso.substring(11, 13).toInt() - 12);
    const char* sym = entry["data"]["next_6_hours"]["summary"]["symbol_code"];
    if (sym && score < weeklyForecast[dayIndex].hourScore) {
      weeklyForecast[dayIndex].hourScore = score;

      String iconSymbol = normalizeWeatherSymbol(sym);

      // First, remove any existing suffix to get a clean base symbol name
      if (iconSymbol.endsWith("_day")) {
        iconSymbol.remove(iconSymbol.length() - 4);
      } else if (iconSymbol.endsWith("_night")) {
        iconSymbol.remove(iconSymbol.length() - 6);
      }

      // Now, append the correct suffix based on the current time of day.
      // This ensures icons like "cloudy" become "cloudy_day" or "cloudy_night".
      if (isCurrentlyDay) {
        iconSymbol += "_day";
      } else {
        iconSymbol += "_night";
      }

      weeklyForecast[dayIndex].icon = iconSymbol;
    }
  }
  for (auto& day : weeklyForecast) {
    struct tm timeinfo = {};
    sscanf(day.ymd.c_str(), "%d-%d-%d", &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday);
    timeinfo.tm_year -= 1900;
    timeinfo.tm_mon -= 1;
    mktime(&timeinfo);
    day.dayStr = getDayShort(timeinfo.tm_wday);
  }
  std::sort(weeklyForecast.begin(), weeklyForecast.end(), [](const DayForecast& a, const DayForecast& b) {
    return a.ymd < b.ymd;
  });
  struct tm timeinfo = {};
  if (getSafeLocalTime(&timeinfo, 5)) {
    char buffer[11];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    String todayLocal = buffer;
    weeklyForecast.erase(std::remove_if(weeklyForecast.begin(), weeklyForecast.end(), [&](const DayForecast& d) {
                           return d.ymd == todayLocal;
                         }),
                         weeklyForecast.end());
  }
  if (weeklyForecast.size() > 7) weeklyForecast.resize(7);
}


// Updates the sunrise, sunset, moonrise, and moonset times.
void updateSunAndMoonTimes() {
  time_t now;
  time(&now);
  if (now < 1672531200) {
    Serial.println("[Sun/Moon] Time not synced, cannot calculate times.");
    strncpy(weatherData.sunriseTime, "--:--", sizeof(weatherData.sunriseTime));
    strncpy(weatherData.sunsetTime, "--:--", sizeof(weatherData.sunsetTime));
    strncpy(weatherData.moonriseTime, "--:--", sizeof(weatherData.moonriseTime));
    strncpy(weatherData.moonsetTime, "--:--", sizeof(weatherData.moonsetTime));
    return;
  }

  float lat = latitude.toFloat();
  float lon = longitude.toFloat();

  SunRise sun;
  sun.calculate(lat, lon, now);
  time_t sunrise_t_utc = sun.riseTime;
  time_t sunset_t_utc = sun.setTime;

  String tempSunrise = formatUTCTimeToLocalString(sunrise_t_utc, system_tz_string.c_str());
  strncpy(weatherData.sunriseTime, tempSunrise.c_str(), sizeof(weatherData.sunriseTime) - 1);
  weatherData.sunriseTime[sizeof(weatherData.sunriseTime) - 1] = '\0';

  String tempSunset = formatUTCTimeToLocalString(sunset_t_utc, system_tz_string.c_str());
  strncpy(weatherData.sunsetTime, tempSunset.c_str(), sizeof(weatherData.sunsetTime) - 1);
  weatherData.sunsetTime[sizeof(weatherData.sunsetTime) - 1] = '\0';

  MoonRise moon;
  moon.calculate(lat, lon, now);
  time_t moonrise_t_utc = moon.riseTime;
  time_t moonset_t_utc = moon.setTime;

  String tempMoonrise = formatUTCTimeToLocalString(moonrise_t_utc, system_tz_string.c_str());
  strncpy(weatherData.moonriseTime, tempMoonrise.c_str(), sizeof(weatherData.moonriseTime) - 1);
  weatherData.moonriseTime[sizeof(weatherData.moonriseTime) - 1] = '\0';

  String tempMoonset = formatUTCTimeToLocalString(moonset_t_utc, system_tz_string.c_str());
  strncpy(weatherData.moonsetTime, tempMoonset.c_str(), sizeof(weatherData.moonsetTime) - 1);
  weatherData.moonsetTime[sizeof(weatherData.moonsetTime) - 1] = '\0';

  Serial.println("[Sun/Moon] Updated times using timezone-safe formatting:");
  Serial.println("  - Home TZ for Conversion: " + system_tz_string);

  Serial.printf("  - Sunrise: %s, Sunset: %s\n", weatherData.sunriseTime, weatherData.sunsetTime);
  Serial.printf("  - Moonrise: %s, Moonset: %s\n", weatherData.moonriseTime, weatherData.moonsetTime);
}


// Calculates the local timezone offset in hours for a given date, accounting for DST.
bool tzOffsetHoursForDate(int y, int m, int d, float& out) {
  struct tm lt = {};
  lt.tm_year = y - 1900;
  lt.tm_mon = m - 1;
  lt.tm_mday = d;
  lt.tm_hour = 12;
  lt.tm_isdst = -1;
  time_t t = mktime(&lt);
  if (t == -1) return false;
  struct tm g = {};
  gmtime_r(&t, &g);
  int minsLoc = lt.tm_hour * 60 + lt.tm_min;
  int minsUtc = g.tm_hour * 60 + g.tm_min;
  int dayDelta = lt.tm_yday - g.tm_yday;
  int diffMin = (minsLoc - minsUtc) + dayDelta * 1440;
  out = diffMin / 60.0f;
  return true;
}


// Custom implementation of timegm() to convert a UTC time structure to a Unix timestamp.
time_t timegm_custom(struct tm* tm) {
  static const int month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  long days = 0;
  int year;

  for (year = 1970; year < tm->tm_year; ++year) {
    days += 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  }
  for (int month = 0; month < tm->tm_mon; ++month) {
    days += month_days[month];
  }
  if (tm->tm_mon > 1 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
    days += 1;
  }
  days += tm->tm_mday - 1;

  return ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec;
}

// Converts an ISO 8601 timestamp string to a Unix epoch time.
time_t iso8601toEpoch(const char* iso8601) {
  struct tm t;
  if (strptime(iso8601, "%Y-%m-%dT%H:%M:%SZ", &t) == NULL) {
    return 0;
  }
  return timegm_custom(&t);
}

// Parses the hourly forecast data from the main weather API JSON response.
void parseHourlyForecast(JsonArray timeseries) {
  hourlyForecast.clear();
  int count = 0;

  char* original_tz = getenv("TZ");
  setenv("TZ", system_tz_string.c_str(), 1);
  tzset();

  for (JsonObject entry : timeseries) {
    if (count >= 24) break;

    String iso = entry["time"] | "";
    if (iso.isEmpty()) continue;

    time_t utc_time = iso8601toEpoch(iso.c_str());
    if (utc_time == 0) continue;

    struct tm local_tm;
    localtime_r(&utc_time, &local_tm);

    HourlyDataPoint point;
    point.hour = local_tm.tm_hour;
    point.temperature = entry["data"]["instant"]["details"]["air_temperature"] | NAN;
    const char* symbolCode = entry["data"]["next_1_hours"]["summary"]["symbol_code"];
    point.icon = normalizeWeatherSymbol(symbolCode ? symbolCode : "fair_day");

    if (!isnan(point.temperature)) {
      hourlyForecast.push_back(point);
      count++;
    }
  }

  if (original_tz) {
    setenv("TZ", original_tz, 1);
  } else {
    unsetenv("TZ");
  }
  tzset();

  Serial.printf("[Hourly] Parsed %d hours of forecast data.\n", hourlyForecast.size());
}

bool fetchWeatherDataOnce(int attempt, int maxAttempts) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Weather] Cannot fetch, WiFi not connected.");
    isFetchingWeather = false;
    return false;
  }

  isFetchingWeather = true;

  fetchLocationName();
  updateSunAndMoonTimes();
  fetchAirQualityData();

  String url = "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=" + latitude + "&lon=" + longitude;

  std::unique_ptr<HTTPClient> http(new HTTPClient);
  WiFiClientSecure client;
  client.setInsecure();
  http->setTimeout(10000);

  Serial.printf("[Weather] Fetching data... Attempt %d/%d\n", attempt, maxAttempts);
  Serial.println("Requesting Weather URL: " + url);

  if (!http->begin(client, url)) {
    Serial.println("[Weather] Failed to initialize HTTP client.");
    http->end();
    isFetchingWeather = false;
    return false;
  }

  http->addHeader("User-Agent", API_USER_AGENT);
  int httpCode = http->GET();

  if (httpCode <= 0) {
    Serial.printf("[Weather] HTTP request failed, error: %s\n", http->errorToString(httpCode).c_str());
    http->end();
    isFetchingWeather = false;
    return false;
  }

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http->getSize();
    if (contentLength <= 0) {
      Serial.println("[Weather] Error: Content-Length header missing or invalid.");
      http->end();
      isFetchingWeather = false;
      return false;
    }
    Serial.printf("[Weather] Server reported Content-Length: %d bytes.\n", contentLength);

    File weatherFile = SD.open("/weather.json", FILE_WRITE);
    if (!weatherFile) {
      Serial.println("[Weather] Failed to open /weather.json for writing");
      http->end();
      isFetchingWeather = false;
      return false;
    }

    WiFiClient& stream = http->getStream();
    const size_t bufferSize = 1024;
    uint8_t buffer[bufferSize];
    size_t written = 0;

    Serial.println("[Weather] Starting download to SD card...");

    while (http->connected() && (written < contentLength)) {
      int len = stream.readBytes(buffer, bufferSize);
      if (len > 0) {
        weatherFile.write(buffer, len);
        written += len;
      } else {
        delay(1);
      }
    }
    weatherFile.close();

    Serial.printf("[Weather] Downloaded %d bytes to SD card.\n", written);
    http->end();

    if (written != contentLength) {
      Serial.printf("[Weather] Download integrity check FAILED! Expected %d, but got %d.\n", contentLength, written);
      SD.remove("/weather.json");
      isFetchingWeather = false;
      return false;
    }

    Serial.println("[Weather] Download integrity check PASSED.");

    weatherFile = SD.open("/weather.json");
    if (!weatherFile) {
      Serial.println("[Weather] Failed to re-open weather.json for reading");
      isFetchingWeather = false;
      return false;
    }

    DynamicJsonDocument doc(0);
    DeserializationError error = deserializeJson(doc, weatherFile);
    weatherFile.close();
    SD.remove("/weather.json");

    if (error) {
      Serial.printf("[Weather] deserializeJson() failed: %s\n", error.c_str());
      isFetchingWeather = false;
      return false;
    }

    JsonArray timeseries = doc["properties"]["timeseries"].as<JsonArray>();
    if (!timeseries || timeseries.size() == 0) {
      Serial.println("[Weather] JSON parsing error: timeseries array is missing or empty.");
      isFetchingWeather = false;
      return false;
    }

    JsonObject d = timeseries[0]["data"]["instant"]["details"];
    float t = d["air_temperature"] | NAN, rh = d["relative_humidity"] | NAN, ws = d["wind_speed"] | NAN, p0 = d["air_pressure_at_sea_level"] | NAN;
    float uv = d["ultraviolet_index_clear_sky"] | NAN, wd = d["wind_from_direction"] | NAN, cc = d["cloud_area_fraction"] | NAN;

    snprintf(weatherData.tempC, sizeof(weatherData.tempC), isnan(t) ? "--" : "%.1f", t);
    snprintf(weatherData.humidity, sizeof(weatherData.humidity), isnan(rh) ? "--" : "%d", (int)roundf(rh));
    snprintf(weatherData.wind_ms, sizeof(weatherData.wind_ms), isnan(ws) ? "--" : "%.1f", ws);
    snprintf(weatherData.wind_kmh, sizeof(weatherData.wind_kmh), isnan(ws) ? "" : "%d", (int)roundf(ws * 3.6f));
    snprintf(weatherData.wind_mph, sizeof(weatherData.wind_mph), isnan(ws) ? "" : "%d", (int)roundf(ws * 2.23694f));
    snprintf(weatherData.pressure_hpa, sizeof(weatherData.pressure_hpa), isnan(p0) ? "" : "%d", (int)roundf(p0));
    snprintf(weatherData.pressure_inhg, sizeof(weatherData.pressure_inhg), isnan(p0) ? "" : "%.2f", p0 * 0.02953f);
    snprintf(weatherData.pressure_mmhg, sizeof(weatherData.pressure_mmhg), isnan(p0) ? "" : "%d", (int)roundf(p0 * 0.750062f));
    snprintf(weatherData.uvIndex, sizeof(weatherData.uvIndex), isnan(uv) ? "--" : "%d", (int)roundf(uv));
    strncpy(weatherData.windDirection, isnan(wd) ? "--" : degreesToCardinal(wd).c_str(), sizeof(weatherData.windDirection));
    snprintf(weatherData.cloudCover, sizeof(weatherData.cloudCover), isnan(cc) ? "--" : "%d", (int)roundf(cc));
    snprintf(weatherData.feelsC, sizeof(weatherData.feelsC), isnan(t) ? "--" : "%.1f", computeFeelsLike(t, rh, ws));

    const char* s1 = timeseries[0]["data"]["next_1_hours"]["summary"]["symbol_code"];
    strncpy(weatherData.currentIconCode, normalizeWeatherSymbol(s1 ? s1 : "fair_day").c_str(), sizeof(weatherData.currentIconCode));
    const char* s6 = timeseries[0]["data"]["next_6_hours"]["summary"]["symbol_code"];
    strncpy(weatherData.next6HoursIconCode, normalizeWeatherSymbol(s6 ? s6 : weatherData.currentIconCode).c_str(), sizeof(weatherData.next6HoursIconCode));

    String desc = getWeatherDescription(weatherData.currentIconCode);
    strncpy(weatherData.weatherDescription, desc.c_str(), sizeof(weatherData.weatherDescription));

    struct tm timeinfo = {};
    bool timeAvailable = getSafeLocalTime(&timeinfo, 5);
    int sunriseMinutes = parseTimeToMinutes(String(weatherData.sunriseTime));
    int sunsetMinutes = parseTimeToMinutes(String(weatherData.sunsetTime));
    int currentMinutes = timeAvailable ? (timeinfo.tm_hour * 60 + timeinfo.tm_min) : -1;

    bool isCurrentlyDay = true;
    if (sunriseMinutes != -1 && sunsetMinutes != -1 && currentMinutes != -1) {
      isCurrentlyDay = (currentMinutes >= sunriseMinutes && currentMinutes < sunsetMinutes);
    } else if (timeAvailable) {
      isCurrentlyDay = (timeinfo.tm_hour >= 6 && timeinfo.tm_hour < 18);
    }

    parseWeeklyForecast(timeseries, isCurrentlyDay);
    parseHourlyForecast(timeseries);

    isDataValid = true;
    lastDataFetchTimestamp = millis();
    generateIconReport();
    isFetchingWeather = false;

    Serial.printf("[Weather] Success! Description: %s\n", weatherData.weatherDescription);

    return true;

  } else {
    Serial.printf("[Weather] HTTP request failed with code: %d\n", httpCode);
    http->end();
    isFetchingWeather = false;
    return false;
  }
}

// Checks if it's the top of the hour and triggers the hourly beep if enabled.
void handleHourlyBeep() {
  struct tm timeinfo;

  if (!getSafeLocalTime(&timeinfo, 5)) {
    return;
  }

  if (!hourlyBeepEnabled || !isTimeSynced) return;

  int currentHour = timeinfo.tm_hour;
  if (currentHour != previousHour && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
    previousHour = currentHour;
    beepCount = 0;
    int hour12 = currentHour % 12;
    targetBeeps = (hour12 == 0) ? 12 : hour12;
    hourBeepActive = true;
    Serial.printf("[Beep] It's %d o'clock. Beeping %d times.\n", hour12 == 0 ? 12 : hour12, targetBeeps);
  }
}

// Saves the hourly beep setting to persistent memory.
void saveBeepSetting() {
  prefs.begin("weather-app", false);
  prefs.putBool("hourlyBeep", hourlyBeepEnabled);
  prefs.end();
  Serial.printf("[Prefs] Saved hourly beep setting: %s\n", hourlyBeepEnabled ? "ON" : "OFF");
}

void processTouchLogic() {
  if (touchscreen.touched()) {
    if (!isTouchActive) {
      isTouchActive = true;
      TS_Point p = touchscreen.getPoint();
      touchStartX = map(p.x, calData.x_min, calData.x_max, 0, SCREEN_WIDTH);
      touchStartY = map(p.y, calData.y_min, calData.y_max, 0, SCREEN_HEIGHT);
      touchPressStartTime = millis();

      if (isAlarmSounding) {
        isAlarmSounding = false;
        mainAlarmActive = false;
        digitalWrite(BUZZER_PIN, LOW);
        tft.invertDisplay(invertDisplay);
        return;
      }
      if (alarmUI.inAdjustMode || timerUI.inAdjustMode) {
        handleValueAdjustment(touchStartX, touchStartY, false);
      }
      if (currentPage == CLOCK_PAGE && !cityListVisible) {
        int screen_icon_x = 240 + BEEP_ICON_X, screen_icon_y = BEEP_ICON_Y;
        if (sqrt(pow(touchStartX - screen_icon_x, 2) + pow(touchStartY - screen_icon_y, 2)) < BEEP_ICON_R + 15) {
          showBellTouchCircle = true;
          updateAndDrawAnalogClock(false);
        }
      }
    } else {
      if (millis() - touchPressStartTime > LONG_PRESS_DURATION) {
        Serial.println(">>> Long press detected! Entering WiFi config mode. <<<");
        isTouchActive = false;
        mainAlarmActive = false;
        WiFiManager wm;
        wm.setCustomHeadElement(CUSTOM_CSS_HEAD);
        wm.setAPCallback([](WiFiManager* myWiFiManager) {
          displayBootMessage(myWiFiManager->getConfigPortalSSID() + "\n" + WiFi.softAPIP().toString());
        });
        prefs.begin("weather-app", false);
        String lat = prefs.getString("latitude", "16.890174");
        String lon = prefs.getString("longitude", "96.214092");
        String tz = prefs.getString("timezone", "<+0630>-6:30");
        bool currentInvert = prefs.getBool("invertDisplay", true);
        prefs.end();
        String timezoneHtml = generateTimezoneHtml();
        WiFiManagerParameter p_lat("latitude", "Lat", lat.c_str(), 20);
        WiFiManagerParameter p_lon("longitude", "Lon", lon.c_str(), 20);
        WiFiManagerParameter p_tz("timezone", "Timezone", tz.c_str(), 4096, timezoneHtml.c_str(), WFM_LABEL_BEFORE);
        WiFiManagerParameter p_invert("invertDisplay", "Screen Color", currentInvert ? "1" : "0", 4, INVERT_DISPLAY_UI_HTML, WFM_LABEL_BEFORE);
        wm.addParameter(&p_lat);
        wm.addParameter(&p_lon);
        wm.addParameter(&p_tz);
        wm.addParameter(&p_invert);
        wm.setTitle("Weather Setup");
        wm.setConfigPortalTimeout(180);
        if (wm.startConfigPortal("WeatherStation_Setup")) {
          prefs.begin("weather-app", false);
          prefs.putString("latitude", p_lat.getValue());
          prefs.putString("longitude", p_lon.getValue());
          prefs.putString("timezone", p_tz.getValue());
          prefs.putBool("invertDisplay", String(p_invert.getValue()) == "1");
          prefs.end();
          displayBootMessage("Settings Saved!\nRestarting .....");
        } else {
          displayBootMessage("Timed Out.\nRestarting .....");
        }
        delay(3000);
        ESP.restart();
      }
    }
  } else if (isTouchActive) {
    isTouchActive = false;
    TS_Point p = touchscreen.getPoint();
    int16_t endX = map(p.x, calData.x_min, calData.x_max, 0, SCREEN_WIDTH);
    int16_t endY = map(p.y, calData.y_min, calData.y_max, 0, SCREEN_HEIGHT);

    if (showBellTouchCircle) {
      showBellTouchCircle = false;
      updateAndDrawAnalogClock(false);
    }

    if (cityListVisible) {
      int newFocusedCityIndex = focusedCityIndex;
      bool focusMayHaveChanged = false;
      const int btnY = 14, btnH = 30, btnW = 80, btnGap = 5;
      const int totalGroupWidth = (btnW * 3) + (btnGap * 2);
      const int btnX_Up = (SCREEN_WIDTH - totalGroupWidth) / 2;
      const int btnX_Down = btnX_Up + btnW + btnGap;
      const int btnX_Set = btnX_Down + btnW + btnGap;

      if (endY >= btnY && endY <= btnY + btnH) {
        if (endX >= btnX_Up && endX <= btnX_Up + btnW) {
          // --- FIX FOR "UP" BUTTON ---
          // Only jump a full page if we are at the top of the visible list AND there are enough items above to jump a full page.
          bool isAtTop = (focusedCityIndex == listScrollOffset) && (focusedCityIndex >= ITEMS_PER_PAGE);
          int jumpAmount = isAtTop ? ITEMS_PER_PAGE : 1;
          newFocusedCityIndex -= jumpAmount;
          focusMayHaveChanged = true;
        } else if (endX >= btnX_Down && endX <= btnX_Down + btnW) {
          // --- FIX FOR "DOWN" BUTTON ---
          // Only jump a full page if we are at the bottom of the visible list AND there are enough items below to jump a full page.
          bool isAtBottom = (focusedCityIndex == listScrollOffset + ITEMS_PER_PAGE - 1) && (focusedCityIndex + ITEMS_PER_PAGE < numCities);
          int jumpAmount = isAtBottom ? ITEMS_PER_PAGE : 1;
          newFocusedCityIndex += jumpAmount;
          focusMayHaveChanged = true;
        } else if (endX >= btnX_Set && endX <= btnX_Set + btnW) {
          confirmCitySelection();
          return;
        }
      } else {
        const int listStartY = 55, itemHeight = 32, itemGap = 2;
        const int itemTotalHeight = itemHeight + itemGap;
        const int listLeftX = 25, listRightX = SCREEN_WIDTH - 25;
        for (int i = 0; i < ITEMS_PER_PAGE; i++) {
          int currentItemTopY = listStartY + (i * itemTotalHeight);
          int currentItemBottomY = currentItemTopY + itemHeight;
          if (endY >= currentItemTopY && endY <= currentItemBottomY && endX >= listLeftX && endX <= listRightX) {
            int tappedCityIndex = listScrollOffset + i;
            if (tappedCityIndex < numCities) {
              if (tappedCityIndex == focusedCityIndex) {
                confirmCitySelection();
                return;
              } else {
                newFocusedCityIndex = tappedCityIndex;
                focusMayHaveChanged = true;
              }
              break;
            }
          }
        }
      }

      if (focusMayHaveChanged && newFocusedCityIndex != focusedCityIndex) {
        int oldFocusedIndex = focusedCityIndex;
        if (newFocusedCityIndex >= numCities) {
          newFocusedCityIndex = 0;
        }
        if (newFocusedCityIndex < 0) {
          newFocusedCityIndex = numCities - 1;
        }
        focusedCityIndex = newFocusedCityIndex;
        bool needsFullRedraw = false;
        if (focusedCityIndex < listScrollOffset) {
          listScrollOffset = focusedCityIndex;
          needsFullRedraw = true;
        } else if (focusedCityIndex >= (listScrollOffset + ITEMS_PER_PAGE)) {
          listScrollOffset = focusedCityIndex - ITEMS_PER_PAGE + 1;
          needsFullRedraw = true;
        }
        if (needsFullRedraw) {
          drawCityListItems();
        } else {
          drawCityListItem(oldFocusedIndex, false);
          drawCityListItem(focusedCityIndex, true);
        }
      }
      lastUiInteractionTime = millis();
      return;
    }

    int16_t deltaX = endX - touchStartX;
    int16_t deltaY = endY - touchStartY;
    bool isSwipe = abs(deltaX) > SWIPE_THRESHOLD || abs(deltaY) > SWIPE_THRESHOLD;

    if (isSwipe) {
      if (alarmUI.inAdjustMode) {
        alarmUI.inAdjustMode = false;
        saveAlarms();
      }
      if (timerUI.inAdjustMode) {
        timerUI.inAdjustMode = false;
      }
      if (abs(deltaX) > abs(deltaY)) {
        int numPages = 6;
        if (deltaX > 0) currentPage = (ScreenPage)((currentPage - 1 + numPages) % numPages);
        else currentPage = (ScreenPage)((currentPage + 1) % numPages);
        needsRedraw = true;

        if ((currentPage == WEATHER_PAGE || currentPage == HOURLY_GRAPH_PAGE) && !isFetchingWeather) {
          if (millis() - lastDataFetchTimestamp > WEATHER_REFRESH_INTERVAL_MS) {
            Serial.println("[Core 1] Swiped to weather page and data is stale. Signaling fetch.");
            isFetchingWeather = true;
            xSemaphoreGive(fetchRequestSignal);
          }
        }
      }
    } else {
      if (currentPage == WEATHER_PAGE) {
        if (endY < HEADER_HEIGHT && endX > (SCREEN_WIDTH - 150)) {
          use24HourFormat = !use24HourFormat;
          prefs.begin("weather-app", false);
          prefs.putBool("use24Hour", use24HourFormat);
          prefs.end();
          updateSunAndMoonTimes();
          needsRedraw = true;
        } else if (endX > THERMO_ICON_X && endX < (THERMO_ICON_X + THERMO_ICON_SIZE) && endY > THERMO_ICON_Y && endY < (THERMO_ICON_Y + THERMO_ICON_SIZE)) {
          useFahrenheit = !useFahrenheit;
          prefs.begin("weather-app", false);
          prefs.putBool("useFahrenheit", useFahrenheit);
          prefs.end();
          needsRedraw = true;
        }
        const int windRowY = INFO_COLUMN_Y_START + (3 * INFO_ROW_HEIGHT);
        if (endX >= INFO_COLUMN_X && endY >= windRowY - (INFO_ROW_HEIGHT / 2) && endY <= windRowY + (INFO_ROW_HEIGHT / 2)) {
          useMph = !useMph;
          prefs.begin("weather-app", false);
          prefs.putBool("useMph", useMph);
          prefs.end();
          needsRedraw = true;
        }
        const int pressureRowY = INFO_COLUMN_Y_START + (5 * INFO_ROW_HEIGHT);
        if (endX >= INFO_COLUMN_X && endY >= pressureRowY - (INFO_ROW_HEIGHT / 2) && endY <= pressureRowY + (INFO_ROW_HEIGHT / 2)) {
          pressureUnitState = (pressureUnitState + 1) % 3;
          prefs.begin("weather-app", false);
          prefs.putInt("pressureUnit", pressureUnitState);
          prefs.end();
          needsRedraw = true;
        }
      } else if (currentPage == CLOCK_PAGE && millis() - lastUiInteractionTime > UI_DEBOUNCE_TIME) {
        int screen_icon_x = 240 + BEEP_ICON_X, screen_icon_y = BEEP_ICON_Y;
        const int RIGHT_PANEL_X = SCREEN_HEIGHT, PADDING_LEFT = 5, FLAG_TEXT_GAP = 3, flagWidth = 32, flagHeight = 32, contentCenterY = 215;
        const int flagScreenX = RIGHT_PANEL_X + PADDING_LEFT, flagScreenY = contentCenterY - (flagHeight / 2), touchPadding = 5;
        tft.setTextSize(2);
        int textWidth = tft.textWidth(cities[currentCityIndex].cityCode), textScreenX = flagScreenX + flagWidth + FLAG_TEXT_GAP;
        int touchAreaX1 = flagScreenX - touchPadding, touchAreaY1 = flagScreenY - touchPadding, touchAreaX2 = textScreenX + textWidth + touchPadding, touchAreaY2 = flagScreenY + flagHeight + touchPadding;
        if (sqrt(pow(endX - screen_icon_x, 2) + pow(endY - screen_icon_y, 2)) < BEEP_ICON_R + 15) {
          hourlyBeepEnabled = !hourlyBeepEnabled;
          saveBeepSetting();
          updateAndDrawAnalogClock(false);
          if (!confirmationBeepActive) {
            digitalWrite(BUZZER_PIN, HIGH);
            confirmationBeepActive = true;
            confirmationBeepStartTime = millis();
          }
        } else if (endX >= touchAreaX1 && endX <= touchAreaX2 && endY >= touchAreaY1 && endY <= touchAreaY2) {
          cityListVisible = true;
          focusedCityIndex = currentCityIndex;
          listScrollOffset = max(0, focusedCityIndex - (ITEMS_PER_PAGE / 2));
          if (listScrollOffset + ITEMS_PER_PAGE > numCities) listScrollOffset = numCities - ITEMS_PER_PAGE;
          if (listScrollOffset < 0) listScrollOffset = 0;
          drawCitySelectionPopup();
          lastUiInteractionTime = millis();
        }
      } else if (currentPage == CALENDAR_PAGE) {
        struct tm timeinfo;
        getSafeLocalTime(&timeinfo, 5);

        if (endY > 5 && endY < 45) {
          if (endX < 80) {
            calendarTime.tm_mon--;
            mktime(&calendarTime);
            needsRedraw = true;
          } else if (endX > SCREEN_WIDTH - 80) {
            calendarTime.tm_mon++;
            mktime(&calendarTime);
            needsRedraw = true;
          }
        }

        const int todayHitboxX1 = 120;
        const int todayHitboxX2 = 200;
        const int todayHitboxY1 = 30;
        const int todayHitboxY2 = 60;

        if (endX > todayHitboxX1 && endX < todayHitboxX2 && endY > todayHitboxY1 && endY < todayHitboxY2) {
          calendarTime = timeinfo;
          drawCalendarPage(calendarTime);
        }
      } else if (currentPage == ALARM_PAGE) {
        handleAlarmPageTouch(endX, endY);
      } else if (currentPage == TIMER_PAGE) {
        handleTimerPageTouch(endX, endY);
      }
    }
  }
}

// Adjusts the screen backlight brightness based on the LDR reading.
void adjustBrightness() {
  static int readings[NUM_LDR_READINGS], readIndex = 0;
  static long total = 0, average = 0;
  static unsigned long lastReadTime = 0;
  if (millis() - lastReadTime > 50) {
    lastReadTime = millis();
    total = total - readings[readIndex];
    readings[readIndex] = analogRead(LDR_PIN);
    total = total + readings[readIndex];
    readIndex = (readIndex + 1) % NUM_LDR_READINGS;
    average = total / NUM_LDR_READINGS;
    int brightness = map(average, 0, 1024, 255, 20);
    brightness = constrain(brightness, 20, 255);
    analogWrite(TFT_BACKLIGHT_PIN, brightness);
  }
}

// --- BEEP ICON REAL-TIME UPDATE ---
// Tracks the last drawn state to prevent unnecessary redraws
bool lastBlinkState = false;

// Efficiently updates the blinking bell icon without redrawing the whole screen.
void updateBeepIcon() {
  if (currentPage != CLOCK_PAGE || cityListVisible) {
    return;
  }
  if (isBlinking != lastBlinkState) {
    beepIconSprite.fillSprite(COLOR_BACKGROUND);
    drawBeepIcon(&beepIconSprite, 15, 15);
    int screenX = (SCREEN_HEIGHT + BEEP_ICON_X) - 15;
    int screenY = BEEP_ICON_Y - 15;
    beepIconSprite.pushSprite(screenX, screenY);
    lastBlinkState = isBlinking;
  }
}

void drawHourlyGraphPage() {
  tft.fillScreen(COLOR_BACKGROUND);

  if (hourlyForecast.empty()) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.drawString("Hourly Data Unavailable", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    return;
  }

  const int topClearance = 75;
  const int bottomClearance = 15;
  const int horizontalPadding = 16;
  const int graphY = topClearance;
  const int graphH = SCREEN_HEIGHT - topClearance - bottomClearance;
  const int insetX = horizontalPadding;
  const int insetW = SCREEN_WIDTH - (2 * horizontalPadding);

  std::vector<HourlyDataPoint> pointsToShow;
  for (size_t i = 0; i < hourlyForecast.size() && i < 24; i += 3) {
    pointsToShow.push_back(hourlyForecast[i]);
  }
  int numPointsToShow = pointsToShow.size();

  if (numPointsToShow < 2) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT);
    tft.drawString("Incomplete Hourly Data", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    return;
  }

  float tempMin = 100, tempMax = -100;
  for (int i = 0; i < numPointsToShow; i++) {
    float tempC = pointsToShow[i].temperature;
    float tempToCompare = useFahrenheit ? (tempC * 1.8 + 32) : tempC;
    if (tempToCompare < tempMin) tempMin = tempToCompare;
    if (tempToCompare > tempMax) tempMax = tempToCompare;
  }
  tempMin = floor(tempMin) - 2;
  tempMax = ceil(tempMax) + 2;
  float tempRange = tempMax - tempMin;
  if (tempRange < 1) tempRange = 1;

  for (int i = 0; i < numPointsToShow; i++) {
    int x = insetX + (insetW * i / (numPointsToShow - 1));
    float tempC = pointsToShow[i].temperature;
    float currentTemp = useFahrenheit ? (tempC * 1.8 + 32) : tempC;
    int y = graphY + graphH - (int)((graphH * (currentTemp - tempMin)) / tempRange);

    String iconPath = selectedIconDirSmall + "/" + pointsToShow[i].icon + ".bmp";
    drawBmpFromFile(iconPath, x - 16, y - 40);

    String tempStr = String(tempC, 1);
    drawTempUnit(x, y - 45, tempStr, 1, 0, COLOR_HIGH_TEMP, COLOR_BACKGROUND, -1, 1);

    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);

    String timeStr;
    if (use24HourFormat) {
      timeStr = String(pointsToShow[i].hour) + ":00";
    } else {
      int hour12 = pointsToShow[i].hour % 12;
      if (hour12 == 0) hour12 = 12;
      String ampm = (pointsToShow[i].hour < 12) ? "AM" : "PM";
      timeStr = String(hour12) + ampm;
    }

    int16_t textW = tft.textWidth(timeStr);
    tft.fillRect(x - textW / 2 - 2, y - 68, textW + 4, 10, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT);
    tft.drawString(timeStr, x, y - 64);
  }

  for (int i = 0; i < numPointsToShow - 1; i++) {
    float tempC1 = pointsToShow[i].temperature;
    float currentTemp1 = useFahrenheit ? (tempC1 * 1.8 + 32) : tempC1;
    float tempC2 = pointsToShow[i + 1].temperature;
    float currentTemp2 = useFahrenheit ? (tempC2 * 1.8 + 32) : tempC2;
    int x1 = insetX + (insetW * i / (numPointsToShow - 1));
    int y1 = graphY + graphH - (int)((graphH * (currentTemp1 - tempMin)) / tempRange);
    int x2 = insetX + (insetW * (i + 1) / (numPointsToShow - 1));
    int y2 = graphY + graphH - (int)((graphH * (currentTemp2 - tempMin)) / tempRange);
    tft.drawLine(x1, y1, x2, y2, CLOCK_COLOR_ACCENT);
  }
}


// --- ADD THIS BLOCK --- Calibration Functions ---
void drawCalibrationTarget(int x, int y, int index) {
  const int TARGET_SIZE = 25;
  const int TARGET_THICKNESS = 3;

  switch (index) {
    case 0:  // Top-Left
      tft.fillRect(x, y, TARGET_SIZE, TARGET_THICKNESS, COLOR_TEXT);
      tft.fillRect(x, y, TARGET_THICKNESS, TARGET_SIZE, COLOR_TEXT);
      break;
    case 1:  // Top-Right
      tft.fillRect(x - TARGET_SIZE + 1, y, TARGET_SIZE, TARGET_THICKNESS, COLOR_TEXT);
      tft.fillRect(x - TARGET_THICKNESS + 1, y, TARGET_THICKNESS, TARGET_SIZE, COLOR_TEXT);
      break;
    case 2:  // Bottom-Right
      tft.fillRect(x - TARGET_SIZE + 1, y - TARGET_THICKNESS + 1, TARGET_SIZE, TARGET_THICKNESS, COLOR_TEXT);
      tft.fillRect(x - TARGET_THICKNESS + 1, y - TARGET_SIZE + 1, TARGET_THICKNESS, TARGET_SIZE, COLOR_TEXT);
      break;
    case 3:  // Bottom-Left
      tft.fillRect(x, y - TARGET_THICKNESS + 1, TARGET_SIZE, TARGET_THICKNESS, COLOR_TEXT);
      tft.fillRect(x, y - TARGET_SIZE + 1, TARGET_THICKNESS, TARGET_SIZE, COLOR_TEXT);
      break;
  }
}

void runTouchCalibration() {
  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Calibration Required", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 40);
  tft.setTextSize(1);
  tft.drawString("Please tap the exact corner of each target", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);
  delay(3000);

  int cal_points_x[4] = { 0, SCREEN_WIDTH - 1, SCREEN_WIDTH - 1, 0 };
  int cal_points_y[4] = { 0, 0, SCREEN_HEIGHT - 1, SCREEN_HEIGHT - 1 };
  int32_t raw_x_points[4];
  int32_t raw_y_points[4];

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(COLOR_BACKGROUND);
    drawCalibrationTarget(cal_points_x[i], cal_points_y[i], i);
    while (!touchscreen.touched()) {
      delay(10);
    }
    TS_Point p = touchscreen.getPoint();
    raw_x_points[i] = p.x;
    raw_y_points[i] = p.y;
    Serial.printf("Point %d: Pixel(%d, %d) -> Raw(X:%ld, Y:%ld)\n", i, cal_points_x[i], cal_points_y[i], raw_x_points[i], raw_y_points[i]);
    while (touchscreen.touched()) {
      delay(10);
    }
    delay(500);
  }

  calData.x_min = (raw_x_points[0] + raw_x_points[3]) / 2;
  calData.x_max = (raw_x_points[1] + raw_x_points[2]) / 2;
  calData.y_min = (raw_y_points[0] + raw_y_points[1]) / 2;
  calData.y_max = (raw_y_points[2] + raw_y_points[3]) / 2;

  Serial.println("\n--- Initial Calculated Values ---");
  Serial.printf("Raw X_MIN: %ld, Raw X_MAX: %ld\n", calData.x_min, calData.x_max);
  Serial.printf("Raw Y_MIN: %ld, Raw Y_MAX: %ld\n", calData.y_min, calData.y_max);

  if (calData.x_min > calData.x_max) {
    Serial.println("-> X-Axis is inverted. Swapping min/max values for correction.");
    std::swap(calData.x_min, calData.x_max);
  }
  if (calData.y_min > calData.y_max) {
    Serial.println("-> Y-Axis is inverted. Swapping min/max values for correction.");
    std::swap(calData.y_min, calData.y_max);
  }

  prefs.begin("weather-app", false);
  prefs.putBytes("calibration", &calData, sizeof(TouchCalibrationData));
  prefs.putBool("calibrated", true);
  prefs.end();

  isCalibrated = true;

  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Calibration Complete!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
  Serial.println("--- Final Corrected Values Saved ---");
  Serial.printf("Final X_MIN: %ld, Final X_MAX: %ld\n", calData.x_min, calData.x_max);
  Serial.printf("Final Y_MIN: %ld, Final Y_MAX: %ld\n\n", calData.y_min, calData.y_max);
  delay(2000);
}

/**
   @brief Core 0 Task: Handles all blocking network operations for weather data.
*/
void dataFetchLoop(void* parameter) {
  Serial.println("Data fetching task started on Core 0.");
  for (;;) {
    xSemaphoreTake(fetchRequestSignal, portMAX_DELAY);
    Serial.println("[Core 0] Received weather fetch request. Starting network operations...");
    bool weatherSuccess = fetchWeatherDataOnce();

    if (weatherSuccess) {
      Serial.println("[Core 0] Weather fetch successful. Waiting for mutex to update shared data...");
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        lastDataFetchTimestamp = millis();
        newDataIsAvailable = true;
        Serial.println("[Core 0] Mutex acquired. Shared data updated. Releasing mutex.");
        xSemaphoreGive(dataMutex);
      } else {
        Serial.println("[Core 0] FAILED to acquire data mutex. Skipping data update.");
      }
    } else {
      Serial.println("[Core 0] Weather fetch failed.");
    }
    isFetchingWeather = false;
  }
}

bool fetchInitialDataWithRetry(bool showBootMessage) {
  for (int i = 1; i <= MAX_NETWORK_RETRIES; i++) {
    if (showBootMessage) {
      displayBootMessage("Fetching Weather .....\nAttempt " + String(i) + "/" + MAX_NETWORK_RETRIES);
    }
    if (fetchWeatherDataOnce(i, MAX_NETWORK_RETRIES)) {
      Serial.println("[Boot] Initial weather fetch successful.");
      return true;
    }
    Serial.printf("[Boot Retry] Initial fetch attempt %d failed.\n", i);
    if (i < MAX_NETWORK_RETRIES) {
      delay(NETWORK_RETRY_DELAY_MS);
    }
  }
  Serial.println("[Boot Retry] Failed to fetch weather after all retries.");
  return false;
}

// Main setup function, runs once on boot.
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  analogWriteResolution(TFT_BACKLIGHT_PIN, PWM_RES);
  analogWrite(TFT_BACKLIGHT_PIN, 128);
  tft.init();
  tft.setRotation(3);

  prefs.begin("weather-app", false);
  invertDisplay = prefs.getBool("invertDisplay", false);
  prefs.end();
  tft.invertDisplay(invertDisplay);
  tft.setSwapBytes(true);
  tft.fillScreen(COLOR_BACKGROUND);
  mountSdCard();
  displayStartupLogo();

  dataMutex = xSemaphoreCreateMutex();
  fetchRequestSignal = xSemaphoreCreateBinary();
  timezoneMutex = xSemaphoreCreateMutex();

  if (dataMutex == NULL || fetchRequestSignal == NULL || timezoneMutex == NULL) {
    Serial.println("FATAL: Could not create mutex or semaphore!");
    displayBootMessage("System Error\nMutex Failed");
    while (1)
      ;
  }

  xTaskCreatePinnedToCore(dataFetchLoop, "DataFetchTask", 10000, NULL, 1, &dataFetchTaskHandle, 0);

  prefs.begin("weather-app", false);
  latitude = prefs.getString("latitude", "16.890174");
  longitude = prefs.getString("longitude", "96.214092");
  isNorthernHemisphere = (latitude.toFloat() >= 0);
  system_tz_string = prefs.getString("timezone", "<+0630>-6:30");
  currentCityIndex = prefs.getInt("cityIndex", 53);
  useFahrenheit = prefs.getBool("useFahrenheit", false);
  useMph = prefs.getBool("useMph", true);
  pressureUnitState = prefs.getInt("pressureUnit", 0);
  hourlyBeepEnabled = prefs.getBool("hourlyBeep", true);
  use24HourFormat = prefs.getBool("use24Hour", false);
  prefs.end();

  loadAlarms();

  Serial.println("--- Initial Configuration ---");
  Serial.println("Latitude: " + latitude);
  Serial.println("Longitude: " + longitude);
  Serial.println("SYSTEM Timezone: " + system_tz_string);
  Serial.println("Time Format: " + String(use24HourFormat ? "24-Hour" : "12-Hour"));
  Serial.println("-----------------------------");

  displayBootMessage("Connecting to WiFi .....");
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime < 15000)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    displayBootMessage("Setup Required\nStarting Portal .....");
    WiFiManager wm;
    wm.setCustomHeadElement(CUSTOM_CSS_HEAD);
    wm.setConfigPortalTimeout(180);
    wm.setAPCallback([](WiFiManager* m) {
      displayBootMessage(m->getConfigPortalSSID() + "\n" + WiFi.softAPIP().toString());
    });
    String timezoneHtml = generateTimezoneHtml();
    WiFiManagerParameter p_lat("latitude", "Latitude", latitude.c_str(), 20);
    WiFiManagerParameter p_lon("longitude", "Longitude", longitude.c_str(), 20);
    WiFiManagerParameter p_tz("timezone", "Timezone", system_tz_string.c_str(), 4096, timezoneHtml.c_str(), WFM_LABEL_BEFORE);
    WiFiManagerParameter p_invert("invertDisplay", "Screen Color", invertDisplay ? "1" : "0", 4, INVERT_DISPLAY_UI_HTML, WFM_LABEL_BEFORE);
    wm.addParameter(&p_lat);
    wm.addParameter(&p_lon);
    wm.addParameter(&p_tz);
    wm.addParameter(&p_invert);
    wm.setTitle("Weather Station Setup");
    if (wm.startConfigPortal("WeatherStation_Setup")) {
      displayBootMessage("Settings Saved!");
      prefs.begin("weather-app", false);
      prefs.putString("latitude", p_lat.getValue());
      prefs.putString("longitude", p_lon.getValue());
      prefs.putString("timezone", p_tz.getValue());
      prefs.putBool("invertDisplay", String(p_invert.getValue()) == "1");
      prefs.end();
      delay(2500);
    } else {
      displayBootMessage("Config Timed Out\nRestarting .....");
      delay(3000);
      ESP.restart();
    }
  }

  displayBootMessage("WiFi Connected!");
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("HTTP web server started.");

  prefs.begin("weather-app", false);
  latitude = prefs.getString("latitude", "16.890174");
  longitude = prefs.getString("longitude", "96.214092");
  system_tz_string = prefs.getString("timezone", "<+0630>-6:30");
  invertDisplay = prefs.getBool("invertDisplay", true);
  tft.invertDisplay(invertDisplay);
  isNorthernHemisphere = (latitude.toFloat() >= 0);
  prefs.end();

  Serial.println("--- Using Final Configuration ---");
  Serial.println("Latitude: " + latitude);
  Serial.println("Longitude: " + longitude);
  Serial.println("SYSTEM Timezone: " + system_tz_string);
  Serial.println("-------------------------------");

  memset(&prevClockState, 0, sizeof(DigitalClockState));
  memset(&prevCountdownState, 0, sizeof(CountdownState));
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000, true, 0);

  prefs.begin("weather-app", false);
  isCalibrated = prefs.getBool("calibrated", false);

  if (isCalibrated) {
    prefs.getBytes("calibration", &calData, sizeof(TouchCalibrationData));
    Serial.println("Loaded existing touch calibration data.");
    Serial.printf("X_MIN: %ld, X_MAX: %ld, Y_MIN: %ld, Y_MAX: %ld\n", calData.x_min, calData.x_max, calData.y_min, calData.y_max);
  }
  prefs.end();

  displayBootMessage("Initializing .....");
  touchscreenSPI.begin(HSPI_CLK_PIN, HSPI_MISO_PIN, HSPI_MOSI_PIN, TOUCH_CS_PIN);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);
  pinMode(TOUCH_IRQ_PIN, INPUT);

  if (!isCalibrated) {
    runTouchCalibration();
  }

  // Re-attach the interrupt for normal operation after calibration
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ_PIN), handleTouchInterrupt, FALLING);

  detectIconDirectories();
  generateFlagReport();

  bool startupSuccess = false;
  bool timeSyncOK = false;
  for (int i = 1; i <= MAX_NETWORK_RETRIES; i++) {
    displayBootMessage("Syncing Time .....\nAttempt " + String(i) + "/" + MAX_NETWORK_RETRIES);
    Serial.println("TZ: " + system_tz_string);
    configTzTime(system_tz_string.c_str(), "pool.ntp.org", "time.cloudflare.com");
    if (ensureTimeSynced()) {
      timeSyncOK = true;
      break;
    }
    if (i < MAX_NETWORK_RETRIES) delay(NETWORK_RETRY_DELAY_MS);
  }

  if (timeSyncOK) {
    struct tm timeinfo;
    if (getSafeLocalTime(&timeinfo, 5000)) {
      calendarTime = timeinfo;
      Serial.println("Calendar initialized to current date.");
    }

    if (fetchInitialDataWithRetry(true)) {
      startupSuccess = true;
    }
  }

  if (!startupSuccess) {
    displayBootMessage(timeSyncOK ? "Weather Fetch Failed" : "Time Sync Failed");
    delay(5000);
    WiFiManager recovery_wm;
    recovery_wm.setAPCallback([](WiFiManager* myWiFiManager) {
      displayBootMessage(myWiFiManager->getConfigPortalSSID() + "\n" + WiFi.softAPIP().toString());
    });
    recovery_wm.setConfigPortalTimeout(180);
    recovery_wm.startConfigPortal("WeatherStation_Setup");
    displayBootMessage("Config Timed Out\nRestarting .....");
    delay(3000);
    ESP.restart();
  }

  drawWeatherScreen();
  Serial.printf("Setup complete. Free Heap: %u bytes\n", ESP.getFreeHeap());
}

void loop() {
  server.handleClient();
  ensureHomeTimezoneIsSet();
  adjustBrightness();
  checkAlarmsAndTimers();
  processTouchLogic();
  updateBeepIcon();

  if (confirmationBeepActive && (millis() - confirmationBeepStartTime >= CONFIRMATION_BEEP_DURATION_MS)) {
    digitalWrite(BUZZER_PIN, LOW);
    confirmationBeepActive = false;
  }

  if (isTouchActive && (alarmUI.inAdjustMode || timerUI.inAdjustMode) && !isAlarmSounding) {
    if (millis() - touchPressStartTime > 500) {
      if (millis() - lastAutoIncrementTime > AUTO_INCREMENT_INTERVAL_MS) {
        lastAutoIncrementTime = millis();
        TS_Point p = touchscreen.getPoint();
        int16_t currentX = map(p.x, calData.x_min, calData.x_max, 0, SCREEN_WIDTH);
        int16_t currentY = map(p.y, calData.y_min, calData.y_max, 0, SCREEN_HEIGHT);
        handleValueAdjustment(currentX, currentY, true);
      }
    }
  }

  if (newDataIsAvailable) {
    newDataIsAvailable = false;
    isFetchingWeather = false;
    Serial.println("[Core 1] New weather data has arrived from Core 0.");
    if (currentPage == WEATHER_PAGE || currentPage == HOURLY_GRAPH_PAGE) {
      Serial.println("[Core 1] User is on a weather screen. Flagging for an immediate redraw.");
      needsRedraw = true;
    }
  }

  if (cityListVisible) {
    return;
  }

  if (needsRedraw) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      Serial.println("[Core 1] Mutex acquired. Redrawing screen for page: " + String(currentPage));

      if (currentPage != CLOCK_PAGE) {
        if (clockSprite.created()) {
          clockSprite.deleteSprite();
          Serial.println("[Memory] Cleared clockSprite.");
        }
        if (digitalClockSprite.created()) {
          digitalClockSprite.deleteSprite();
          Serial.println("[Memory] Cleared digitalClockSprite.");
        }
        if (beepIconSprite.created()) {
          beepIconSprite.deleteSprite();
          Serial.println("[Memory] Cleared beepIconSprite.");
        }
      }

      switch (currentPage) {
        case WEATHER_PAGE: drawWeatherScreen(); break;
        case HOURLY_GRAPH_PAGE: drawHourlyGraphPage(); break;
        case CLOCK_PAGE: drawClockPage(); break;
        case CALENDAR_PAGE:
          {
            struct tm timeinfo = {};
            if (getSafeLocalTime(&timeinfo, 5)) drawCalendarPage(timeinfo);
            break;
          }
        case ALARM_PAGE: drawAlarmPage(); break;
        case TIMER_PAGE: drawTimerPage(); break;
      }

      needsRedraw = false;
      xSemaphoreGive(dataMutex);
      Serial.println("[Core 1] Redraw complete. Mutex released.");
    } else {
      Serial.println("[Core 1] FAILED to acquire mutex for screen redraw. Will retry.");
    }
  }

  struct tm timeinfo = {};
  if (getSafeLocalTime(&timeinfo, 5)) {
    handleHourlyBeep();
    if (currentPage == WEATHER_PAGE) {
      static int lastMinute = -1;
      if (timeinfo.tm_min != lastMinute && !isFetchingWeather) {
        lastMinute = timeinfo.tm_min;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          drawHeader();
          xSemaphoreGive(dataMutex);
        }
      }
    } else if (currentPage == CLOCK_PAGE) {
      static unsigned long lastClockUpdate = 0;
      if (millis() - lastClockUpdate >= 1000) {
        lastClockUpdate = millis();
        updateAndDrawAnalogClock(true);
      }
    } else if (currentPage == TIMER_PAGE) {
      if (countdown.isRunning && !isAlarmSounding) {
        static unsigned long lastTimerUpdate = 0;
        if (millis() - lastTimerUpdate >= 1000) {
          lastTimerUpdate = millis();
          unsigned long elapsed = (millis() - countdown.startTime) + countdown.pauseOffsetMs;
          unsigned long remainingMillis = (elapsed < countdown.durationSetMs) ? countdown.durationSetMs - elapsed : 0;
          updateAndAnimateCountdownDisplay(remainingMillis, false);
        }
      }
    }
  }

  if ((millis() - lastDataFetchTimestamp > WEATHER_REFRESH_INTERVAL_MS) && !isFetchingWeather) {
    if (currentPage == WEATHER_PAGE || currentPage == HOURLY_GRAPH_PAGE) {
      Serial.println("[Core 1] Weather data is stale. Auto-refreshing on current page.");
      isFetchingWeather = true;
      xSemaphoreGive(fetchRequestSignal);
    }
  }
}

// Saves the current alarm settings to persistent memory.
void saveAlarms() {
  prefs.begin("weather-app", false);
  prefs.putBytes("alarms", &alarms, sizeof(alarms));
  prefs.end();
  Serial.println("[Prefs] Saved all alarm settings.");
}

// Loads alarm settings from memory on startup.
void loadAlarms() {
  prefs.begin("weather-app", false);
  if (prefs.isKey("alarms") && prefs.getBytesLength("alarms") == sizeof(alarms)) {
    prefs.getBytes("alarms", &alarms, sizeof(alarms));
    Serial.println("[Prefs] Loaded alarm settings.");
  } else {
    Serial.println("[Prefs] No saved alarms found, using defaults.");
  }
  prefs.end();
}

static bool alarmBlinkState = false;

// Checks if any alarms or timers should be triggered. Called continuously from loop().
void checkAlarmsAndTimers() {
  static int lastMinuteChecked = -1;
  static bool hasTriggeredThisMinute = false;

  struct tm timeinfo;
  if (!getSafeLocalTime(&timeinfo, 5)) {
    return;
  }

  if (isAlarmSounding) {
    if (timeinfo.tm_min != alarmTriggerTime.tm_min) {
      Serial.println("Alarm timed out after 1 minute.");
      isAlarmSounding = false;
      mainAlarmActive = false;
      tft.invertDisplay(invertDisplay);
      return;
    }
    if (millis() - alarmSoundStartTime > 300) {
      alarmSoundStartTime = millis();
      //tft.invertDisplay(!tft.getSwapBytes());
      alarmBlinkState = !alarmBlinkState;  // Toggle the state
      tft.invertDisplay(alarmBlinkState);  // Apply the new state
    }
    return;
  } else {
    tft.invertDisplay(invertDisplay);
  }

  if (timeinfo.tm_min != lastMinuteChecked) {
    lastMinuteChecked = timeinfo.tm_min;
    hasTriggeredThisMinute = false;
  }

  if (!hasTriggeredThisMinute) {
    for (int i = 0; i < MAX_ALARMS; i++) {
      if (alarms[i].isEnabled && alarms[i].hour == timeinfo.tm_hour && alarms[i].minute == timeinfo.tm_min) {
        Serial.printf(">>> ALARM %d TRIGGERED! <<<\n", i);
        isAlarmSounding = true;
        mainAlarmActive = true;
        alarmBlinkState = !invertDisplay;
        tft.invertDisplay(alarmBlinkState);
        alarmSoundStartTime = millis();
        memcpy(&alarmTriggerTime, &timeinfo, sizeof(struct tm));
        hasTriggeredThisMinute = true;
        return;
      }
    }
  }

  if (countdown.isRunning) {
    unsigned long elapsed = (millis() - countdown.startTime) + countdown.pauseOffsetMs;
    if (elapsed >= countdown.durationSetMs) {
      Serial.println(">>> COUNTDOWN FINISHED! <<<");
      countdown.isRunning = false;
      countdown.durationSetMs = 0;
      countdown.pauseOffsetMs = 0;
      isAlarmSounding = true;
      mainAlarmActive = true;
      alarmBlinkState = !invertDisplay;
      tft.invertDisplay(alarmBlinkState);
      alarmSoundStartTime = millis();
      memcpy(&alarmTriggerTime, &timeinfo, sizeof(struct tm));
    }
  }
}

void drawAlarmPage() {
  const int HIGHLIGHT_HOUR_X = 30, HIGHLIGHT_HOUR_W = 50;
  const int HIGHLIGHT_MIN_X = 80, HIGHLIGHT_MIN_W = 50;
  const int HIGHLIGHT_AMPM_X = 125, HIGHLIGHT_AMPM_W = 40;
  const int ARROW_HOUR_X = HIGHLIGHT_HOUR_X + HIGHLIGHT_HOUR_W / 2;
  const int ARROW_MIN_X = HIGHLIGHT_MIN_X + HIGHLIGHT_MIN_W / 2;
  const int ARROW_AMPM_X = HIGHLIGHT_AMPM_X + HIGHLIGHT_AMPM_W / 2;

  tft.fillScreen(COLOR_BACKGROUND);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_HIGH_TEMP, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.drawString("Alarms", SCREEN_WIDTH / 2, 8);

  int itemHeight = 42;
  int itemWidth = SCREEN_WIDTH - 20;
  int y_pos = 35;
  int y_gap = 10;

  for (int i = 0; i < MAX_ALARMS; i++) {
    uint16_t bgColor = COLOR_BACKGROUND;
    uint16_t borderColor = COLOR_DIVIDER_LINE;
    if (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == i) {
      bgColor = 0x3186;
      borderColor = CLOCK_COLOR_ACCENT;
    }
    tft.fillRoundRect(10, y_pos, itemWidth, itemHeight, 8, bgColor);
    tft.drawRoundRect(10, y_pos, itemWidth, itemHeight, 8, borderColor);

    char timeBuf[6];
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_TEXT, bgColor);

    if (use24HourFormat) {
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarms[i].hour, alarms[i].minute);
      tft.drawString(timeBuf, 80, y_pos + itemHeight / 2 + 2);
    } else {
      int displayHour = alarms[i].hour % 12;
      if (displayHour == 0) displayHour = 12;
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", displayHour, alarms[i].minute);
      tft.drawString(timeBuf, 80, y_pos + itemHeight / 2 + 2);
      tft.setTextSize(2);
      tft.drawString(alarms[i].isPM ? "PM" : "AM", 145, y_pos + itemHeight / 2 + 1);
    }

    bool isEnabled = alarms[i].isEnabled;
    uint16_t toggleColor = isEnabled ? CLOCK_COLOR_HOUR_HAND : TFT_DARKGREY;
    tft.fillRoundRect(220, y_pos + 6, 80, itemHeight - 12, 5, toggleColor);
    tft.setTextColor(COLOR_TEXT, toggleColor);
    tft.setTextSize(2);
    tft.drawString(isEnabled ? "ON" : "OFF", 260, y_pos + itemHeight / 2 + 1);

    if (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == i) {
      if (alarmUI.adjustingField == 1) tft.drawRect(HIGHLIGHT_HOUR_X, y_pos + 4, HIGHLIGHT_HOUR_W, itemHeight - 8, CLOCK_COLOR_ACCENT);
      if (alarmUI.adjustingField == 2) tft.drawRect(HIGHLIGHT_MIN_X, y_pos + 4, HIGHLIGHT_MIN_W, itemHeight - 8, CLOCK_COLOR_ACCENT);
      if (alarmUI.adjustingField == 3 && !use24HourFormat) tft.drawRect(HIGHLIGHT_AMPM_X, y_pos + 4, HIGHLIGHT_AMPM_W, itemHeight - 8, CLOCK_COLOR_ACCENT);

      tft.fillTriangle(ARROW_HOUR_X, y_pos + 4, ARROW_HOUR_X - 6, y_pos + 12, ARROW_HOUR_X + 6, y_pos + 12, COLOR_ARROW);
      tft.fillTriangle(ARROW_MIN_X, y_pos + 4, ARROW_MIN_X - 6, y_pos + 12, ARROW_MIN_X + 6, y_pos + 12, COLOR_ARROW);
      if (!use24HourFormat) tft.fillTriangle(ARROW_AMPM_X, y_pos + 4, ARROW_AMPM_X - 6, y_pos + 12, ARROW_AMPM_X + 6, y_pos + 12, COLOR_ARROW);

      tft.fillTriangle(ARROW_HOUR_X, y_pos + itemHeight - 4, ARROW_HOUR_X - 6, y_pos + itemHeight - 12, ARROW_HOUR_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
      tft.fillTriangle(ARROW_MIN_X, y_pos + itemHeight - 4, ARROW_MIN_X - 6, y_pos + itemHeight - 12, ARROW_MIN_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
      if (!use24HourFormat) tft.fillTriangle(ARROW_AMPM_X, y_pos + itemHeight - 4, ARROW_AMPM_X - 6, y_pos + itemHeight - 12, ARROW_AMPM_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
    }
    y_pos += itemHeight + y_gap;
  }
}

void handleAlarmPageTouch(int16_t touchX, int16_t touchY) {
  int itemHeight = 42, y_gap = 10, y_pos = 35;
  bool tapHandled = false;

  for (int i = 0; i < MAX_ALARMS; i++) {
    if (touchY >= y_pos && touchY <= y_pos + itemHeight) {
      tapHandled = true;
      if (touchX >= 220 && touchX <= 300) {
        alarms[i].isEnabled = !alarms[i].isEnabled;
        if (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == i) alarmUI.inAdjustMode = false;
        saveAlarms();
        redrawAlarmItem(i);
        return;
      }
      if (touchX >= 10 && touchX < 220) {
        int oldAdjustingIndex = alarmUI.adjustingAlarmIndex;
        alarmUI.inAdjustMode = true;
        alarmUI.adjustingAlarmIndex = i;
        if (oldAdjustingIndex != -1 && oldAdjustingIndex != i) redrawAlarmItem(oldAdjustingIndex);

        if (use24HourFormat) {
          if (touchX < 81) alarmUI.adjustingField = 1;
          else alarmUI.adjustingField = 2;
        } else {
          if (touchX < 81) alarmUI.adjustingField = 1;
          else if (touchX < 131) alarmUI.adjustingField = 2;
          else alarmUI.adjustingField = 3;
        }

        redrawAlarmItem(i);
        return;
      }
    }
    y_pos += itemHeight + y_gap;
  }

  if (alarmUI.inAdjustMode && !tapHandled) {
    int oldIndex = alarmUI.adjustingAlarmIndex;
    alarmUI.inAdjustMode = false;
    alarmUI.adjustingAlarmIndex = -1;
    saveAlarms();
    if (oldIndex != -1) redrawAlarmItem(oldIndex);
  }
}

// Draws the Countdown Timer page UI.
void drawTimerPage() {
  tft.fillScreen(COLOR_BACKGROUND);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COLOR_HIGH_TEMP, COLOR_BACKGROUND);
  tft.setTextSize(2);
  tft.drawString("Timer", SCREEN_WIDTH / 2, 8);

  int btnY = 175, btnH = 50, btnW = 120, btnGap = 20;
  int btnX_Start = (SCREEN_WIDTH / 2) - btnW - (btnGap / 2);
  int btnX_Reset = (SCREEN_WIDTH / 2) + (btnGap / 2);
  bool isRunning = countdown.isRunning;
  uint16_t startBtnColor = isRunning ? COLOR_FEELS_LIKE : CLOCK_COLOR_HOUR_HAND;
  tft.fillRoundRect(btnX_Start, btnY, btnW, btnH, 8, startBtnColor);
  tft.setTextColor(COLOR_TEXT, startBtnColor);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(3);
  tft.drawString(isRunning ? "PAUSE" : "START", btnX_Start + btnW / 2, btnY + btnH / 2 + 2);
  tft.fillRoundRect(btnX_Reset, btnY, btnW, btnH, 8, CLOCK_COLOR_MIN_HAND);
  tft.setTextColor(COLOR_TEXT, CLOCK_COLOR_MIN_HAND);
  tft.drawString("RESET", btnX_Reset + btnW / 2, btnY + btnH / 2 + 2);

  updateAndAnimateCountdownDisplay(countdown.durationSetMs, true);
}

// Handles touch events on the Timer page.
void handleTimerPageTouch(int16_t touchX, int16_t touchY) {
  int btnY = 175, btnH = 50, btnW = 120, btnGap = 20;
  int btnX_Start = (SCREEN_WIDTH / 2) - btnW - (btnGap / 2);
  int btnX_Reset = (SCREEN_WIDTH / 2) + (btnGap / 2);

  if (touchY >= btnY && touchY <= btnY + btnH) {
    if (touchX >= btnX_Start && touchX <= btnX_Start + btnW) {
      if (countdown.isRunning) {
        countdown.isRunning = false;
        countdown.pauseOffsetMs += millis() - countdown.startTime;
      } else if (countdown.durationSetMs > 0) {
        countdown.isRunning = true;
        countdown.startTime = millis();
      }
      timerUI.inAdjustMode = false;
      needsRedraw = true;
      return;
    }
    if (touchX >= btnX_Reset && touchX <= btnX_Reset + btnW) {
      countdown.isRunning = false;
      countdown.durationSetMs = 0;
      countdown.pauseOffsetMs = 0;
      timerUI.inAdjustMode = true;
      timerUI.adjustingField = 0;
      needsRedraw = true;
      return;
    }
  }

  tft.setTextSize(6);
  int digitPairWidth = tft.textWidth("00"), colonWidth = tft.textWidth(":");
  int totalWidth = (digitPairWidth * 3) + (colonWidth * 2);
  int startX = (SCREEN_WIDTH - totalWidth) / 2;
  int hourX = startX, minX = startX + digitPairWidth + colonWidth, secX = minX + digitPairWidth + colonWidth;
  if (touchY >= 30 && touchY <= 130) {
    timerUI.inAdjustMode = true;
    if (countdown.isRunning) {
      countdown.isRunning = false;
      countdown.pauseOffsetMs += millis() - countdown.startTime;
      needsRedraw = true;
    }
    if (touchX >= hourX && touchX < hourX + digitPairWidth) timerUI.adjustingField = 1;
    else if (touchX >= minX && touchX < minX + digitPairWidth) timerUI.adjustingField = 2;
    else if (touchX >= secX && touchX < secX + digitPairWidth) timerUI.adjustingField = 3;
    else {
      timerUI.inAdjustMode = false;
      timerUI.adjustingField = 0;
    }
    updateAndAnimateCountdownDisplay(countdown.durationSetMs, true);
    return;
  }

  if (timerUI.inAdjustMode) {
    timerUI.inAdjustMode = false;
    timerUI.adjustingField = 0;
    updateAndAnimateCountdownDisplay(countdown.durationSetMs, true);
  }
}

void handleValueAdjustment(int16_t touchX, int16_t touchY, bool isHold) {
  int increment = isHold ? 5 : 1;
  bool needsRedrawLocal = false;

  if (currentPage == ALARM_PAGE && alarmUI.inAdjustMode) {
    const int ARROW_HOUR_X = 55, ARROW_MIN_X = 105, ARROW_AMPM_X = 145;
    const int ARROW_TOUCH_RADIUS = 15;

    int i = alarmUI.adjustingAlarmIndex;
    int itemTop = 35 + i * (42 + 10);
    int itemMidY = itemTop + 21;

    if (touchY > itemTop && touchY < itemMidY) {
      if (touchX > ARROW_HOUR_X - ARROW_TOUCH_RADIUS && touchX < ARROW_HOUR_X + ARROW_TOUCH_RADIUS) {
        alarms[i].hour = (alarms[i].hour + increment) % 24;
        needsRedrawLocal = true;
      }
      if (touchX > ARROW_MIN_X - ARROW_TOUCH_RADIUS && touchX < ARROW_MIN_X + ARROW_TOUCH_RADIUS) {
        alarms[i].minute = (alarms[i].minute + increment) % 60;
        needsRedrawLocal = true;
      }
      if (!use24HourFormat && touchX > ARROW_AMPM_X - ARROW_TOUCH_RADIUS && touchX < ARROW_AMPM_X + ARROW_TOUCH_RADIUS) {
        if (!isHold) {
          alarms[i].hour = (alarms[i].hour + 12) % 24;
          needsRedrawLocal = true;
        }
      }
    } else if (touchY > itemMidY && touchY < itemTop + 42) {
      if (touchX > ARROW_HOUR_X - ARROW_TOUCH_RADIUS && touchX < ARROW_HOUR_X + ARROW_TOUCH_RADIUS) {
        alarms[i].hour = (alarms[i].hour - increment + 24) % 24;
        needsRedrawLocal = true;
      }
      if (touchX > ARROW_MIN_X - ARROW_TOUCH_RADIUS && touchX < ARROW_MIN_X + ARROW_TOUCH_RADIUS) {
        alarms[i].minute = (alarms[i].minute - increment + 60) % 60;
        needsRedrawLocal = true;
      }
      if (!use24HourFormat && touchX > ARROW_AMPM_X - ARROW_TOUCH_RADIUS && touchX < ARROW_AMPM_X + ARROW_TOUCH_RADIUS) {
        if (!isHold) {
          alarms[i].hour = (alarms[i].hour + 12) % 24;
          needsRedrawLocal = true;
        }
      }
    }
    if (needsRedrawLocal) {
      alarms[i].isPM = (alarms[i].hour >= 12);
      redrawAlarmItem(i);
    }

  } else if (currentPage == TIMER_PAGE && timerUI.inAdjustMode) {
    unsigned long amount = (unsigned long)increment;
    bool valueChanged = false;
    int yPos = 85;
    tft.setTextSize(6);
    int digitPairHeight = tft.fontHeight();
    int yBoxTop = yPos - (digitPairHeight / 2);
    if (touchY > yBoxTop - 40 && touchY < yBoxTop) {
      if (timerUI.adjustingField == 1) {
        countdown.durationSetMs += amount * 3600000;
        valueChanged = true;
      } else if (timerUI.adjustingField == 2) {
        countdown.durationSetMs += amount * 60000;
        valueChanged = true;
      } else if (timerUI.adjustingField == 3) {
        countdown.durationSetMs += amount * 1000;
        valueChanged = true;
      }
    } else if (touchY > yBoxTop + digitPairHeight - 10 && touchY < yBoxTop + digitPairHeight + 20) {
      if (timerUI.adjustingField == 1) {
        unsigned long change = amount * 3600000;
        countdown.durationSetMs = (countdown.durationSetMs >= change) ? countdown.durationSetMs - change : 0;
        valueChanged = true;
      } else if (timerUI.adjustingField == 2) {
        unsigned long change = amount * 60000;
        countdown.durationSetMs = (countdown.durationSetMs >= change) ? countdown.durationSetMs - change : 0;
        valueChanged = true;
      } else if (timerUI.adjustingField == 3) {
        unsigned long change = amount * 1000;
        countdown.durationSetMs = (countdown.durationSetMs >= change) ? countdown.durationSetMs - change : 0;
        valueChanged = true;
      }
    }
    if (countdown.durationSetMs > 359999000UL) countdown.durationSetMs = 359999000UL;
    if (valueChanged) updateAndAnimateCountdownDisplay(countdown.durationSetMs, true);
  }
}

// Animates the large countdown timer display, optimizing for minimal redraws.
void updateAndAnimateCountdownDisplay(unsigned long remainingMillis, bool forceRedraw = false) {
  unsigned long totalSeconds = remainingMillis / 1000;
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  char currentHour[3], currentMinute[3], currentSecond[3];
  sprintf(currentHour, "%02d", hours);
  sprintf(currentMinute, "%02d", minutes);
  sprintf(currentSecond, "%02d", seconds);

  static bool prevTimerInAdjustMode = false;
  static int prevTimerAdjustingField = -1;
  bool timeChanged = strcmp(prevCountdownState.prevHour, currentHour) != 0 || strcmp(prevCountdownState.prevMinute, currentMinute) != 0 || strcmp(prevCountdownState.prevSecond, currentSecond) != 0;
  bool adjustStateChanged = (timerUI.inAdjustMode != prevTimerInAdjustMode) || (timerUI.adjustingField != prevTimerAdjustingField);

  if (!timeChanged && !adjustStateChanged && !forceRedraw) return;

  int yPos = 85;
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(6);
  int digitPairWidth = tft.textWidth("00"), digitPairHeight = tft.fontHeight(), singleDigitWidth = tft.textWidth("0"), colonWidth = tft.textWidth(":");
  int totalWidth = (digitPairWidth * 3) + (colonWidth * 2);
  int startX = (SCREEN_WIDTH - totalWidth) / 2;
  int yBoxTop = yPos - (digitPairHeight / 2);
  int hourX = startX, colon1X = hourX + digitPairWidth, minX = colon1X + colonWidth, colon2X = minX + digitPairWidth, secX = colon2X + colonWidth;
  const int animationSteps = 8, animationDelay = 15, rollDistance = 15;

  if (adjustStateChanged || forceRedraw) {
    tft.fillRect(0, 30, SCREEN_WIDTH, 140, COLOR_BACKGROUND);
    tft.setTextColor(COLOR_TEXT);
    tft.drawString(currentHour, hourX + digitPairWidth / 2, yPos);
    tft.drawString(currentMinute, minX + digitPairWidth / 2, yPos);
    tft.drawString(currentSecond, secX + digitPairWidth / 2, yPos);
    tft.drawString(":", colon1X + colonWidth / 2, yPos - 4);
    tft.drawString(":", colon2X + colonWidth / 2, yPos - 4);
    if (timerUI.inAdjustMode && timerUI.adjustingField != 0) {
      int highlightX = 0;
      if (timerUI.adjustingField == 1) highlightX = hourX;
      else if (timerUI.adjustingField == 2) highlightX = minX;
      else if (timerUI.adjustingField == 3) highlightX = secX;
      tft.drawRect(highlightX - 2, yBoxTop - 2, digitPairWidth + 4, digitPairHeight + 4, CLOCK_COLOR_ACCENT);
      int arrowCenterX = highlightX + digitPairWidth / 2, upArrowY = yBoxTop - 17, downArrowY = yBoxTop + digitPairHeight + 15;
      tft.fillTriangle(arrowCenterX, upArrowY, arrowCenterX - 10, upArrowY + 15, arrowCenterX + 10, upArrowY + 15, COLOR_ARROW);
      tft.fillTriangle(arrowCenterX, downArrowY, arrowCenterX - 10, downArrowY - 15, arrowCenterX + 10, downArrowY - 15, COLOR_ARROW);
    }
  } else if (timeChanged) {
    for (int step = 0; step <= animationSteps; step++) {
      int halfSteps = animationSteps / 2;
      int offset;
      char prevDigit[2], currentDigit[2];
      for (int i = 0; i < 2; i++) {
        int digitX = hourX + (i * singleDigitWidth) + (singleDigitWidth / 2);
        if (prevCountdownState.prevHour[i] != currentHour[i]) {
          tft.fillRect(digitX - singleDigitWidth / 2, yBoxTop - rollDistance, singleDigitWidth, digitPairHeight + (2 * rollDistance), COLOR_BACKGROUND);
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevCountdownState.prevHour[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentHour[i];
          currentDigit[1] = '\0';
          tft.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, yPos + offset);
        }
      }
      for (int i = 0; i < 2; i++) {
        int digitX = minX + (i * singleDigitWidth) + (singleDigitWidth / 2);
        if (prevCountdownState.prevMinute[i] != currentMinute[i]) {
          tft.fillRect(digitX - singleDigitWidth / 2, yBoxTop - rollDistance, singleDigitWidth, digitPairHeight + (2 * rollDistance), COLOR_BACKGROUND);
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevCountdownState.prevMinute[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentMinute[i];
          currentDigit[1] = '\0';
          tft.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, yPos + offset);
        }
      }
      for (int i = 0; i < 2; i++) {
        int digitX = secX + (i * singleDigitWidth) + (singleDigitWidth / 2);
        if (prevCountdownState.prevSecond[i] != currentSecond[i]) {
          tft.fillRect(digitX - singleDigitWidth / 2, yBoxTop - rollDistance, singleDigitWidth, digitPairHeight + (2 * rollDistance), COLOR_BACKGROUND);
          offset = (step <= halfSteps) ? map(step, 0, halfSteps, 0, -rollDistance) : map(step, halfSteps + 1, animationSteps, rollDistance, 0);
          prevDigit[0] = prevCountdownState.prevSecond[i];
          prevDigit[1] = '\0';
          currentDigit[0] = currentSecond[i];
          currentDigit[1] = '\0';
          tft.drawString((step <= halfSteps) ? prevDigit : currentDigit, digitX, yPos + offset);
        }
      }
      delay(animationDelay);
    }
    if (strcmp(prevCountdownState.prevHour, currentHour) != 0) tft.drawString(currentHour, hourX + digitPairWidth / 2, yPos);
    if (strcmp(prevCountdownState.prevMinute, currentMinute) != 0) tft.drawString(currentMinute, minX + digitPairWidth / 2, yPos);
    if (strcmp(prevCountdownState.prevSecond, currentSecond) != 0) tft.drawString(currentSecond, secX + digitPairWidth / 2, yPos);
  }

  strcpy(prevCountdownState.prevHour, currentHour);
  strcpy(prevCountdownState.prevMinute, currentMinute);
  strcpy(prevCountdownState.prevSecond, currentSecond);
  prevTimerInAdjustMode = timerUI.inAdjustMode;
  prevTimerAdjustingField = timerUI.adjustingField;
}

void redrawAlarmItem(int index) {
  const int HIGHLIGHT_HOUR_X = 30, HIGHLIGHT_HOUR_W = 50;
  const int HIGHLIGHT_MIN_X = 80, HIGHLIGHT_MIN_W = 50;
  const int HIGHLIGHT_AMPM_X = 125, HIGHLIGHT_AMPM_W = 40;
  const int ARROW_HOUR_X = HIGHLIGHT_HOUR_X + HIGHLIGHT_HOUR_W / 2;
  const int ARROW_MIN_X = HIGHLIGHT_MIN_X + HIGHLIGHT_MIN_W / 2;
  const int ARROW_AMPM_X = HIGHLIGHT_AMPM_X + HIGHLIGHT_AMPM_W / 2;

  int itemHeight = 42;
  int itemWidth = SCREEN_WIDTH - 20;
  int y_pos = 35 + index * (itemHeight + 10);

  uint16_t bgColor = (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == index) ? 0x3186 : COLOR_BACKGROUND;
  uint16_t borderColor = (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == index) ? CLOCK_COLOR_ACCENT : COLOR_DIVIDER_LINE;

  tft.fillRoundRect(10, y_pos, itemWidth, itemHeight, 8, bgColor);
  tft.drawRoundRect(10, y_pos, itemWidth, itemHeight, 8, borderColor);

  char timeBuf[6];
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(COLOR_TEXT, bgColor);

  if (use24HourFormat) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarms[index].hour, alarms[index].minute);
    tft.drawString(timeBuf, 80, y_pos + itemHeight / 2 + 2);
  } else {
    int displayHour = alarms[index].hour % 12;
    if (displayHour == 0) displayHour = 12;
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", displayHour, alarms[index].minute);
    tft.drawString(timeBuf, 80, y_pos + itemHeight / 2 + 2);
    tft.setTextSize(2);
    tft.drawString(alarms[index].isPM ? "PM" : "AM", 145, y_pos + itemHeight / 2 + 1);
  }

  // ... code continues from the previous response ...

  bool isEnabled = alarms[index].isEnabled;
  uint16_t toggleColor = isEnabled ? CLOCK_COLOR_HOUR_HAND : TFT_DARKGREY;
  tft.fillRoundRect(220, y_pos + 6, 80, itemHeight - 12, 5, toggleColor);
  tft.setTextColor(COLOR_TEXT, toggleColor);
  tft.setTextSize(2);
  tft.drawString(isEnabled ? "ON" : "OFF", 260, y_pos + itemHeight / 2 + 1);

  // --- RESUMING FROM HERE ---
  if (alarmUI.inAdjustMode && alarmUI.adjustingAlarmIndex == index) {
    if (alarmUI.adjustingField == 1) {
      tft.drawRect(HIGHLIGHT_HOUR_X, y_pos + 4, HIGHLIGHT_HOUR_W, itemHeight - 8, CLOCK_COLOR_ACCENT);
      tft.fillTriangle(ARROW_HOUR_X, y_pos + 4, ARROW_HOUR_X - 6, y_pos + 12, ARROW_HOUR_X + 6, y_pos + 12, COLOR_ARROW);
      tft.fillTriangle(ARROW_HOUR_X, y_pos + itemHeight - 4, ARROW_HOUR_X - 6, y_pos + itemHeight - 12, ARROW_HOUR_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
    } else if (alarmUI.adjustingField == 2) {
      tft.drawRect(HIGHLIGHT_MIN_X, y_pos + 4, HIGHLIGHT_MIN_W, itemHeight - 8, CLOCK_COLOR_ACCENT);
      tft.fillTriangle(ARROW_MIN_X, y_pos + 4, ARROW_MIN_X - 6, y_pos + 12, ARROW_MIN_X + 6, y_pos + 12, COLOR_ARROW);
      tft.fillTriangle(ARROW_MIN_X, y_pos + itemHeight - 4, ARROW_MIN_X - 6, y_pos + itemHeight - 12, ARROW_MIN_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
    } else if (alarmUI.adjustingField == 3 && !use24HourFormat) {
      tft.drawRect(HIGHLIGHT_AMPM_X, y_pos + 4, HIGHLIGHT_AMPM_W, itemHeight - 8, CLOCK_COLOR_ACCENT);
      tft.fillTriangle(ARROW_AMPM_X, y_pos + 4, ARROW_AMPM_X - 6, y_pos + 12, ARROW_AMPM_X + 6, y_pos + 12, COLOR_ARROW);
      tft.fillTriangle(ARROW_AMPM_X, y_pos + itemHeight - 4, ARROW_AMPM_X - 6, y_pos + itemHeight - 12, ARROW_AMPM_X + 6, y_pos + itemHeight - 12, COLOR_ARROW);
    }
  }
}

void ensureHomeTimezoneIsSet() {
  if (xSemaphoreTake(timezoneMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    const char* current_tz = getenv("TZ");
    if (current_tz == NULL || strcmp(system_tz_string.c_str(), current_tz) != 0) {
      setenv("TZ", system_tz_string.c_str(), 1);
      tzset();
    }
    xSemaphoreGive(timezoneMutex);
  }
}
