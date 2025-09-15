ESP32 Mini Weather Station Features & Functionality List


Version: Final (ISR-Based Touch) - September 16, 2025

Core User Interface & Navigation
• Multi-Page Touch Interface: A modern, responsive UI with five distinct pages: Weather,
Hourly Graph, Clock, Calendar, and Exchange Rates.
• Swipe Navigation: Users can swipe left or right anywhere on the screen to cycle through
pages fluidly (swipe threshold: 60 pixels).
• ISR-Based Touch: Interrupt Service Routine (ISR) for touchscreen handling ensures maximum
responsiveness and low latency.
• Automatic Screen Redraw: The display redraws only when needed (e.g., data updates or
page changes) to prevent flickering.
• Touch Debounce & Long-Press: 20ms debounce for stable interactions; long-press (15
seconds) enters WiFi configuration mode.
• UI Cooldown: 300ms debounce after interactions to prevent accidental ”click-through”
taps.
1. Main Weather Display
• Real-time Weather Data: Fetches and displays current weather conditions from the Met.no
API, a professional meteorological source.
• Dynamic Location Name: Automatically determines and displays the town/city name based
on latitude and longitude using the OpenStreetMap API.
• Comprehensive Current Conditions:
– Current Temperature
– “Feels Like” Temperature (calculated based on humidity for heat index or wind chill for
cold conditions)
– Detailed Weather Description (e.g., “Partly Cloudy,” “Light Rain”)
– Large, clear weather icon that changes based on conditions (day/night aware, loaded
from SD card in 64x64 or 32x32 BMP formats)
• Detailed Information Panel: A right-side panel provides at-a-glance meteorological data:
– Sunrise and Sunset times, calculated for the specific location and date
– Humidity (%)
– Wind Speed (m/s or km/h) and Cardinal Direction (e.g., N, SW, ENE)
Page 1 of 5
ESP32 Mini Weather Station Features Functionality List
– Barometric Pressure (hPa)
– Cloud Cover (%)
– UV Index
– Air Quality Index (AQI) from the Met.no API
• 7-Day Forecast: A bottom strip shows the forecast for the next seven days, including
weather icon, day of the week, and projected high/low temperatures.
• Temperature Unit Toggle: Tapping the thermometer icon toggles all temperature displays
between Celsius and Fahrenheit, with the setting saved and remembered after a restart.
• New Features Difference (from Previous Versions):
– Enhanced Feels-Like Calculation: Now includes precise heat index for hot/humid conditions
and wind chill for cold/windy scenarios.
– Auto-Icon Directory Detection: Dynamically selects large/small icon directories from SD
card for flexibility.
– Data Validation & Retries: Up to 10 network retries with 5-second delays for robust
fetching; enters AP mode on persistent failures.
1.5 Hourly Forecast Graph Page (New Feature)
• 24-Hour Temperature Graph: A line chart displaying projected temperatures for the next
24 hours, sampled every 3 hours for clarity.
• Visual Elements: Temperature points with icons above (from SD card), labels in C/F, and
12-hour time stamps (AM/PM).
• Auto-Scaling: Dynamic range calculation with padding for min/max temps; requires at
least 2 data points.
• Data Parsing: Extracted from Met.no timeseries array for hourly temperature, icons, and
times.
• Graph Layout: Reserved top clearance for icons/text; yellow accent lines connect points.
• New Features Difference:
– This is an entirely new page, adding visual forecasting not present in prior versions.
– Supports partial views (0-11h or 12-23h) for detailed analysis.
2. World Clock & Time Display Page
• Dual Clock Display: Features a classic analog clock on the left and a detailed digital clock
on the right.
• Animated Analog Clock: A beautifully rendered analog clock with sweeping hour, minute,
and second hands (colored: green hour, magenta minute, blue second).

