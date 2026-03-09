#!/usr/bin/env python3
"""
Art Frame Image Converter
Converts images to .art format for E-Reader Art Frame mode

Usage:
    python convert_to_art.py input.jpg output.art
    python convert_to_art.py --batch input_folder/ output_folder/

Requirements:
    pip install pillow
"""

import os
import sys
from PIL import Image
import argparse

# E-Reader display specifications
DISPLAY_WIDTH = 792
DISPLAY_HEIGHT = 272
TARGET_SIZE = 27200  # bytes

def convert_to_art(input_image_path, output_art_path, dither=True):
    """
    Convert an image to .art format for E-Reader Art Frame
    
    Args:
        input_image_path: Path to input image
        output_art_path: Path to output .art file
        dither: Apply dithering for better grayscale representation
    
    Returns:
        True if successful, False otherwise
    """
    try:
        print(f"📖 Loading: {input_image_path}")
        
        # Open image
        img = Image.open(input_image_path)
        
        # Get original dimensions
        orig_width, orig_height = img.size
        print(f"   Original size: {orig_width}x{orig_height}")
        
        # Calculate aspect ratios
        target_aspect = DISPLAY_WIDTH / DISPLAY_HEIGHT
        img_aspect = orig_width / orig_height
        
        # Resize to fit display while maintaining aspect ratio
        if img_aspect > target_aspect:
            # Image is wider - fit to width
            new_width = DISPLAY_WIDTH
            new_height = int(DISPLAY_WIDTH / img_aspect)
        else:
            # Image is taller - fit to height
            new_height = DISPLAY_HEIGHT
            new_width = int(DISPLAY_HEIGHT * img_aspect)
        
        img = img.resize((new_width, new_height), Image.LANCZOS)
        print(f"   Resized to: {new_width}x{new_height}")
        
        # Create a white canvas of display size
        canvas = Image.new('RGB', (DISPLAY_WIDTH, DISPLAY_HEIGHT), 'white')
        
        # Center the image on canvas
        x_offset = (DISPLAY_WIDTH - new_width) // 2
        y_offset = (DISPLAY_HEIGHT - new_height) // 2
        canvas.paste(img, (x_offset, y_offset))
        
        # Convert to 1-bit monochrome
        if dither:
            canvas = canvas.convert('1', dither=Image.FLOYDSTEINBERG)
            print(f"   Applied Floyd-Steinberg dithering")
        else:
            canvas = canvas.convert('1')
        
        # Rotate 180 degrees to match display orientation (Rotation = 180 in EPD.h)
        canvas = canvas.rotate(180)
        print(f"   Rotated 180° to match display")
        
        # Get pixel data
        pixels = list(canvas.getdata())
        
        # Pack bits into bytes (800x272 buffer format)
        # Note: We need to handle the dual-driver offset
        byte_array = bytearray()
        
        for y in range(DISPLAY_HEIGHT):
            for x in range(0, 800, 8):  # Using 800-pixel buffer width
                byte_val = 0
                for bit in range(8):
                    pixel_x = x + bit
                    
                    # Handle the 8-pixel offset for dual drivers
                    if pixel_x >= 396 and pixel_x < 404:
                        # This is the gap between drivers - fill with white
                        pass  # Leave as 0 (white)
                    else:
                        # Map buffer position to actual display position
                        if pixel_x >= 404:
                            display_x = pixel_x - 8
                        else:
                            display_x = pixel_x
                        
                        # Get pixel value (only if within display bounds)
                        if display_x < DISPLAY_WIDTH:
                            pixel_idx = y * DISPLAY_WIDTH + display_x
                            # In PIL mode '1': 0 = black, 255 = white
                            # In hardware: bit 0 = black, bit 1 = white
                            # So we set the bit only for white pixels
                            if pixels[pixel_idx] != 0:  # White pixel (255 in PIL)
                                byte_val |= (0x80 >> bit)
                
                byte_array.append(byte_val)
        
        # Verify size
        print(f"   Buffer size: {len(byte_array)} bytes (target: {TARGET_SIZE})")
        
        # Pad or trim to exact size if needed
        if len(byte_array) < TARGET_SIZE:
            byte_array.extend([0xFF] * (TARGET_SIZE - len(byte_array)))
        elif len(byte_array) > TARGET_SIZE:
            byte_array = byte_array[:TARGET_SIZE]
        
        # Write to file
        with open(output_art_path, 'wb') as f:
            f.write(byte_array)
        
        print(f"✅ Created: {output_art_path} ({len(byte_array)} bytes)")
        return True
        
    except Exception as e:
        print(f"❌ Error converting {input_image_path}: {e}")
        return False


def batch_convert(input_folder, output_folder, dither=True):
    """
    Convert all images in a folder to .art format
    
    Args:
        input_folder: Folder containing input images
        output_folder: Folder for output .art files
        dither: Apply dithering
    """
    # Create output folder if it doesn't exist
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)
        print(f"📁 Created output folder: {output_folder}")
    
    # Supported image extensions
    image_extensions = ('.png', '.jpg', '.jpeg', '.bmp', '.gif', '.tiff', '.webp')
    
    # Find all images
    image_files = []
    for filename in os.listdir(input_folder):
        if filename.lower().endswith(image_extensions):
            image_files.append(filename)
    
    if not image_files:
        print(f"❌ No image files found in {input_folder}")
        return
    
    print(f"\n🎨 Found {len(image_files)} image(s) to convert\n")
    
    # Convert each image
    success_count = 0
    for i, filename in enumerate(image_files, 1):
        print(f"[{i}/{len(image_files)}]")
        input_path = os.path.join(input_folder, filename)
        output_filename = os.path.splitext(filename)[0] + ".art"
        output_path = os.path.join(output_folder, output_filename)
        
        if convert_to_art(input_path, output_path, dither):
            success_count += 1
        print()  # Blank line between conversions
    
    print(f"\n{'='*60}")
    print(f"✅ Successfully converted {success_count}/{len(image_files)} images")
    print(f"📂 Output folder: {output_folder}")
    print(f"{'='*60}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Convert images to .art format for E-Reader Art Frame',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Convert single image:
    python convert_to_art.py photo.jpg photo.art
  
  Batch convert folder:
    python convert_to_art.py --batch ./images/ ./art_files/
  
  Disable dithering:
    python convert_to_art.py --no-dither image.png image.art
        """
    )
    
    parser.add_argument('input', help='Input image file or folder')
    parser.add_argument('output', nargs='?', help='Output .art file or folder')
    parser.add_argument('--batch', action='store_true', 
                       help='Batch convert all images in input folder')
    parser.add_argument('--no-dither', action='store_true',
                       help='Disable dithering (simple threshold instead)')
    
    args = parser.parse_args()
    
    # Check if Pillow is installed
    try:
        import PIL
    except ImportError:
        print("❌ Error: Pillow library not found")
        print("Install it with: pip install pillow")
        sys.exit(1)
    
    # Batch mode
    if args.batch:
        if not os.path.isdir(args.input):
            print(f"❌ Error: {args.input} is not a directory")
            sys.exit(1)
        
        output_folder = args.output if args.output else args.input + "_converted"
        batch_convert(args.input, output_folder, dither=not args.no_dither)
    
    # Single file mode
    else:
        if not args.output:
            print("❌ Error: Output file required for single file conversion")
            print("Usage: python convert_to_art.py input.jpg output.art")
            sys.exit(1)
        
        if not os.path.exists(args.input):
            print(f"❌ Error: Input file not found: {args.input}")
            sys.exit(1)
        
        success = convert_to_art(args.input, args.output, dither=not args.no_dither)
        sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
