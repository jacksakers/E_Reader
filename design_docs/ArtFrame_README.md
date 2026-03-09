# Art Frame Mode - User Guide

## Overview
The Art Frame mode allows your E-Reader to function as a digital picture frame, cycling through artwork automatically or manually. Perfect for displaying artwork, photos, or designs on your e-ink display with zero power consumption when idle.

## Features
- ✅ **Auto-cycling**: Art changes every 30 seconds automatically
- ✅ **Manual navigation**: Use UP/DOWN buttons to browse your collection
- ✅ **Web upload**: Upload art files directly via the Web Portal
- ✅ **Safe rendering**: Proper bounds checking prevents display crashes
- ✅ **Low power**: E-ink holds images without power

## How to Use

### 1. Prepare Your Art Files

Art files must be in `.art` format (raw bitmap format for e-ink displays).

**File Format Requirements:**
- **Resolution**: 792 x 272 pixels (actual visible display area)
- **Format**: Raw bitmap data, 1 bit per pixel
- **Size**: Approximately 27,200 bytes
- **Extension**: `.art`

### 2. Converting Images to .art Format

You'll need to convert your images to the special .art format. Here are the steps:

#### Method 1: Python Script (Recommended)

```python
from PIL import Image
import os

def convert_to_art(input_image_path, output_art_path):
    """
    Convert an image to .art format for E-Reader Art Frame
    Resolution: 792x272, 1-bit monochrome
    """
    # Open and convert image
    img = Image.open(input_image_path)
    
    # Resize to display dimensions
    img = img.resize((792, 272), Image.LANCZOS)
    
    # Convert to 1-bit monochrome
    img = img.convert('1')
    
    # Get pixel data
    pixels = list(img.getdata())
    
    # Pack bits into bytes
    byte_array = bytearray()
    for y in range(272):
        for x in range(0, 792, 8):
            byte_val = 0
            for bit in range(8):
                if x + bit < 792:
                    pixel_idx = y * 792 + x + bit
                    if pixels[pixel_idx] == 0:  # Black pixel
                        byte_val |= (0x80 >> bit)
            byte_array.append(byte_val)
    
    # Pad to 27200 bytes if needed
    while len(byte_array) < 27200:
        byte_array.append(0xFF)
    
    # Write to file
    with open(output_art_path, 'wb') as f:
        f.write(byte_array)
    
    print(f"✅ Converted: {output_art_path} ({len(byte_array)} bytes)")

# Example usage
if __name__ == "__main__":
    convert_to_art("myimage.jpg", "myimage.art")
```

**Installation:**
```bash
pip install pillow
python convert_to_art.py
```

#### Method 2: Batch Convert Multiple Images

```python
import os
from PIL import Image

def batch_convert(input_folder, output_folder):
    """Convert all images in a folder to .art format"""
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)
    
    for filename in os.listdir(input_folder):
        if filename.lower().endswith(('.png', '.jpg', '.jpeg', '.bmp', '.gif')):
            input_path = os.path.join(input_folder, filename)
            output_filename = os.path.splitext(filename)[0] + ".art"
            output_path = os.path.join(output_folder, output_filename)
            
            try:
                convert_to_art(input_path, output_path)
            except Exception as e:
                print(f"❌ Failed to convert {filename}: {e}")

# Use it
batch_convert("./my_images", "./art_files")
```

### 3. Upload Art via Web Portal

1. **Enter Web Portal Mode** on your E-Reader device
2. **Connect to WiFi**:
   - Default SSID: `E-Reader-Portal`
   - Default password: `ereader123`
3. **Open browser** and go to the displayed IP address (e.g., `192.168.4.1`)
4. **Click the "Art" tab** at the top of the page
5. **Select your .art file** and click "Upload Art"
6. **Wait for confirmation** - you'll see "Upload successful"

### 4. View Your Art Collection

1. **Return to Home** by pressing EXIT on the device
2. **Select "Art Frame"** from the main menu
3. **Enjoy!** Your art will display and auto-cycle every 30 seconds

## Controls

While in Art Frame mode:

| Button | Action |
|--------|--------|
| **UP (PRV)** | Previous art piece |
| **DOWN (NEXT)** | Next art piece |
| **OK** | Toggle auto-cycle ON/OFF |
| **EXIT/HOME** | Return to main menu |

## Tips & Best Practices

### Image Preparation
- **High contrast works best** - E-ink displays are monochrome (black and white only)
- **Simple designs** look better than complex photos
- **Line art and illustrations** are ideal for e-ink
- **Consider dithering** for photos to create grayscale effect

### Storage Management
- The SD card can hold thousands of art files
- Organize files in the `/art` folder
- Keep file sizes around 27KB each

### Power Efficiency
- **Display holds image without power** - perfect for always-on display
- Auto-cycle uses minimal power (wakes every 30 seconds)
- Disable auto-cycle to maximize battery life when viewing a single piece

### Creating Great E-Ink Art
1. Start with high-resolution source images
2. Adjust contrast/brightness before converting
3. Test different dithering algorithms (Floyd-Steinberg works well)
4. Preview on a monochrome display if possible

## Troubleshooting

### "No .art files found"
- Check that files have `.art` extension (case-sensitive on some systems)
- Verify files are in the `/art` folder on SD card
- Try re-uploading via Web Portal

### Display shows garbled images
- File may be wrong size - check it's exactly 27,200 bytes or close
- Try reconverting the source image
- Ensure conversion script is using correct dimensions (792x272)

### Art doesn't cycle
- Check if auto-cycle is enabled (press OK to toggle)
- Verify you have multiple art files
- Device may be low on battery

### Upload fails
- Check WiFi connection
- Verify file has `.art` extension
- Try uploading smaller files first
- Restart Web Portal mode if needed

## File Structure

```
SD Card Root
└── art/
    ├── landscape1.art
    ├── portrait2.art
    ├── abstract3.art
    └── ... (more art files)
```

## Advanced: Manual File Copy

If you prefer to copy files directly to the SD card:

1. Remove SD card from device
2. Insert into computer
3. Create `/art` folder if it doesn't exist
4. Copy your `.art` files into `/art` folder
5. Safely eject SD card
6. Insert back into E-Reader
7. Enter Art Frame mode

## Technical Details

- **Display Resolution**: 792x272 pixels (visible area)
- **Buffer Size**: 800x272 pixels (includes 8-pixel offset for dual-driver)
- **Format**: 1-bit monochrome bitmap
- **Memory Usage**: ~27KB per image
- **Cycle Interval**: 30 seconds (configurable in `ArtFrame.h`)
- **Max Images**: 100 (configurable in `ArtFrame.h`)

## Configuration

You can customize the Art Frame behavior by editing `ArtFrame.h`:

```cpp
#define ARTFRAME_CYCLE_INTERVAL 30000  // Change cycle time (milliseconds)
#define ARTFRAME_MAX_IMAGES 100        // Change max number of images
```

## Future Enhancements

Potential features for future updates:
- Support for multiple image formats (BMP, PNG conversion on-device)
- Adjustable cycle intervals via Settings menu
- Slideshow effects/transitions
- Random vs sequential ordering
- Favorite/bookmark specific art pieces
- Integration with online art galleries

## Credits

Art Frame mode designed for the ELECROW CrowPanel ESP32 5.79" E-Paper HMI Display.

---

**Enjoy your digital art gallery!** 🎨🖼️
