# Open Sespme 
[![Build Status](https://travis-ci.org/tylercrumpton/open-sespame.svg?branch=master)](https://travis-ci.org/tylercrumpton/open-sespame)

ESP + PN532 + MQTT = door opening goodness

### Build and Deploy
	pip install platformio
        cp src/config.example.h src/config.h
	platformio run
	platformio run -t upload
