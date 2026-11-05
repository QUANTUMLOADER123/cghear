#!/usr/bin/env bash
# ============================================================================
#  HYPERSTACK - Android APK builder (no Gradle plugins, no Android Studio).
#
#  Pipeline:  aapt2 compile+link (res+manifest) -> javac R stub -> d8 dex
#             -> ndk-build (libmain.so, arm64-v8a + armeabi-v7a)
#             -> zip add (classes.dex + lib/<abi>/libmain.so)
#             -> zipalign -> apksigner
#
#  Output:    build-android/hyperstack.apk  (debug-signed, installable)
#
#  Requires:  java, zip, curl, ANDROID_HOME (build-tools;34.0.0, platforms;34)
#             ANDROID_NDK (r25+)
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

# on failure, dump the error tail into GitHub annotations (readable via API)
LOG=/tmp/hyperstack_build.log
exec > >(tee "$LOG") 2>&1
trap 'rc=$?; if [ $rc -ne 0 ]; then
    sleep 3
    echo "::group::HYPERSTACK build failure (exit $rc)"; tail -60 "$LOG"; echo "::endgroup::";
    tail -35 "$LOG" | while IFS= read -r l; do echo "::error::$l"; done
fi' EXIT

API=34
BT=34.0.0
OUT=build-android
ABIS="arm64-v8a armeabi-v7a"

command -v java >/dev/null || { echo "ERROR: java not found"; exit 1; }
command -v zip  >/dev/null || { echo "ERROR: zip not found";  exit 1; }
[ -n "${ANDROID_HOME:-}" ] || { echo "ERROR: ANDROID_HOME is not set"; exit 1; }
[ -n "${ANDROID_NDK:-}" ]  || { echo "ERROR: ANDROID_NDK is not set (point it to your NDK, e.g. ~/Android/ndk/27.0.11718014)"; exit 1; }

SDK_BD="$ANDROID_HOME/build-tools/$BT"
for t in aapt2 d8 zipalign apksigner; do
    [ -x "$SDK_BD/$t" ] || { echo "ERROR: $SDK_BD/$t not found (install build-tools;$BT via sdkmanager)"; exit 1; }
done
[ -d "$ANDROID_HOME/platforms/android-$API" ] || { echo "ERROR: platforms;android-$API not found (sdkmanager \"platforms;android-$API\")"; exit 1; }
ANDROID_JAR="$ANDROID_HOME/platforms/android-$API/android.jar"

# fetch pinned raylib sources for ndk-build
if [ ! -f build/raylib-5.5/src/raylib.h ]; then
    echo "==> fetching raylib 5.5 sources"
    mkdir -p build
    curl -sL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5 | tar xz -C build
fi
[ -f build/raylib-5.5/src/raylib.h ] || { echo "ERROR: raylib fetch failed"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/obj" "$OUT/pkg"

echo "==> [1/5] resources + manifest (aapt2)"
"$SDK_BD/aapt2" compile --dir android/res -o "$OUT/res.zip"
"$SDK_BD/aapt2" link \
    -o "$OUT/base.apk" \
    --manifest android/AndroidManifest.xml \
    -I "$ANDROID_JAR" \
    --java "$OUT/classes" \
    --auto-add-overlay \
    "$OUT/res.zip"

echo "==> [2/5] java stub + dex (d8)  [hasCode=false APK still needs classes.dex]"
find "$OUT/classes" -name '*.java' > "$OUT/java.txt"
javac -source 8 -target 8 -classpath "$ANDROID_JAR" -d "$OUT/classes" $(cat "$OUT/java.txt") 2>/dev/null \
  || javac -classpath "$ANDROID_JAR" -d "$OUT/classes" $(cat "$OUT/java.txt")
find "$OUT/classes" -name '*.class' > "$OUT/classes.txt"
"$SDK_BD/d8" --release --min-api 21 --lib "$ANDROID_JAR" \
    --output "$OUT" $(cat "$OUT/classes.txt")
cp "$OUT/classes.dex" "$OUT/pkg/"

echo "==> [3/5] native lib: $ABIS"
NDKBUILD="$ANDROID_NDK/ndk-build"
[ -f "$NDKBUILD" ] || NDKBUILD="$ANDROID_NDK/ndk-build.cmd"
[ -f "$NDKBUILD" ] || { echo "ERROR: ndk-build not found in $ANDROID_NDK"; exit 1; }
"$NDKBUILD" -j4 \
    NDK_PROJECT_PATH="$PWD/$OUT" \
    APP_BUILD_SCRIPT="$PWD/android/Android.mk" \
    NDK_APPLICATION_MK="$PWD/android/Application.mk" \
    NDK_OUT="$PWD/$OUT/obj" \
    NDK_LIBS_OUT="$PWD/$OUT/raw"
cp -r "$OUT/raw/lib" "$OUT/pkg/lib"

echo "==> [4/5] packaging"
cp "$OUT/base.apk" "$OUT/unsigned.apk"
( cd "$OUT/pkg" && zip -q -n .so ../unsigned.apk classes.dex lib/*/libmain.so )
"$SDK_BD/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

echo "==> [5/5] signing"
KS="$OUT/debug.keystore"
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass hyperstack \
    -keypass hyperstack -alias hyperstack -keyalg RSA -keysize 2048 \
    -validity 10000 -dname "CN=HyperStack Debug,O=HyperStack,C=US"
"$SDK_BD/apksigner" sign --ks "$KS" --ks-pass pass:hyperstack \
    --key-pass pass:hyperstack --out "$OUT/hyperstack.apk" "$OUT/aligned.apk"

# sanity: the APK must contain the native lib for every ABI
for a in $ABIS; do
    unzip -l "$OUT/hyperstack.apk" | grep -q "lib/$a/libmain.so" \
        || { echo "ERROR: lib/$a/libmain.so missing from APK"; exit 1; }
done

echo
ls -lh "$OUT/hyperstack.apk"
echo "OK -> $OUT/hyperstack.apk"
echo "Install with:  adb install -r $OUT/hyperstack.apk"
