#!/bin/bash
set -e

REPO="jptrsn/esp-airsync"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -f "$SCRIPT_DIR/.env" ]; then
    source "$SCRIPT_DIR/.env"
fi

if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GITHUB_TOKEN not set. Add it to .env or set it in your environment."
    exit 1
fi

TAG=$(grep 'set(PROJECT_VER' "$SCRIPT_DIR/CMakeLists.txt" | grep -o '"[^"]*"' | tr -d '"')

if [ -z "$TAG" ]; then
    echo "Error: Could not read PROJECT_VER from CMakeLists.txt"
    exit 1
fi

echo "Releasing $TAG..."

if [ -f "$SCRIPT_DIR/.env" ]; then
    source "$SCRIPT_DIR/.env"
fi

if [ -z "$GITHUB_TOKEN" ]; then
    echo "Error: GITHUB_TOKEN not set. Add it to .env or set it in your environment."
    exit 1
fi

echo "Building esp32..."
rm -rf build
unset IDF_TARGET
idf.py set-target esp32 && idf.py build
cp build/esp-airsync.bin esp-airsync-esp32.bin
cp build/esp-idf/esp-raop-receiver/libesp-raop-receiver.a libesp-raop-receiver-esp32.a

echo "Building esp32s3..."
rm -rf build
unset IDF_TARGET
idf.py set-target esp32s3 && idf.py build
cp build/esp-airsync.bin esp-airsync-esp32s3.bin
cp build/esp-idf/esp-raop-receiver/libesp-raop-receiver.a libesp-raop-receiver-esp32s3.a

echo "Tagging release $TAG..."
git tag "$TAG"
git push origin "$TAG"

sleep 2  # give GitHub a moment to register the tag

echo "Creating release $TAG..."
RELEASE=$(curl -s -X POST \
    -H "Authorization: token $GITHUB_TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"tag_name\": \"$TAG\", \"name\": \"$TAG\", \"body\": \"\", \"draft\": false}" \
    https://api.github.com/repos/$REPO/releases)

RELEASE_ID=$(echo $RELEASE | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)

if [ -z "$RELEASE_ID" ]; then
    echo "Error: Failed to create release. Response: $RELEASE"
    exit 1
fi

echo "Uploading assets..."
for ASSET in \
    "esp-airsync-esp32.bin" \
    "esp-airsync-esp32s3.bin" \
    "libesp-raop-receiver-esp32.a" \
    "libesp-raop-receiver-esp32s3.a"; do
    curl -s -X POST \
        -H "Authorization: token $GITHUB_TOKEN" \
        -H "Content-Type: application/octet-stream" \
        --data-binary @"$ASSET" \
        "https://api.github.com/repos/$REPO/releases/$RELEASE_ID/assets?name=$ASSET"
done

echo "Cleaning up..."
rm esp-airsync-esp32.bin esp-airsync-esp32s3.bin libesp-raop-receiver-esp32.a libesp-raop-receiver-esp32s3.a

echo "Done! Release $TAG created."