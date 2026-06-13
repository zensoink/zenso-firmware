#ifndef PORTAL_HTML_H
#define PORTAL_HTML_H

#include <Arduino.h>

// --- Static HTML pages (no dynamic content) ---

static const char PROVISIONING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zenso Setup</title>
<style>
  body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto}
  label{display:block;margin:12px 0}
  input{width:100%;padding:8px;box-sizing:border-box;margin-top:4px}
  button{padding:10px 20px;font-size:16px;margin-top:12px}
</style>
</head>
<body>
<h2>Zenso Configuration</h2>
<form method="POST" action="/save" onsubmit="document.getElementById('btn').disabled=true;document.getElementById('btn').textContent='Connecting...';document.getElementById('status').style.display='block'">
  <label>WiFi SSID/Name<input name="ssid" type="text"></label>
  <label>Password<input name="password" type="password" maxlength="63"></label>
  <label>API URL<input name="api_url" type="text" placeholder="http://192.168.1.x:3000"></label>
  <div id="status" style="display:none;margin-top:12px;color:#666;font-size:14px">Connecting to WiFi and registering device...<br>This may take up to 20 seconds.</div>
  <button id="btn" type="submit">Save & Connect</button>
</form>
</body>
</html>
)rawliteral";

// Captive portal redirect snippet
static const char REDIRECT_HTML[] PROGMEM =
  "<html><meta http-equiv='refresh' content='0;url=/'><body>"
  "<a href='/'>Continue to setup</a></body></html>";

// --- Shared base for dynamic error pages ---

static inline String _base_html(const String& title, const String& body_content) {
  return "<!DOCTYPE html><html><head>"
         "<meta charset=\"UTF-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>" + title + "</title>"
         "<style>"
         "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
         "button{padding:10px 20px;font-size:16px;margin-top:12px;background:#f44336;color:#fff;border:none;border-radius:4px;cursor:pointer}"
         "a{word-break:break-all;color:#1976D2}"
         "</style></head><body>"
         + body_content +
         "</body></html>";
}

// --- Dynamic error page builders ---

static inline String portal_html_wifi_failed(const String& ssid) {
  return _base_html("Connection Failed",
    "<h2>WiFi Connection Failed</h2>"
    "<p>Could not connect to <strong>" + ssid + "</strong>.</p>"
    "<p>Check your WiFi credentials and try again.</p>"
    "<button onclick=\"window.location.href='/'\">Go Back</button>");
}

static inline String portal_html_server_error(int http_code) {
  return _base_html("Server Error",
    "<h2>Server Error</h2>"
    "<p>Failed to register device with server.</p>"
    "<p>HTTP status: " + String(http_code) + "</p>"
    "<button onclick=\"window.location.href='/'\">Go Back</button>");
}

static inline String portal_html_parse_error() {
  return _base_html("Parse Error",
    "<h2>Invalid Server Response</h2>"
    "<button onclick=\"window.location.href='/'\">Go Back</button>");
}

// --- Success page (different styling from error pages) ---

static inline String portal_html_success(const String& ssid, const String& ip,
                                         const String& claim_url,
                                         const String& claim_expires_at) {
  String expires_display = "Valid until " + claim_expires_at.substring(11, 16) + " UTC";
  return "<!DOCTYPE html><html><head>"
         "<meta charset=\"UTF-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>Setup Complete</title>"
         "<style>"
         "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
         "a{word-break:break-all;color:#1976D2}"
         "button{padding:12px 24px;font-size:18px;margin-top:16px;background:#4CAF50;color:#fff;border:none;border-radius:4px;cursor:pointer}"
         "</style></head><body>"
         "<h2>WiFi Connected</h2>"
         "<p>Device connected to <strong>" + ssid + "</strong>.</p>"
         "<p>IP: " + ip + "</p>"
         "<hr>"
         "<p><strong>Claim your device:</strong></p>"
         "<p><a href=\"" + claim_url + "\">" + claim_url + "</a></p>"
         "<p>Expires: " + expires_display + "</p>"
         "<p style=\"font-size:13px;color:#888;margin-top:8px\">"
         "On iPhone: tap <strong>&times;</strong> to close this window, then open the link in Safari.</p>"
         "<button onclick=\"window.location.href='" + claim_url + "'\">Continue in Zenso</button>"
         "</body></html>";
}

#endif
