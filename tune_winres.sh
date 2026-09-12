#!/bin/sh

WINRES="winres.rc"
WINRES_XP="winres_xp.rc"
WINRES_MAN="winres.manifest.xml"

# Get version number components
VER="$(grep '^#define VER L*"' version.h | cut -d\" -f2)"
V1="$(echo "$VER" | cut -d. -f1)"; test -z "$V1" && V1=0
V2="$(echo "$VER" | cut -d. -f2)"; test -z "$V2" && V2=0
V3="$(echo "$VER" | cut -d. -f3)"; test -z "$V3" && V3=0
V4="$(echo "$VER" | cut -d. -f4)"; test -z "$V4" && V4=0
# Build VS_VERSION_INFO product version string with commas
PRODVER="$V1,$V2,$V3,$V4"
# Extend version to include four discrete numbers
XVER="$V1.$V2.$V3.$V4"
echo "$VER  =  $PRODVER ($XVER)"

# Actually change the manifest version information
sed -i 's/\([A-Z]*\)VERSION [0-9],.*/\1VERSION '"$PRODVER/"';s/"\([A-Za-z]*\)Version", "[0-9],.*"/"\1Version", '"\"$PRODVER\"/" "$WINRES"
sed -i 's/\([A-Z]*\)VERSION [0-9],.*/\1VERSION '"$PRODVER/"';s/"\([A-Za-z]*\)Version", "[0-9],.*"/"\1Version", '"\"$PRODVER\"/" "$WINRES_XP"
sed -i 's/\(assemblyIdentity .*\) version="[^"]*/\1 version="'$XVER/ "$WINRES_MAN"
