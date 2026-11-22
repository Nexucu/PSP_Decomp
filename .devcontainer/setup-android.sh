#!/usr/bin/env bash
# Setup del Android SDK dentro de un GitHub Codespace (sin proxy).
# Idempotente: se puede correr de nuevo sin romper nada.
# Instala: cmdline-tools, platform-tools, platforms;android-34,
# build-tools;34.0.0, ndk;26.1.10909125, cmake;3.22.1
# y deja local.properties apuntando al SDK instalado.
set -euo pipefail

export ANDROID_HOME="${ANDROID_HOME:-/opt/android-sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
CMDLINE_VERSION="11076708"

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
  echo "Instalando Android cmdline-tools..."
  sudo mkdir -p "$ANDROID_HOME/cmdline-tools"
  cd /tmp
  curl -fsSL -o cmdtools.zip \
    "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_VERSION}_latest.zip"
  sudo unzip -q -o cmdtools.zip -d "$ANDROID_HOME/cmdline-tools/tmp"
  sudo mv "$ANDROID_HOME/cmdline-tools/tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
  sudo rmdir "$ANDROID_HOME/cmdline-tools/tmp" || true
  rm -f cmdtools.zip
  sudo chown -R "$(whoami)" "$ANDROID_HOME" || true
fi

export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
yes | sdkmanager --licenses >/dev/null 2>&1 || true
sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
  "ndk;26.1.10909125" "cmake;3.22.1"

cat > local.properties <<EOF
sdk.dir=$ANDROID_HOME
ndk.dir=$ANDROID_HOME/ndk/26.1.10909125
EOF

echo "SDK listo en $ANDROID_HOME"
echo "Para compilar: ./gradlew assembleDebug"
echo "APK en: app/build/outputs/apk/debug/app-debug.apk"
