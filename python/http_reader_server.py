# epd_server.py
# Run this on your Raspberry Pi 4
# Install requirements first: pip install flask requests beautifulsoup4 pillow

from flask import Flask, request, jsonify, Response
import requests
from bs4 import BeautifulSoup
from PIL import Image
import io

app = Flask(__name__)

# Helper to configure standard headers so websites don't block us
def get_headers():
    return {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)'
    }

@app.route('/text')
def get_text():
    """
    Fetches a URL, strips out the HTML, and returns the paragraph text.
    Usage: http://<pi_ip>:5000/text?url=https://example.com
    """
    url = request.args.get('url')
    limit = int(request.args.get('limit', 4000)) # ESP32 RAM limit safeguard
    
    if not url:
        return jsonify({"success": False, "error": "Missing URL parameter"}), 400

    try:
        res = requests.get(url, headers=get_headers(), timeout=10)
        res.raise_for_status()
        
        soup = BeautifulSoup(res.content, 'html.parser')
        
        # Extract text mostly from paragraphs to avoid navbars and scripts
        paragraphs = soup.find_all('p')
        text_content = "\n\n".join([p.get_text().strip() for p in paragraphs if p.get_text().strip()])
        
        # Truncate to save ESP32 memory if it's too long
        if len(text_content) > limit:
            text_content = text_content[:limit] + "\n\n[...Article Truncated...]"
            
        return jsonify({
            "success": True, 
            "text": text_content
        })
        
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

@app.route('/image.bin')
def get_image_bin():
    """
    Fetches an image URL, resizes it, converts it to 1-bit dithered black & white, 
    and returns raw binary byte data (8 pixels per byte) optimized for EPDs.
    Usage: http://<pi_ip>:5000/image.bin?url=https://example.com/img.jpg&w=648&h=480
    """
    url = request.args.get('url')
    # Defaulting to 648x480 (a common resolution for 5.79/5.83" EPDs)
    w = int(request.args.get('w', 648))
    h = int(request.args.get('h', 480))
    
    if not url:
        return "Missing URL parameter", 400

    try:
        res = requests.get(url, headers=get_headers(), timeout=10)
        res.raise_for_status()
        
        # Open image from web response
        img = Image.open(io.BytesIO(res.content))
        
        # Create a white background canvas sized specifically for the EPD
        background = Image.new('1', (w, h), 1) # '1' is 1-bit, 1 is white
        
        # Resize original image keeping aspect ratio
        img.thumbnail((w, h), Image.Resampling.LANCZOS)
        
        # Convert to 1-bit black and white using Floyd-Steinberg dithering
        img_bw = img.convert('1')
        
        # Calculate center offset
        offset_x = int((w - img.width) / 2)
        offset_y = int((h - img.height) / 2)
        
        # Paste the dithered image onto the center of the white canvas
        background.paste(img_bw, (offset_x, offset_y))
        
        # Get raw bytes (PIL naturally packs 8 pixels per byte)
        raw_bytes = background.tobytes()
        
        # Send raw octet-stream so the ESP32 can stream it directly into a memory buffer
        return Response(raw_bytes, mimetype='application/octet-stream')
        
    except Exception as e:
        return str(e), 500

if __name__ == '__main__':
    # Run on all network interfaces on port 5000
    app.run(host='0.0.0.0', port=5000)