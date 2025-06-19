/*
 * *******************************************************
 * Intranet Wi-Fi Credentials
 * *******************************************************
 * Replace the values below with your home or intranet Wi-Fi credentials. 
 * The ESP-07 will use these credentials to connect to your local Wi-Fi 
 * network for debugging, accessing internal resources, or enabling 
 * communication with other devices on your network.
 * *******************************************************
 */
#define LOCAL_SSID "MyWiFiNetwork"  	// Generic example SSID
#define LOCAL_PASS "password1234"   	// Generic example password



/*
 * *******************************************************
 * Access Point (AP) Configuration for the ESP-07
 * *******************************************************
 * When the ESP-07 is in AP mode, it creates its own Wi-Fi network.
 * Devices can connect to this network to communicate with the ESP-07, 
 * especially useful in cases where there is no external Wi-Fi network 
 * available or for setting up a configuration mode.
 * 
 * The SSID (AP_SSID) is the name of the Wi-Fi network created by the ESP-07, 
 * and AP_PASS is the password required to connect to it. 
 * 
 * This setup allows the ESP-07 to act as an access point (e.g., for 
 * configuration or direct device communication).
 * *******************************************************
 */

#define AP_SSID "ESP-07-Access-Point"
#define AP_PASS "123456789"
