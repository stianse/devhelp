#!/bin/sh

rm -rf devhelp/ repo/
flatpak-builder devhelp org.gnome.Devhelp.json || exit 1
flatpak build-export repo devhelp
