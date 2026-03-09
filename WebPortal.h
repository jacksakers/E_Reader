#ifndef WEBPORTAL_H
#define WEBPORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include "SD.h"
#include "EPD.h"

// ==================== WEB PORTAL CONFIGURATION ====================
// Choose one mode:
#define WEBPORTAL_MODE_AP 1      // Create Access Point
#define WEBPORTAL_MODE_STA 2     // Connect to existing WiFi

// Select mode here:
#define WEBPORTAL_MODE WEBPORTAL_MODE_AP

// AP Mode Configuration
#define AP_SSID "E-Reader-Portal"
#define AP_PASSWORD "ereader123"  // Set empty string "" for open network

// Station Mode Configuration (if connecting to existing WiFi)
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"

// ==================== WEB PORTAL STATE ====================
// External display buffer (defined in main file)
extern uint8_t ImageBW[27200];

namespace WebPortalNS {
  WebServer server(80);
  bool isRunning = false;
  bool needsDisplayUpdate = false;
  String statusMessage = "";
  int uploadedFiles = 0;
  IPAddress serverIP;
}

// ==================== HTML PAGE ====================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>E-Reader File Upload</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 700px;
            margin: 50px auto;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
        }
        .container {
            background: rgba(255, 255, 255, 0.95);
            padding: 30px;
            border-radius: 15px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.3);
            color: #333;
        }
        h1 {
            color: #667eea;
            text-align: center;
            margin-bottom: 10px;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 30px;
        }
        .tabs {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            border-bottom: 2px solid #667eea;
        }
        .tab {
            padding: 12px 25px;
            cursor: pointer;
            background: #f0f0f0;
            border: none;
            border-radius: 5px 5px 0 0;
            font-size: 16px;
            transition: all 0.3s;
        }
        .tab:hover {
            background: #e0e0e0;
        }
        .tab.active {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            font-weight: bold;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .upload-section {
            border: 3px dashed #667eea;
            border-radius: 10px;
            padding: 30px;
            text-align: center;
            background: #f8f9ff;
            transition: all 0.3s;
        }
        .upload-section:hover {
            border-color: #764ba2;
            background: #f0f2ff;
        }
        input[type="file"] {
            margin: 20px 0;
            padding: 10px;
            width: 100%;
            border: 2px solid #ddd;
            border-radius: 5px;
            background: white;
        }
        button {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            padding: 15px 40px;
            font-size: 16px;
            border-radius: 25px;
            cursor: pointer;
            transition: transform 0.2s;
        }
        button:hover {
            transform: scale(1.05);
        }
        button:active {
            transform: scale(0.95);
        }
        .status {
            margin-top: 20px;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
            font-weight: bold;
            display: none;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
            border: 2px solid #c3e6cb;
            display: block;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
            border: 2px solid #f5c6cb;
            display: block;
        }
        .info {
            background: #e7f3ff;
            padding: 15px;
            border-radius: 8px;
            border-left: 4px solid #667eea;
            margin-bottom: 20px;
        }
        .progress {
            width: 100%;
            height: 30px;
            background: #e0e0e0;
            border-radius: 15px;
            overflow: hidden;
            margin: 15px 0;
            display: none;
        }
        .progress-bar {
            height: 100%;
            background: linear-gradient(90deg, #667eea 0%, #764ba2 100%);
            width: 0%;
            transition: width 0.3s;
            text-align: center;
            line-height: 30px;
            color: white;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>📚 E-Reader Portal</h1>
        <p class="subtitle">Upload books and artwork to your E-Reader</p>
        
        <!-- Tabs -->
        <div class="tabs">
            <button class="tab active" onclick="switchTab('books')">📖 Books</button>
            <button class="tab" onclick="switchTab('art')">🎨 Art</button>
        </div>
        
        <!-- Books Tab -->
        <div id="booksTab" class="tab-content active">
            <div class="info">
                <strong>ℹ️ Book Upload:</strong>
                <ul style="margin: 10px 0; padding-left: 20px;">
                    <li>Upload .txt or .epub files</li>
                    <li>Files saved to /books/ folder</li>
                    <li>Maximum file size: 10MB</li>
                    <li>Files uploaded: <span id="bookCount">0</span></li>
                </ul>
            </div>
            
            <div class="upload-section">
                <h3>📁 Select Book File</h3>
                <form id="bookUploadForm" enctype="multipart/form-data">
                    <input type="file" name="file" id="bookFileInput" accept=".txt,.epub" required>
                    <br>
                    <button type="submit">🚀 Upload Book</button>
                </form>
                
                <div class="progress" id="bookProgress">
                    <div class="progress-bar" id="bookProgressBar">0%</div>
                </div>
            </div>
            
            <div id="bookStatus" class="status"></div>
        </div>
        
        <!-- Art Tab -->
        <div id="artTab" class="tab-content">
            <div class="info">
                <strong>ℹ️ Art Upload:</strong>
                <ul style="margin: 10px 0; padding-left: 20px;">
                    <li>Upload .art files (792x272 raw bitmap)</li>
                    <li>Files saved to /art/ folder</li>
                    <li>Use image converter to create .art files</li>
                    <li>Art frames cycle every 30 seconds</li>
                    <li>Files uploaded: <span id="artCount">0</span></li>
                </ul>
            </div>
            
            <div class="upload-section">
                <h3>🎨 Select Art File</h3>
                <form id="artUploadForm" enctype="multipart/form-data">
                    <input type="file" name="file" id="artFileInput" accept=".art" required>
                    <br>
                    <button type="submit">🚀 Upload Art</button>
                </form>
                
                <div class="progress" id="artProgress">
                    <div class="progress-bar" id="artProgressBar">0%</div>
                </div>
            </div>
            
            <div id="artStatus" class="status"></div>
        </div>
        
        <div style="margin-top: 30px; text-align: center; color: #666;">
            <small>Remember to press EXIT button on device to return</small>
        </div>
    </div>
    
    <script>
        let bookCount = 0;
        let artCount = 0;
        
        // Tab switching
        function switchTab(tab) {
            // Hide all tabs
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            
            // Show selected tab
            if (tab === 'books') {
                document.getElementById('booksTab').classList.add('active');
                document.querySelectorAll('.tab')[0].classList.add('active');
            } else {
                document.getElementById('artTab').classList.add('active');
                document.querySelectorAll('.tab')[1].classList.add('active');
            }
        }
        
        // Book upload handler
        document.getElementById('bookUploadForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            uploadFile('book', 
                      document.getElementById('bookFileInput'), 
                      '/upload',
                      'bookStatus', 
                      'bookProgress', 
                      'bookProgressBar',
                      'bookCount');
        });
        
        // Art upload handler
        document.getElementById('artUploadForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            uploadFile('art', 
                      document.getElementById('artFileInput'), 
                      '/uploadArt',
                      'artStatus', 
                      'artProgress', 
                      'artProgressBar',
                      'artCount');
        });
        
        // Generic upload function
        function uploadFile(type, fileInput, url, statusId, progressId, progressBarId, countId) {
            const file = fileInput.files[0];
            const statusDiv = document.getElementById(statusId);
            const progress = document.getElementById(progressId);
            const progressBar = document.getElementById(progressBarId);
            
            if (!file) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '❌ Please select a file';
                return;
            }
            
            // Show progress
            progress.style.display = 'block';
            statusDiv.style.display = 'none';
            
            const formData = new FormData();
            formData.append('file', file);
            
            const xhr = new XMLHttpRequest();
            
            xhr.upload.addEventListener('progress', (e) => {
                if (e.lengthComputable) {
                    const percent = Math.round((e.loaded / e.total) * 100);
                    progressBar.style.width = percent + '%';
                    progressBar.textContent = percent + '%';
                }
            });
            
            xhr.addEventListener('load', () => {
                progress.style.display = 'none';
                
                if (xhr.status === 200) {
                    statusDiv.className = 'status success';
                    statusDiv.textContent = '✅ File uploaded successfully: ' + file.name;
                    
                    if (type === 'book') {
                        bookCount++;
                        document.getElementById('bookCount').textContent = bookCount;
                    } else {
                        artCount++;
                        document.getElementById('artCount').textContent = artCount;
                    }
                    
                    fileInput.value = '';
                    progressBar.style.width = '0%';
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.textContent = '❌ Upload failed: ' + xhr.responseText;
                }
            });
            
            xhr.addEventListener('error', () => {
                progress.style.display = 'none';
                statusDiv.className = 'status error';
                statusDiv.textContent = '❌ Upload error occurred';
            });
            
            xhr.open('POST', url);
            xhr.send(formData);
        }
    </script>
