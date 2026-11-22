#!/bin/bash
# build-apk.sh — Construye el APK SIN Gradle (sin red): compila el C++ con
# el clang del NDK, empaqueta con aapt2, alinea con zipalign y firma con
# apksigner. El camino canónico sigue siendo `./gradlew assembleDebug`
# (en Android Studio o una máquina con red); este script existe porque en
# esta VM el HttpClient de Gradle no logra atravesar el proxy de egress
# (ver README, "Estado del build").
#
# Requisitos (ya instalados en esta VM):
#   ANDROID_SDK=/home/hatch/android-sdk con platforms;android-34,
#   build-tools;34.0.0, ndk;26.1.10909125
#   JDK 17 (para keytool): $JAVA_HOME/bin en PATH, o Temurin en
#   ~/android-sdk-dl/jdk-17.0.21.1+2
# Uso: ./build-manual/build-apk.sh [--abi arm64-v8a|armeabi-v7a|all]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK:-$HOME/android-sdk}"
NDK="$SDK/ndk/26.1.10909125"
BT="$SDK/build-tools/34.0.0"
PLATFORM="$SDK/platforms/android-34/android.jar"
OUT="$ROOT/build-manual"
ABIS="arm64-v8a"
[ "${1:-}" = "--abi" ] && { [ "$2" = "all" ] && ABIS="arm64-v8a armeabi-v7a" || ABIS="$2"; }

export JAVA_HOME="${JAVA_HOME:-$HOME/android-sdk-dl/jdk-17.0.21.1+2}"
export PATH="$JAVA_HOME/bin:$PATH"

TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
GLUE="$NDK/sources/android/native_app_glue"
CPP="$ROOT/app/src/main/cpp"

echo "== 1. Librerías nativas ($ABIS)"
for ABI in $ABIS; do
    case "$ABI" in
        arm64-v8a)     TRIPLE=aarch64-linux-android; API=26; DIR=arm64-v8a ;;
        armeabi-v7a)   TRIPLE=armv7a-linux-androideabi; API=26; DIR=armeabi-v7a ;;
        *) echo "ABI desconocido: $ABI"; exit 1 ;;
    esac
    mkdir -p "$OUT/$DIR"
    "$TC/${TRIPLE}${API}-clang" -O2 -fPIC -DANDROID -I"$GLUE" \
        -c "$GLUE/android_native_app_glue.c" -o "$OUT/$DIR/glue.o"
    for SRC in main renderer_vulkan flower_scene tga; do
        "$TC/${TRIPLE}${API}-clang++" -std=c++17 -O2 -fPIC -DANDROID -I"$GLUE" \
            -c "$CPP/$SRC.cpp" -o "$OUT/$DIR/$SRC.o"
    done
    "$TC/${TRIPLE}${API}-clang++" -shared -Wl,-soname,libflower_vulkan.so \
        "$OUT/$DIR/"*.o -o "$OUT/$DIR/libflower_vulkan.so" \
        -landroid -llog -lvulkan
    # libc++_shared.so es NEEDED por el .so (STL del NDK): AGP lo empaqueta
    # solo; aquí hay que copiarlo a mano o la app no cargaría en el móvil.
    case "$ABI" in
        arm64-v8a)   STL_DIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android" ;;
        armeabi-v7a) STL_DIR="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/arm-linux-androideabi" ;;
    esac
    cp "$STL_DIR/libc++_shared.so" "$OUT/$DIR/"
    echo "   $ABI: $(stat -c%s "$OUT/$DIR/libflower_vulkan.so") bytes"
done

echo "== 2. APK con aapt2"
rm -rf "$OUT/apk-staging" && mkdir -p "$OUT/apk-staging"
"$BT/aapt2" link -o "$OUT/unsigned.apk" --manifest "$ROOT/app/src/main/AndroidManifest.xml" \
    -I "$PLATFORM" --auto-add-overlay \
    --min-sdk-version 26 --target-sdk-version 34 \
    --version-code 1 --version-name 1.0
cd "$OUT/apk-staging"
unzip -q -o "$OUT/unsigned.apk"
for ABI in $ABIS; do
    case "$ABI" in
        arm64-v8a) DIR=arm64-v8a ;; armeabi-v7a) DIR=armeabi-v7a ;;
    esac
    mkdir -p "lib/$DIR"
    cp "$OUT/$DIR/"*.so "lib/$DIR/"  # libflower_vulkan.so + libc++_shared.so
done
mkdir -p assets
cp -r "$ROOT/app/src/main/assets/data" assets/data
cp -r "$ROOT/app/src/main/assets/shaders" assets/shaders
zip -q -r "$OUT/unsigned.apk" lib assets
cd "$ROOT"

echo "== 3. zipalign + firma"
KEYSTORE="$OUT/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -alias flower-debug \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass android -keypass android \
        -dname "CN=Flower Debug, O=Moppi Port" 2>/dev/null
fi
"$BT/zipalign" -f 4 "$OUT/unsigned.apk" "$OUT/flower-vulkan-debug.apk"
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android \
    --key-pass pass:android --ks-key-alias flower-debug \
    --v1-signing-enabled true --v2-signing-enabled true \
    --out "$OUT/flower-vulkan-debug-signed.apk" "$OUT/flower-vulkan-debug.apk"
"$BT/apksigner" verify --verbose "$OUT/flower-vulkan-debug-signed.apk"
echo "APK: $OUT/flower-vulkan-debug-signed.apk ($(stat -c%s "$OUT/flower-vulkan-debug-signed.apk") bytes)"
"$BT/aapt2" dump badging "$OUT/flower-vulkan-debug-signed.apk" | head -8
