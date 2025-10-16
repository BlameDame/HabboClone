#!/bin/bash

# Root directory containing your asset folders
ASSET_ROOT="/home/dame/HabboClone/client/game/assets"

# Tile size used in your game (adjust if needed)
TILE_WIDTH=32
TILE_HEIGHT=32

# Output folder for JSON metadata
OUTPUT_DIR="./metadata"
mkdir -p "$OUTPUT_DIR"

# Check if ImageMagick is installed
if ! command -v identify &> /dev/null; then
  echo "❌ ImageMagick not found! Install it using: sudo apt install imagemagick"
  exit 1
fi

# Loop through each subfolder (furniture, avatar, objects, walls)
for FOLDER in "$ASSET_ROOT"/*; do
  [ -d "$FOLDER" ] || continue
  CATEGORY=$(basename "$FOLDER")
  OUTPUT_FILE="$OUTPUT_DIR/${CATEGORY}.json"

  echo "{" > "$OUTPUT_FILE"
  FIRST=true

  for IMG in "$FOLDER"/*.png; do
    [ -f "$IMG" ] || continue
    NAME=$(basename "$IMG" .png)
    WIDTH=$(identify -format "%w" "$IMG")
    HEIGHT=$(identify -format "%h" "$IMG")

    # Estimate tile coverage (round up if larger than base tile)
    TILE_W=$(( (WIDTH + TILE_WIDTH - 1) / TILE_WIDTH ))
    TILE_H=$(( (HEIGHT + TILE_HEIGHT - 1) / TILE_HEIGHT ))

    if [ "$FIRST" = true ]; then
      FIRST=false
    else
      echo "," >> "$OUTPUT_FILE"
    fi

    cat <<EOF >> "$OUTPUT_FILE"
  "$NAME": {
    "sprite": "$CATEGORY/$NAME.png",
    "pixelWidth": $WIDTH,
    "pixelHeight": $HEIGHT,
    "tileWidth": $TILE_W,
    "tileHeight": $TILE_H,
    "offsetX": 0,
    "offsetY": 0,
    "depth": 0,
    "type": "$CATEGORY"
  }
EOF
  done

  echo "}" >> "$OUTPUT_FILE"
  echo "✅ Generated $OUTPUT_FILE"
done