</body>
</html>
)rawliteral";

// ==================== WEB PORTAL FUNCTIONS ====================

void webPortalUpdateDisplay(const char* title, const char* line1, const char* line2 = NULL, 
                            const char* line3 = NULL, const char* line4 = NULL) {
  Paint_Clear(WHITE);
  
  // Header
  EPD_DrawLine(0, 30, 792, 30, BLACK);
  EPD_ShowString(10, 5, (char*)title, 16, BLACK);
  
  int yPos = 50;
  if (line1) {
    EPD_ShowString(20, yPos, (char*)line1, 16, BLACK);
    yPos += 30;
  }
  if (line2) {
    EPD_ShowString(20, yPos, (char*)line2, 16, BLACK);
    yPos += 30;
  }
  if (line3) {
    EPD_ShowString(20, yPos, (char*)line3, 16, BLACK);
    yPos += 30;
  }
  if (line4) {
    EPD_ShowString(20, yPos, (char*)line4, 16, BLACK);
    yPos += 30;
  }
  
  // Footer
  EPD_DrawLine(0, 245, 792, 245, BLACK);
  EPD_ShowString(10, 250, (char*)"EXIT: Stop server and return to home", 16, BLACK);
  
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// Handle root page
void webPortalHandleRoot() {
  Serial.println("[PORTAL] Serving main page");
  WebPortalNS::server.send(200, "text/html", HTML_PAGE);
}

// Handle file upload
void webPortalHandleUpload() {
  HTTPUpload& upload = WebPortalNS::server.upload();
  static File uploadFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    
    String filepath = "/books" + filename;
    Serial.printf("[PORTAL] Upload started: %s\n", filepath.c_str());
    
    // Create /books directory if it doesn't exist
    if (!SD.exists("/books")) {
      SD.mkdir("/books");
    }
    
    uploadFile = SD.open(filepath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("[PORTAL] Failed to open file for writing");
    } else {
      WebPortalNS::statusMessage = "Uploading: " + filename;
      WebPortalNS::needsDisplayUpdate = true;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
      Serial.printf("[PORTAL] Writing %d bytes\n", upload.currentSize);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("[PORTAL] Upload complete: %d bytes\n", upload.totalSize);
      
      WebPortalNS::uploadedFiles++;
      WebPortalNS::statusMessage = "Upload complete!";
      WebPortalNS::needsDisplayUpdate = true;
    }
  }
}

