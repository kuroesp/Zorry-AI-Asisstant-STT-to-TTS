<div align ="center">
  
  # 🤖 Zorry AI-Asisstant-STT to TTS 
  ### Real-time Edge Voice System
</div>

## ✨ Table of Contents
- [Why this project exists?](#%E2%80%8D-why-this-project-exists)
- [Project Introduction](#-project-introduction)
- [Features](#ⓘ-features)
- [Hardware & Software](#%EF%B8%8F-hardware--software)
- [API AI Services](#-ai-api-services)
- [Voice Asisstant Architecture](#_-voice-asisstant-architecture)


## 👨🏻‍💻 Why this project exists? 
In this current era, people are often too busy with their daily activities. Especially young people who move to other cities or live independently. We need something that can remind us of our obligations and help us stay on track. An AI Assistant was created to help people remember tasks and responsibilities, or simply to have short conversations in the middle of the night.


## ֎🇦🇮 Project Introduction

Zorry AI Assistant is a project that integrates three AI API services into a single system. It uses an ESP32 microcontroller connected to Wi-Fi to produce real-time audio streaming.

As a result, Zorry AI Assistant is capable of having simple conversations, helping users complete basic tasks, and supporting the development of good habits.

With a Voice Activity Detection (VAD) system, the AI can detect when the user is speaking or silent, reducing unnecessary data being sent to the API servers.


## ⓘ Features
- Wake word detection
- Real-time audio processing
- Memory Storage
- Manual input user data

## 🛠️ Hardware & Software 
### Hardware : 
- ESP32
- INMP441 (MIC)
- MAX98357A (i2s Speaker)
- Micro SD Card Adapter (Memory Place)
- Micro SD Card 16GB (Memory)
- Speaker 3 Watt & 8 Ohm (Speaker
- Wire : (Male to Male), (Male to Female)
- USB Cable
- Breadboard (Terminal) 

### Software : 
- Visual Studio Code
- Platformio
- CMD

## 🌐 AI API Services 
- OpenRouter -> GrogCloud API (STT) 
- OpenRouter -> Qwen API (LLM)
- Microsoft Edge API (TTS)

## >_ Voice Asisstant Architecture
- SETUP : When system get power supply from USB, Script will be setup program. 
- MIC : Mic will be turn on, ready to listening user speech.
- VAD System : VAD will make desicion if speech then listening continues, if not speech then wait until user speech.
- LLM System : After a speech merge into one segment then, request to send the prompt into GrogCloud API to convert Speech to Text.
- LLM System (Get Reply) : Text from GrogCloud API and memory from SD Card (Core Memory) sended to Qwen API to get the reply.
- Parse Json : After get a reply, Program will parse json format to get the clean string reply.
- ESP32 : The clean string reply will be forward to ESP32 and convert to MP3 format and request to Server.py.
- Server.py : While to convert format, Script Server.py will be initialized Microsoft Edge TTS then, get request from ESP32 to convert MP3 to PCM format and get better voices and low noises
- MAX98357A : After get the voice, with PCM format MAX98357A will be write the PCM and send to Speaker
- Speaker : The reply AI now, become a voice. 





