#!/bin/sh
# Wraps site/index.html (written as an artifact body, without <html>/<head>) into a full page for GitHub Pages.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/../_site"}
url="https://sarkarshubhdeep.github.io/tessera/"
mkdir -p "$out"
{
  cat <<HEAD
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="description" content="Tessera: an interface for small AI devices where the agent designs the screen and the device draws it natively. Open-source firmware for ESP32 square-screen boards, starting with the ESP-Mosaico.">
<meta property="og:type" content="website">
<meta property="og:title" content="Tessera">
<meta property="og:description" content="The agent designs the screen. The device draws it.">
<meta property="og:url" content="$url">
<meta property="og:image" content="${url}og.png">
<meta name="twitter:card" content="summary_large_image">
<meta name="theme-color" content="#000000">
<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}body{margin:0}img{max-width:100%}[hidden]{display:none!important}</style>
</head>
<body>
HEAD
  cat "$here/index.html"
  printf '\n</body>\n</html>\n'
} > "$out/index.html"
if [ -f "$here/../design/screenshots/og.png" ]; then cp "$here/../design/screenshots/og.png" "$out/og.png"; fi
echo "built $out/index.html"