// Send upload response
void webPortalHandleUploadResponse() {
  WebPortalNS::server.send(200, "text/plain", "Upload successful");
}

// Handle art file upload
void webPortalHandleArtUpload() {
  HTTPUpload& upload = WebPortalNS::server.upload();
  static File uploadFile;
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    
    String filepath = "/art" + filename;
    Serial.printf("[PORTAL] Art upload started: %s\n", filepath.c_str());
    
    // Create /art directory if it doesn't exist
    if (!SD.exists("/art")) {
      SD.mkdir("/art");
    }
    
    uploadFile = SD.open(filepath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("[PORTAL] Failed to open file for writing");
    } else {
      WebPortalNS::statusMessage = "Uploading art: " + filename;
      WebPortalNS::needsDisplayUpdate = true;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
      Serial.printf("[PORTAL] Writing %d bytes\n", upload.currentSize);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("[PORTAL] Art upload complete: %d bytes\n", upload.totalSize);
      
      WebPortalNS::uploadedFiles++;
      WebPortalNS::statusMessage = "Art upload complete!";
      WebPortalNS::needsDisplayUpdate = true;
    }
  }
}

// Send art upload response
void webPortalHandleArtUploadResponse() {
  WebPortalNS::server.send(200, "text/plain", "Art upload successful");
}

// Handle 404
void webPortalHandleNotFound() {
  WebPortalNS::server.send(404, "text/plain", "Not found");
}