ESP32 Mini Weather Station Features Functionality List
• Animated Digital Clock: Displays Day of the Week, Date, Year, and Time (HH:MM:SS
AM/PM) with a smooth “rolling” animation for time changes, using partial screen updates
for efficiency.
• Sun/Moon Position Arc: A dotted arc on the analog clock shows the sun or moon’s position
based on sunrise/sunset times.
– Day Mode: Yellow sun symbol.
– Night Mode: Moon symbol with accurate phase visualization (8 phases, e.g., New Moon,
Full Moon).
• World Clock Functionality:
– Displays time for a user-selected city from an expanded list of over 50 world cities (sorted
alphabetically, with UTC offsets like +09:30).
– Shows the city’s flag (from SD card), three-letter code, and timezone-adjusted time.
– Tapping the city/flag opens a scrollable pop-up menu (5 items per page) to select a
new city; supports up/down/set buttons and direct taps.
• Hourly Chime: An optional feature using the piezo buzzer to beep the number of hours at
the top of every hour (e.g., 3 beeps at 3:00 AM/PM; non-blocking via hardware timer).
• Chime Control: Tapping the bell icon toggles the hourly chime on/off, with the setting
saved and the icon displaying a slash when disabled; includes visual blinking during beeps.
• New Features Difference:
– Moon Phase Calculation: New astronomical computation using Julian Day for precise
phase fraction (0-1), named phases (e.g., Waxing Crescent), and hemisphere awareness
(Northern/Southern).
– Expanded Cities: More cities (e.g., Kiritimati +14:00, Midway -12:00) with POSIX
timezones for accurate DST handling.
– Remote Time Fetch: Temporarily sets TZ for city time without affecting system clock.
– Bell Icon Enhancements: Real-time blinking and confirmation beep (50ms) on toggle.
3. Calendar Page
• Full Monthly View: Displays a traditional calendar layout for the selected month and year.
• Current Day Highlight: The current date is highlighted with a colored square.
• Easy Navigation: Users can tap the < and > arrows to move to the previous or next
month.
• “Today” Button: Allows users to jump back to the current month if they have navigated
away.
• New Features Difference:
– Auto-Redraw on Day Change: Automatically updates if the day rolls over while on the
page.

ESP32 Mini Weather Station Features Functionality List
4. Exchange Rate Page
• Key-Free API: Uses the free Fawaz Ahmed Currency API, supporting a wide range of
currencies, including MMK, without requiring an API key.
• Major Currency List: Displays a sorted list of 20 major world currencies with their respective
country flags (from SD card).
• Page-by-Page Scrolling: Users can swipe vertically or tap up/down arrows to scroll through
the currency list a full page (5 items) at a time.
• Automatic Refresh: Exchange rate data updates in the background every 6 hours.
• New Features Difference:
– Data Validation: Checks for valid JSON and ”usd” base; retries up to 10 times on
failure.
– Scroll Offset Management: Smooth handling of list offsets for large lists.
Hardware & System Features
• Automatic Brightness Control: A Light Dependent Resistor (LDR) adjusts the screen’s
backlight based on ambient light for optimal viewing (averaged over 10 readings, PWM
resolution 8-bit).
• Audio Feedback: A piezo buzzer provides hourly chimes and short confirmation beeps for
user actions (non-blocking with 150ms beep/250ms pause intervals).
• Data Persistence: Uses the ESP32’s Preferences library to save settings after power-off:
– Wi-Fi Credentials
– Latitude & Longitude
– Timezone
– Selected World Clock City
– Fahrenheit/Celsius Preference
– Hourly Beep On/Off State
– Screen Inversion State
• SD Card Integration: Loads graphical assets (weather icons, country flags, startup logo)
from an SD card for easy customization; includes flag report generation for missing files.
• New Features Difference:
– Screen Inversion: Toggleable in config portal for normal/inverted colors (useful for different
TFT panels).
– Startup Logo: Displays a custom logo from SD during boot.
– Enhanced NTP Sync: Retries up to 10 times; uses multiple servers (pool.ntp.org,
time.cloudflare.com).

ESP32 Mini Weather Station Features Functionality List
Setup & Configuration
• WiFi Manager: Creates a Wi-Fi Access Point (“WeatherStationSetup”)ifunabletoconnecttoaknownnetwork.Accessibleviatheaccesspoint, allowingusersto :
• • Scan for and select their home Wi-Fi network
• Enter their Wi-Fi password
• Enter local Latitude, Longitude, and Timezone (with dropdown for POSIX strings)
• Select Screen Color (Invert/Normal)
Easy Re-configuration: Long-pressing the touchscreen for 15 seconds re-enters the configuration
portal for changing Wi-Fi or location.
New Features Difference:
• Custom HTML for Portal: Includes hidden fields, CSS for layout, and scripts for timezone/
inversion dropdowns.
• Save Callback: Triggers saving only when changes are made.
• Boot Messages: Displays progress (e.g., ”Fetching Weather... Attempt 1/10”) during
initial setup.