// Initialize web portal
bool webPortalInit() {
  Serial.println("[PORTAL] Initializing Web Portal...");
  
  // Show connecting screen
  webPortalUpdateDisplay("WEB PORTAL", "Starting WiFi...", "Please wait...");
  
  bool connected = false;
  
  #if WEBPORTAL_MODE == WEBPORTAL_MODE_AP
    // Access Point Mode
    Serial.println("[PORTAL] Creating Access Point...");
    WiFi.mode(WIFI_AP);
    
    bool apStarted;
    if (strlen(AP_PASSWORD) > 0) {
      apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
    } else {
      apStarted = WiFi.softAP(AP_SSID);
    }
    
    if (apStarted) {
      WebPortalNS::serverIP = WiFi.softAPIP();
      Serial.printf("[PORTAL] AP started: %s\n", AP_SSID);
      Serial.printf("[PORTAL] IP Address: %s\n", WebPortalNS::serverIP.toString().c_str());
      connected = true;
    } else {
      Serial.println("[PORTAL] AP start failed");
    }
    
  #elif WEBPORTAL_MODE == WEBPORTAL_MODE_STA
    // Station Mode - Connect to existing WiFi
    Serial.printf("[PORTAL] Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      WebPortalNS::serverIP = WiFi.localIP();
      Serial.printf("\n[PORTAL] Connected! IP: %s\n", WebPortalNS::serverIP.toString().c_str());
      connected = true;
    } else {
      Serial.println("\n[PORTAL] WiFi connection failed");
    }
  #endif
  
  if (!connected) {
    webPortalUpdateDisplay("WEB PORTAL", "WiFi Failed!", "Press EXIT to return");
    return false;
  }
  
  // Setup web server routes
  WebPortalNS::server.on("/", HTTP_GET, webPortalHandleRoot);
  WebPortalNS::server.on("/upload", HTTP_POST, webPortalHandleUploadResponse, webPortalHandleUpload);
  WebPortalNS::server.on("/uploadArt", HTTP_POST, webPortalHandleArtUploadResponse, webPortalHandleArtUpload);
  WebPortalNS::server.onNotFound(webPortalHandleNotFound);
  
  // Start server
  WebPortalNS::server.begin();
  Serial.println("[PORTAL] Web server started");
  
  WebPortalNS::isRunning = true;
  WebPortalNS::uploadedFiles = 0;
  
  // Display connection info
  String ipStr = WebPortalNS::serverIP.toString();
  char ipLine[50];
  sprintf(ipLine, "IP: %s", ipStr.c_str());
  
  #if WEBPORTAL_MODE == WEBPORTAL_MODE_AP
    char ssidLine[50];
    sprintf(ssidLine, "WiFi: %s", AP_SSID);
    webPortalUpdateDisplay("WEB PORTAL - READY", 
                          ssidLine,
                          ipLine,
                          "Connect and upload files",
                          "Files uploaded: 0");
  #else
    webPortalUpdateDisplay("WEB PORTAL - READY",
                          "Connected to WiFi",
                          ipLine,
                          "Open browser to upload",
                          "Files uploaded: 0");
  #endif
  
  return true;
}

// Stop web portal
void webPortalStop() {
  Serial.println("[PORTAL] Stopping Web Portal...");
  
  WebPortalNS::server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  WebPortalNS::isRunning = false;
  
  Serial.println("[PORTAL] Web Portal stopped");
}

// Update web portal (call in loop)
void webPortalUpdate() {
  if (!WebPortalNS::isRunning) return;
  
  // Handle web server
  WebPortalNS::server.handleClient();
  
  // Update display if needed
  if (WebPortalNS::needsDisplayUpdate) {
    char uploadLine[50];
    sprintf(uploadLine, "Files uploaded: %d", WebPortalNS::uploadedFiles);
    
    String ipStr = WebPortalNS::serverIP.toString();
    char ipLine[50];
    sprintf(ipLine, "IP: %s", ipStr.c_str());
    
    webPortalUpdateDisplay("WEB PORTAL - ACTIVE",
                          ipLine,
                          uploadLine,
                          WebPortalNS::statusMessage.c_str());
    
    WebPortalNS::needsDisplayUpdate = false;
  }
}

// Check if running
bool webPortalIsRunning() {
  return WebPortalNS::isRunning;
}

#endif // WEBPORTAL_H
